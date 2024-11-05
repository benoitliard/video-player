#pragma once
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
}

class AudioManager;  // Forward declaration

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool initialize(const std::string& path);
    void startDecoding();
    void stopDecoding();
    AVFrame* getNextFrame();
    
    AVCodecContext* getCodecContext() const { return codecContext; }
    AVStream* getVideoStream() const;
    AVStream* getAudioStream() const;
    AVCodecContext* getAudioCodecContext() const { return audioCodecContext; }
    void setAudioManager(AudioManager* am) { audioManager = am; }

    void seekToStart() {
        if (formatContext) {
            av_seek_frame(formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codecContext);
            if (audioCodecContext) {
                avcodec_flush_buffers(audioCodecContext);
            }
        }
    }

private:
    void decodeThreadFunction();
    
    AVFormatContext* formatContext;
    AVCodecContext* codecContext;
    AVCodecContext* audioCodecContext;
    int videoStreamIndex;
    int audioStreamIndex;
    AudioManager* audioManager;
    
    std::thread decodeThread;
    std::queue<AVFrame*> frameQueue;
    std::mutex mutex;
    std::condition_variable condition;
    bool isRunning;
    
    static constexpr size_t MAX_QUEUE_SIZE = 10;
}; 