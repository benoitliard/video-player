#pragma once
#include <SDL2/SDL.h>
#include <queue>
#include <mutex>
#include <condition_variable>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
}

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool initialize(AVCodecContext* codecContext, AVStream* stream);
    void cleanup();
    void stop();
    
    static void audioCallback(void* userdata, Uint8* stream, int len);
    void pushFrame(AVFrame* frame);
    double getAudioClock() const;

private:
    struct AudioState {
        SwrContext *swr_ctx;
        AVStream *stream;
        AVCodecContext *codec_ctx;
        std::queue<AVFrame*> audioQueue;
        std::mutex audioMutex;
        std::condition_variable audioCondition;
        int stream_index;
        double clock;
    } state;

    SDL_AudioDeviceID deviceId;
    bool isInitialized;
}; 