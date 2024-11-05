#include "VideoDecoder.h"
#include "AudioManager.h"
#include "../utils/Logger.h"

extern "C" {
    #include <libavutil/imgutils.h>  // Ajout de cet include pour av_image_copy
}

VideoDecoder::VideoDecoder()
    : formatContext(nullptr)
    , codecContext(nullptr)
    , audioCodecContext(nullptr)
    , videoStreamIndex(-1)
    , audioStreamIndex(-1)
    , audioManager(nullptr)
    , isRunning(false) {
}

VideoDecoder::~VideoDecoder() {
    stopDecoding();
}

bool VideoDecoder::initialize(const std::string& path) {
    formatContext = avformat_alloc_context();
    if (!formatContext) {
        Logger::logError("Could not allocate format context");
        return false;
    }

    if (avformat_open_input(&formatContext, path.c_str(), nullptr, nullptr) < 0) {
        Logger::logError("Could not open video file");
        return false;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        Logger::logError("Could not find stream info");
        return false;
    }

    // Find video and audio streams
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
        } else if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
        }
    }

    if (videoStreamIndex == -1) {
        Logger::logError("Could not find video stream");
        return false;
    }

    // Vérifier le codec de la vidéo
    const AVCodec* videoCodec = NULL;
    if (formatContext->streams[videoStreamIndex]->codecpar->codec_id == AV_CODEC_ID_HEVC) {
        videoCodec = avcodec_find_decoder_by_name("hevc_v4l2m2m");
        if (!videoCodec) {
            Logger::logInfo("Hardware H.265 decoder not available, falling back to software decoder");
            videoCodec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
        }
    } else {
        videoCodec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    }

    if (!videoCodec) {
        Logger::logError("Unsupported video codec");
        return false;
    }

    Logger::logInfo("Video dimensions: " + std::to_string(formatContext->streams[videoStreamIndex]->codecpar->width) + 
                   "x" + std::to_string(formatContext->streams[videoStreamIndex]->codecpar->height));
    Logger::logInfo("Input pixel format: " + std::string(av_get_pix_fmt_name(
        static_cast<AVPixelFormat>(formatContext->streams[videoStreamIndex]->codecpar->format))));
    Logger::logInfo("Pixel format description: " + std::string(av_get_pix_fmt_name(AV_PIX_FMT_YUV420P)));

    codecContext = avcodec_alloc_context3(videoCodec);
    if (!codecContext) {
        Logger::logError("Could not allocate video codec context");
        return false;
    }

    if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        Logger::logError("Could not copy video codec params");
        return false;
    }

    // Configuration du décodage - IMPORTANT
    codecContext->thread_count = 1;  // Désactiver le multi-threading pour éviter les problèmes
    codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;  // Réduire la latence

    // Ajouter ces options pour le codec context
    codecContext->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;  // Permet de continuer même avec des frames corrompues
    codecContext->err_recognition = 0;  // Moins strict sur les erreurs

    if (avcodec_open2(codecContext, videoCodec, nullptr) < 0) {
        Logger::logError("Could not open video codec");
        return false;
    }

    // Initialize audio codec if available
    if (audioStreamIndex >= 0) {
        const AVCodec* audioCodec = avcodec_find_decoder(formatContext->streams[audioStreamIndex]->codecpar->codec_id);
        if (!audioCodec) {
            Logger::logError("Unsupported audio codec");
            return false;
        }

        audioCodecContext = avcodec_alloc_context3(audioCodec);
        if (!audioCodecContext) {
            Logger::logError("Could not allocate audio codec context");
            return false;
        }

        if (avcodec_parameters_to_context(audioCodecContext, formatContext->streams[audioStreamIndex]->codecpar) < 0) {
            Logger::logError("Could not copy audio codec params");
            return false;
        }

        if (avcodec_open2(audioCodecContext, audioCodec, nullptr) < 0) {
            Logger::logError("Could not open audio codec");
            return false;
        }

        Logger::logInfo("Audio codec initialized successfully");
    }

    // Ajouter un délai initial pour permettre le remplissage des buffers
    Logger::logInfo("Waiting for initial buffering...");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    return true;
}

void VideoDecoder::startDecoding() {
    isRunning = true;
    decodeThread = std::thread(&VideoDecoder::decodeThreadFunction, this);
}

void VideoDecoder::stopDecoding() {
    isRunning = false;
    condition.notify_all();
    
    if (decodeThread.joinable()) {
        decodeThread.join();
    }

    // Cleanup resources
    if (codecContext) {
        avcodec_free_context(&codecContext);
    }
    if (formatContext) {
        avformat_close_input(&formatContext);
    }
}

AVFrame* VideoDecoder::getNextFrame() {
    std::unique_lock<std::mutex> lock(mutex);
    if (frameQueue.empty()) {
        return nullptr;
    }
    
    AVFrame* frame = frameQueue.front();
    frameQueue.pop();
    lock.unlock();
    condition.notify_one();
    return frame;
}

AVStream* VideoDecoder::getVideoStream() const {
    if (videoStreamIndex >= 0) {
        return formatContext->streams[videoStreamIndex];
    }
    return nullptr;
}

AVStream* VideoDecoder::getAudioStream() const {
    if (audioStreamIndex >= 0) {
        return formatContext->streams[audioStreamIndex];
    }
    return nullptr;
}

void VideoDecoder::decodeThreadFunction() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    
    int64_t video_frame_count = 0;
    int64_t audio_frame_count = 0;
    
    Logger::logInfo("Starting decode thread, buffering initial frames...");
    
    // Attendre un peu avant de commencer le décodage vidéo
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    while (isRunning) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (frameQueue.size() >= MAX_QUEUE_SIZE) {
                condition.wait(lock);
                continue;
            }
        }

        int ret = av_read_frame(formatContext, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                Logger::logInfo("End of file reached, seeking to start");
                seekToStart();
                continue;
            }
            break;
        }

        // Traiter d'abord quelques paquets audio avant de commencer la vidéo
        if (video_frame_count == 0 && audio_frame_count < 5 && 
            packet->stream_index == audioStreamIndex && audioCodecContext && audioManager) {
            // Traitement des paquets audio initiaux
            // ... code existant pour l'audio ...
        }
        // Traitement normal des paquets
        else if (packet->stream_index == videoStreamIndex) {
            Logger::logInfo("Processing video packet - size: " + std::to_string(packet->size) + 
                          ", pts: " + std::to_string(packet->pts));
            
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                Logger::logError("Error sending video packet: " + std::to_string(ret));
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    Logger::logError("Error receiving video frame: " + std::to_string(ret));
                    break;
                }

                // Vérification plus stricte des buffers vidéo
                if (!frame || !frame->data[0] || !frame->buf[0]) {
                    Logger::logError("Invalid video frame data - skipping");
                    continue;
                }

                Logger::logInfo("Creating video frame copy...");
                
                // Utiliser av_frame_alloc et av_frame_ref au lieu de av_frame_clone
                AVFrame* frame_copy = av_frame_alloc();
                if (!frame_copy) {
                    Logger::logError("Failed to allocate video frame");
                    continue;
                }

                // Copier les propriétés de base avant av_frame_ref
                frame_copy->format = frame->format;
                frame_copy->width = frame->width;
                frame_copy->height = frame->height;

                ret = av_frame_get_buffer(frame_copy, 32);
                if (ret < 0) {
                    Logger::logError("Failed to allocate frame buffers");
                    av_frame_free(&frame_copy);
                    continue;
                }

                ret = av_frame_copy(frame_copy, frame);
                if (ret < 0) {
                    Logger::logError("Failed to copy frame data");
                    av_frame_free(&frame_copy);
                    continue;
                }

                ret = av_frame_copy_props(frame_copy, frame);
                if (ret < 0) {
                    Logger::logError("Failed to copy frame properties");
                    av_frame_free(&frame_copy);
                    continue;
                }

                Logger::logInfo("Video frame copy created successfully");

                {
                    std::unique_lock<std::mutex> lock(mutex);
                    frameQueue.push(frame_copy);
                    condition.notify_one();
                    Logger::logInfo("Video frame " + std::to_string(video_frame_count++) + " queued");
                }
            }
        }
        // Traitement des paquets audio
        else if (packet->stream_index == audioStreamIndex && audioCodecContext && audioManager) {
            Logger::logInfo("Processing audio packet - size: " + std::to_string(packet->size) + 
                          ", pts: " + std::to_string(packet->pts));
            
            ret = avcodec_send_packet(audioCodecContext, packet);
            if (ret < 0) {
                Logger::logError("Error sending audio packet: " + std::to_string(ret));
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audioCodecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    Logger::logError("Error receiving audio frame: " + std::to_string(ret));
                    break;
                }

                Logger::logInfo("Cloning audio frame " + std::to_string(audio_frame_count));
                AVFrame* frame_copy = av_frame_clone(frame);
                if (!frame_copy) {
                    Logger::logError("Failed to clone audio frame");
                    continue;
                }

                // Gérer le PTS négatif
                if (frame_copy->pts < 0 || frame_copy->pts == AV_NOPTS_VALUE) {
                    frame_copy->pts = audio_frame_count * frame_copy->nb_samples;
                    Logger::logInfo("Corrected audio PTS: " + std::to_string(frame_copy->pts));
                }

                Logger::logInfo("Pushing audio frame " + std::to_string(audio_frame_count));
                audioManager->pushFrame(frame_copy);
                Logger::logInfo("Audio frame " + std::to_string(audio_frame_count++) + " pushed");
            }
        }

        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    Logger::logInfo("Decode thread terminated");
}