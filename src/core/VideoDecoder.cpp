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
    const AVCodec* videoCodec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
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
                av_seek_frame(formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecContext);
                if (audioCodecContext) {
                    avcodec_flush_buffers(audioCodecContext);
                }
                continue;
            }
            break;
        }

        if (packet->stream_index == videoStreamIndex) {
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(codecContext, frame);
            if (ret >= 0) {
                AVFrame* frame_copy = av_frame_alloc();
                if (!frame_copy) {
                    av_packet_unref(packet);
                    continue;
                }

                frame_copy->format = frame->format;
                frame_copy->width = frame->width;
                frame_copy->height = frame->height;
                frame_copy->pts = frame->pts;

                ret = av_frame_get_buffer(frame_copy, 32);
                if (ret >= 0) {
                    ret = av_frame_make_writable(frame_copy);
                    if (ret >= 0) {
                        ret = av_frame_copy(frame_copy, frame);
                        if (ret >= 0) {
                            std::unique_lock<std::mutex> lock(mutex);
                            frameQueue.push(frame_copy);
                            condition.notify_one();
                        } else {
                            av_frame_free(&frame_copy);
                        }
                    } else {
                        av_frame_free(&frame_copy);
                    }
                } else {
                    av_frame_free(&frame_copy);
                }
            }
        }
        else if (packet->stream_index == audioStreamIndex && audioCodecContext && audioManager) {
            ret = avcodec_send_packet(audioCodecContext, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(audioCodecContext, frame);
            if (ret >= 0) {
                AVFrame* frame_copy = av_frame_alloc();
                if (!frame_copy) {
                    continue;
                }

                frame_copy->format = frame->format;
                frame_copy->nb_samples = frame->nb_samples;
                frame_copy->sample_rate = frame->sample_rate;
                frame_copy->pts = frame->pts;

                // Utiliser la nouvelle API pour la disposition des canaux
                ret = av_channel_layout_copy(&frame_copy->ch_layout, &frame->ch_layout);
                if (ret < 0) {
                    av_frame_free(&frame_copy);
                    continue;
                }

                ret = av_frame_get_buffer(frame_copy, 0);
                if (ret >= 0) {
                    ret = av_frame_make_writable(frame_copy);
                    if (ret >= 0) {
                        ret = av_frame_copy(frame_copy, frame);
                        if (ret >= 0) {
                            audioManager->pushFrame(frame_copy);
                        } else {
                            av_frame_free(&frame_copy);
                        }
                    } else {
                        av_frame_free(&frame_copy);
                    }
                } else {
                    av_frame_free(&frame_copy);
                }
            }
        }

        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
}