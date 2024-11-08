#include "VideoPlayer.h"
#include "utils/Logger.h"
#include <signal.h>
#include <algorithm>
#include <thread>

static VideoPlayer* g_player = nullptr;

static void signal_handler(int signum) {
    if (g_player) {
        Logger::logInfo("Received signal " + std::to_string(signum) + ", stopping playback...");
        g_player->stop();
        std::exit(0);
    }
}

VideoPlayer::VideoPlayer() : isRunning(false), isDecodingFinished(false), paused(false), volume(100), shouldReset(false), wsController(this) {
    g_player = this;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

VideoPlayer::~VideoPlayer() {
    stop();
}

bool VideoPlayer::initialize(const std::string& videoPath, uint16_t wsPort) {
    // Initialize SDL first
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        Logger::logError("SDL initialization failed: " + std::string(SDL_GetError()));
        return false;
    }

    if (!decoder.initialize(videoPath)) {
        Logger::logError("Failed to initialize decoder");
        return false;
    }

    if (!renderer.initialize(decoder.getCodecContext()->width, 
                           decoder.getCodecContext()->height)) {
        Logger::logError("Failed to initialize renderer");
        return false;
    }

    // Initialize audio if stream exists
    if (decoder.getAudioStream()) {
        Logger::logInfo("Audio stream found, initializing audio...");
        decoder.setAudioManager(&audioManager);
        if (!audioManager.initialize(decoder.getAudioCodecContext(), decoder.getAudioStream())) {
            Logger::logError("Audio initialization failed");
        } else {
            Logger::logInfo("Audio initialized successfully");
        }
    } else {
        Logger::logInfo("No audio stream found");
    }

    // Initialiser le WebSocketController
    if (!wsController.initialize("0.0.0.0", wsPort)) {
        Logger::logError("Failed to initialize WebSocket controller");
        return false;
    }

    // Démarrer le WebSocketController dans un thread séparé
    std::thread wsThread([this]() {
        wsController.start();
    });
    wsThread.detach();  // Détacher le thread pour qu'il s'exécute en arrière-plan

    isRunning = true;
    decoder.startDecoding();
    
    return true;
}

void VideoPlayer::run() {
    while (isRunning) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    stop();
                    return;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        Logger::logInfo("ESC pressed, stopping playback...");
                        stop();
                        return;
                    }
                    break;
            }
        }

        if (!paused) {
            processFrame();
        } else {
            SDL_Delay(10);  // Éviter d'utiliser trop de CPU en pause
        }

        if (shouldReset.exchange(false)) {
            decoder.seekToStart();
        }
    }
}

void VideoPlayer::processFrame() {
    AVFrame* frame = decoder.getNextFrame();
    if (frame) {
        renderer.renderFrame(frame);
        av_frame_free(&frame);
    } else {
        SDL_Delay(1);  // Avoid busy waiting
    }
}

void VideoPlayer::stop() {
    isRunning = false;
    decoder.stopDecoding();
    audioManager.stop();
    wsController.stop();  // Arrêter le WebSocketController
}

void VideoPlayer::play() {
    paused = false;
}

void VideoPlayer::pause() {
    paused = true;
}

void VideoPlayer::reset() {
    shouldReset = true;
}

void VideoPlayer::setVolume(int vol) {
    volume = std::min(100, std::max(0, vol));
    // Appliquer le volume à l'audio
    if (audioManager.isInitialized()) {
        audioManager.setVolume(volume / 100.0f);
    }
}