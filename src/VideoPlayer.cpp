#include "VideoPlayer.h"
#include "utils/Logger.h"
#include <signal.h>
#include <algorithm>

static VideoPlayer* g_player = nullptr;

static void signal_handler(int signum) {
    if (g_player) {
        Logger::logInfo("Received signal " + std::to_string(signum) + ", stopping playback...");
        g_player->stop();
        std::exit(0);
    }
}

VideoPlayer::VideoPlayer() : isRunning(false), isDecodingFinished(false) {
    g_player = this;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

VideoPlayer::~VideoPlayer() {
    stop();
}

bool VideoPlayer::initialize(const std::string& videoPath) {
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

    isRunning = true;
    decoder.startDecoding();
    
    return true;
}

void VideoPlayer::run() {
    while (isRunning) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                stop();
                return;
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