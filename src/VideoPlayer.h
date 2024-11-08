#pragma once
#include "core/AudioManager.h"
#include "core/VideoDecoder.h"
#include "core/Renderer.h"
#include "core/WebSocketController.h"
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool initialize(const std::string& videoPath, uint16_t wsPort = 9002);
    void run();
    void stop();
    void play();
    void pause();
    void reset();
    void setVolume(int volume);
    bool isPaused() const { return paused; }

private:
    AudioManager audioManager;
    VideoDecoder decoder;
    Renderer renderer;
    WebSocketController wsController;
    
    bool isRunning;
    bool isDecodingFinished;
    
    std::thread decodeThread;
    std::mutex videoMutex;
    std::condition_variable videoCondition;
    std::queue<AVFrame*> videoFrameQueue;
    
    struct AudioState {
        int stream_index;
        AVCodecContext* codec_ctx;
        SDL_AudioDeviceID deviceId;
    } audio;
    
    bool initializeSDL();
    bool openVideoFile(const std::string& videoPath);
    bool initializeAudio();
    void processFrame();
    void renderFrame(AVFrame* frame);
    void decodeThreadFunction();
    void cleanup();
    static void audioCallback(void* userdata, Uint8* stream, int len);
    
    static constexpr size_t MAX_QUEUE_SIZE = 10;
    
    bool paused;
    int volume;
    std::atomic<bool> shouldReset;
}; 