#include "Renderer.h"
#include "../utils/Logger.h"

Renderer::Renderer() 
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , swsContext(nullptr)
    , yPlane(nullptr)
    , uPlane(nullptr)
    , vPlane(nullptr) {
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::initialize(int width, int height) {
    // Force KMSDRM driver for hardware acceleration
    if (!SDL_SetHint(SDL_HINT_RENDER_DRIVER, "KMSDRM")) {
        Logger::logError("Failed to set KMSDRM hint");
    }
    if (!SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1")) {
        Logger::logError("Failed to set VSYNC hint");
    }
    if (!SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1")) {
        Logger::logError("Failed to set double buffer hint");
    }

    window = SDL_CreateWindow(
        "Video Player",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        1920, 1080,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        Logger::logError("Window creation failed: " + std::string(SDL_GetError()));
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, 
        SDL_RENDERER_ACCELERATED | 
        SDL_RENDERER_PRESENTVSYNC | 
        SDL_RENDERER_TARGETTEXTURE
    );

    if (!renderer) {
        Logger::logError("Renderer creation failed: " + std::string(SDL_GetError()));
        return false;
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );

    if (!texture) {
        Logger::logError("Texture creation failed: " + std::string(SDL_GetError()));
        return false;
    }

    // Allouer les buffers YUV
    yPlane = new uint8_t[width * height];
    uPlane = new uint8_t[width * height / 4];
    vPlane = new uint8_t[width * height / 4];

    // Initialize scaler
    swsContext = sws_getContext(
        width, height, AV_PIX_FMT_YUV420P,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsContext) {
        Logger::logError("Failed to initialize scaler");
        return false;
    }

    SDL_RenderSetLogicalSize(renderer, width, height);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

    return true;
}

void Renderer::renderFrame(AVFrame* frame) {
    if (!frame) return;

    uint8_t* planes[3] = {yPlane, uPlane, vPlane};
    int strides[3] = {frame->width, frame->width/2, frame->width/2};

    sws_scale(swsContext,
              frame->data, frame->linesize, 0, frame->height,
              planes, strides);

    SDL_UpdateYUVTexture(
        texture,
        nullptr,
        yPlane, strides[0],
        uPlane, strides[1],
        vPlane, strides[2]
    );

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void Renderer::cleanup() {
    delete[] yPlane;
    delete[] uPlane;
    delete[] vPlane;

    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }
}