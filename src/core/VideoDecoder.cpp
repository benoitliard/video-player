#include "VideoDecoder.h"
#include "AudioManager.h"
#include "../utils/Logger.h"

VideoDecoder::VideoDecoder()
    : formatContext(nullptr)
    , codecContext(nullptr)
    , videoStreamIndex(-1)
    , audioStreamIndex(-1)
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

    // Initialize video codec
    const AVCodec* videoCodec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!videoCodec) {
        Logger::logError("Unsupported video codec");
        return false;
    }

    codecContext = avcodec_alloc_context3(videoCodec);
    if (!codecContext) {
        Logger::logError("Could not allocate video codec context");
        return false;
    }

    if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        Logger::logError("Could not copy video codec params");
        return false;
    }

    // Set thread count for decoding
    codecContext->thread_count = 4;
    codecContext->thread_type = FF_THREAD_FRAME;

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
    
    while (isRunning) {
        std::unique_lock<std::mutex> lock(mutex);
        if (frameQueue.size() >= MAX_QUEUE_SIZE) {
            condition.wait(lock);
            continue;
        }
        lock.unlock();

        int ret = av_read_frame(formatContext, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                Logger::logInfo("End of file reached, seeking to start...");
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

            while (ret >= 0) {
                AVFrame* frame = av_frame_alloc();
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&frame);
                    break;
                }

                std::unique_lock<std::mutex> lock(mutex);
                frameQueue.push(frame);
                lock.unlock();
                condition.notify_one();
            }
        }
        else if (packet->stream_index == audioStreamIndex && audioCodecContext) {
            ret = avcodec_send_packet(audioCodecContext, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                AVFrame* frame = av_frame_alloc();
                ret = avcodec_receive_frame(audioCodecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&frame);
                    break;
                }

                // Envoyer le frame audio à l'AudioManager
                audioManager->pushFrame(frame);
            }
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
}