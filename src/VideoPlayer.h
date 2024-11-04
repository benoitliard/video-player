#pragma once

#include <string>
#include <SDL2/SDL.h>
#include <chrono>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
    #include <libavutil/opt.h>
    #include <libavutil/pixfmt.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/channel_layout.h>
}

// Structure pour les données WebSocket
struct WebSocketData {
    bool isPlaying;
    int currentFrame;
};

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool initialize(const std::string& videoPath, int websocketPort);
    void run();
    void cleanup();

private:
    bool initializeSDL();
    bool openVideoFile(const std::string& videoPath);
    bool setupWebSocket(int port);
    void processFrame();

    // SDL components
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    // FFmpeg components
    AVFormatContext* formatContext;
    AVCodecContext* codecContext;
    AVFrame* frame;
    AVPacket* packet;
    SwsContext* swsContext;

    int videoStreamIndex;
    bool isRunning;

    // Audio components
    int audioStreamIndex;
    AVCodecContext* audioCodecContext;
    SDL_AudioDeviceID audioDevice;
    AVFrame* audioFrame;
    
    // Audio buffer
    uint8_t* audioBuffer;
    int audioBufferSize;
    int audioBufferIndex;

    // Audio components
    SwrContext* swrContext;  // Ajouter le contexte de resampling
    uint8_t** audioConvertedData; // Buffer pour l'audio converti
    int audioConvertedSize;

    static constexpr int AUDIO_BUFFER_SIZE = 4096; // Taille plus raisonnable
    static constexpr int VIDEO_QUEUE_SIZE = 30;      // Taille de la file vidéo
    static constexpr int AUDIO_QUEUE_SIZE = 50;      // Taille de la file audio

    // Ajouter ces membres pour la synchronisation
    int64_t videoPts;
    int64_t audioPts;
    double videoTimeBase;
    double audioTimeBase;
    uint32_t audioDeviceBufferSize;

    // Ajouter ces membres pour les métriques
    struct PerformanceMetrics {
        std::chrono::steady_clock::time_point lastFrameTime;
        std::queue<double> frameTimes;
        int frameCount;
        int droppedFrames;
        double avgFPS;
        double avgProcessingTime;
        uint32_t audioQueueSize;
        double audioLatency;
    };
    PerformanceMetrics metrics;
    
    void updateMetrics();
    void printMetrics();
    
    // Constantes pour les métriques
    static constexpr int METRICS_WINDOW = 120;  // Fenêtre de 120 frames pour les moyennes
    static constexpr int METRICS_PRINT_INTERVAL = 60;  // Afficher les métriques toutes les 60 frames

    // Ajouter ces constantes
    static constexpr int MAX_FRAME_PROCESSING_TIME = 33;  // 33ms = ~30fps
    static constexpr int PRELOAD_FRAME_COUNT = 5;
    
    // Ajouter cette méthode
    void preloadFrames();

    // Ajouter ces membres pour le multithreading
    std::thread decodeThread;
    std::thread audioThread;
    std::mutex videoMutex;
    std::mutex audioMutex;
    std::condition_variable videoCondition;
    std::condition_variable audioCondition;
    std::queue<AVFrame*> videoFrameQueue;
    std::queue<AVFrame*> audioFrameQueue;
    static constexpr size_t MAX_QUEUE_SIZE = 30;
    std::atomic<bool> isDecodingFinished{false};

    // Méthodes pour le threading
    void decodeThreadFunction();
    void audioThreadFunction();
    void renderFrame(AVFrame* frame);

    // Accélération matérielle
    AVBufferRef* hwDeviceCtx{nullptr};
    AVFrame* hwFrame{nullptr};
    bool initializeHardwareDecoding();

    struct SystemInfo {
        std::string platform;
        std::string cpuInfo;
        int cpuCores;
        std::string gpuInfo;
        size_t totalMemory;
        bool hasHardwareAccel;
    };
    
    SystemInfo sysInfo;
    void collectSystemInfo();
    void printDetailedMetrics();
}; 