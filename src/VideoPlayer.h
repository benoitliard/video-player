#pragma once

#include <string>
#include <SDL2/SDL.h>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <SDL2/SDL_audio.h>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/pixfmt.h>
    #include <libavutil/pixdesc.h>
}

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool initialize(const std::string& videoPath);
    void run();
    void cleanup();
    void stop();

private:
    bool initializeSDL();
    bool openVideoFile(const std::string& videoPath);
    void processFrame();

    // SDL components
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    // FFmpeg components
    AVFormatContext* formatContext;
    AVCodecContext* codecContext;
    SwsContext* swsContext;

    int videoStreamIndex;
    bool isRunning;

    // Threading components
    std::thread decodeThread;
    std::mutex videoMutex;
    std::condition_variable videoCondition;
    std::queue<AVFrame*> videoFrameQueue;
    std::atomic<bool> isDecodingFinished{false};
    static constexpr size_t MAX_QUEUE_SIZE = 10;
    static constexpr size_t MIN_FRAMES_THRESHOLD = 5;

    struct TimingInfo {
        int64_t start_time;
        double video_timebase;
        double frame_duration;
        int64_t last_pts;
    } timing;

    void decodeThreadFunction();
    void renderFrame(AVFrame* frame);

    struct AudioParams {
        int freq;
        int channels;
        int64_t channel_layout;
        enum AVSampleFormat fmt;
        int frame_size;
        int bytes_per_sec;
    };

    struct AudioState {
        SwrContext *swr_ctx;
        AVStream *stream;
        AVCodecContext *codec_ctx;
        std::queue<AVFrame*> audioQueue;
        std::mutex audioMutex;
        std::condition_variable audioCondition;
        int stream_index;
        double clock;
    } audio;

    SDL_AudioDeviceID audioDeviceId;
    static void audioCallback(void* userdata, Uint8* stream, int len);
    bool initializeAudio();
    void audioThreadFunction();
    std::thread audioThread;
    
    // Timing components
    double audioClock;
    double videoClock;
    double clockDiff;
    static constexpr double AV_SYNC_THRESHOLD = 0.01;
    static constexpr double AV_NOSYNC_THRESHOLD = 10.0;

    int frameCount = 0;
}; 