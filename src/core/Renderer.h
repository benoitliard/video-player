#pragma once
#include <SDL2/SDL.h>
#include <string>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/pixfmt.h>
}

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool initialize(int width, int height);
    void cleanup();
    void renderFrame(AVFrame* frame);
    
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SwsContext* swsContext;
    
    uint8_t* yPlane;
    uint8_t* uPlane;
    uint8_t* vPlane;
}; 