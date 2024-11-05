#include "VideoPlayer.h"
#include <iostream>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <signal.h>

static VideoPlayer* g_player = nullptr;

static void signal_handler(int signum) {
    if (g_player) {
        std::cout << "\nReceived signal " << signum << ", stopping playback..." << std::endl;
        g_player->stop();
        std::exit(0);
    }
}

VideoPlayer::VideoPlayer() 
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , formatContext(nullptr)
    , codecContext(nullptr)
    , swsContext(nullptr)
    , videoStreamIndex(-1)
    , isRunning(false) {
    g_player = this;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

VideoPlayer::~VideoPlayer() {
    cleanup();
}

bool VideoPlayer::initialize(const std::string& videoPath) {
    if (!initializeSDL()) {
        return false;
    }

    if (!openVideoFile(videoPath)) {
        return false;
    }

    if (!initializeAudio()) {
        std::cerr << "Warning: Audio initialization failed" << std::endl;
        // Continue anyway, just without audio
    }

    isRunning = true;
    isDecodingFinished = false;
    
    decodeThread = std::thread(&VideoPlayer::decodeThreadFunction, this);
    
    return true;
}

bool VideoPlayer::initializeSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Force KMSDRM driver for hardware acceleration
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "KMSDRM");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");

    // Ajouter ces hints SDL
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    
    // Pour le Raspberry Pi
    SDL_SetHint(SDL_HINT_RPI_VIDEO_LAYER, "1");

    window = SDL_CreateWindow(
        "Video Player",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        1920,
        1080,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, 
        SDL_RENDERER_ACCELERATED | 
        SDL_RENDERER_PRESENTVSYNC | 
        SDL_RENDERER_TARGETTEXTURE
    );

    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Optimize renderer
    SDL_RenderSetLogicalSize(renderer, 1920, 1080);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

    return true;
}

bool VideoPlayer::openVideoFile(const std::string& videoPath) {
    formatContext = avformat_alloc_context();
    if (!formatContext) {
        std::cerr << "Could not allocate format context" << std::endl;
        return false;
    }

    if (avformat_open_input(&formatContext, videoPath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open video file" << std::endl;
        return false;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        return false;
    }

    // Find video stream
    videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        return false;
    }

    // Get decoder
    const AVCodec* codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Unsupported codec" << std::endl;
        return false;
    }

    // Allocate codec context
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        std::cerr << "Could not copy codec params" << std::endl;
        return false;
    }

    // Set thread count for decoding
    codecContext->thread_count = 4;
    codecContext->thread_type = FF_THREAD_FRAME;

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return false;
    }

    // Initialize scaling context
    swsContext = sws_getContext(
        codecContext->width,
        codecContext->height,
        codecContext->pix_fmt,
        codecContext->width,
        codecContext->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );

    if (!swsContext) {
        std::cerr << "Could not initialize scaling context" << std::endl;
        return false;
    }

    // Create texture
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        codecContext->width,
        codecContext->height
    );

    if (!texture) {
        std::cerr << "Could not create texture: " << SDL_GetError() << std::endl;
        return false;
    }

    std::cout << "Video dimensions: " << codecContext->width << "x" << codecContext->height << std::endl;
    std::cout << "Input pixel format: " << av_get_pix_fmt_name(codecContext->pix_fmt) << std::endl;
    std::cout << "Pixel format description: " << av_pix_fmt_desc_get(codecContext->pix_fmt)->name << std::endl;

    if (videoStreamIndex != -1) {
        AVStream* video_stream = formatContext->streams[videoStreamIndex];
        double fps = av_q2d(video_stream->avg_frame_rate);
        std::cout << "Video Info:" << std::endl;
        std::cout << "- Codec: " << avcodec_get_name(video_stream->codecpar->codec_id) << std::endl;
        std::cout << "- Resolution: " << codecContext->width << "x" << codecContext->height << std::endl;
        std::cout << "- FPS: " << fps << std::endl;
        std::cout << "- Timebase: " << video_stream->time_base.num << "/" << video_stream->time_base.den << std::endl;
        std::cout << "- Duration: " << formatContext->duration / AV_TIME_BASE << " seconds" << std::endl;
    }

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

        if (!isRunning) {
            break;
        }

        processFrame();
    }

    // Nettoyer après la sortie de la boucle
    cleanup();
}

void VideoPlayer::processFrame() {
    std::unique_lock<std::mutex> lock(videoMutex);
    if (videoFrameQueue.empty()) {
        lock.unlock();
        SDL_Delay(1);
        return;
    }

    AVFrame* frame = videoFrameQueue.front();
    videoFrameQueue.pop();
    lock.unlock();
    videoCondition.notify_one();

    renderFrame(frame);
    av_frame_free(&frame);
}

void VideoPlayer::renderFrame(AVFrame* frame) {
    if (!frame) return;
    
    static int frame_count = 0;
    static int64_t last_time = SDL_GetTicks64();
    frame_count++;

    // Optimisation du rendu : pré-allouer les buffers YUV
    static uint8_t* yPlane = new uint8_t[codecContext->width * codecContext->height];
    static uint8_t* uPlane = new uint8_t[codecContext->width * codecContext->height / 4];
    static uint8_t* vPlane = new uint8_t[codecContext->width * codecContext->height / 4];
    
    uint8_t* planes[3] = {yPlane, uPlane, vPlane};
    int strides[3] = {codecContext->width, codecContext->width/2, codecContext->width/2};

    // Conversion et rendu
    sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height, planes, strides);

    SDL_UpdateYUVTexture(texture, nullptr, yPlane, strides[0], uPlane, strides[1], vPlane, strides[2]);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    // Timing simplifié
    int64_t current_time = SDL_GetTicks64();
    double target_frame_time = 1000.0 / av_q2d(formatContext->streams[videoStreamIndex]->avg_frame_rate);
    double elapsed = current_time - last_time;

    // Logs de performance toutes les 30 frames
    if (frame_count % 30 == 0) {
        double actual_fps = 1000.0 / (elapsed / 30.0);
        std::cout << "\nPlayback stats:" << std::endl;
        std::cout << "- Frame: " << frame_count << std::endl;
        std::cout << "- Target FPS: " << av_q2d(formatContext->streams[videoStreamIndex]->avg_frame_rate) << std::endl;
        std::cout << "- Actual FPS: " << actual_fps << std::endl;
        std::cout << "- Queue size: " << videoFrameQueue.size() << std::endl;
        std::cout << "- Frame time: " << elapsed / 30.0 << "ms" << std::endl;
        last_time = current_time;
    }

    // Délai minimal pour éviter la surcharge CPU
    if (elapsed < target_frame_time) {
        SDL_Delay(1);
    }
}

void VideoPlayer::decodeThreadFunction() {
    AVPacket* packet = av_packet_alloc();
    
    while (!isDecodingFinished) {
        // Si la queue vidéo est pleine, attendre
        std::unique_lock<std::mutex> lock(videoMutex);
        if (videoFrameQueue.size() >= MAX_QUEUE_SIZE) {
            videoCondition.wait(lock);
            continue;
        }
        lock.unlock();

        int ret = av_read_frame(formatContext, packet);
        if (ret < 0) break;

        // Traitement des paquets vidéo
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

                std::unique_lock<std::mutex> lock(videoMutex);
                videoFrameQueue.push(frame);
                frameCount++;
                if (frameCount % 30 == 0) {
                    std::cout << "Decoded " << frameCount << " frames" << std::endl;
                }
                lock.unlock();
                videoCondition.notify_one();
            }
        }
        // Traitement des paquets audio
        else if (packet->stream_index == audio.stream_index) {
            ret = avcodec_send_packet(audio.codec_ctx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                AVFrame* frame = av_frame_alloc();
                ret = avcodec_receive_frame(audio.codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&frame);
                    break;
                }

                std::unique_lock<std::mutex> lock(audio.audioMutex);
                // Limiter la taille de la queue audio
                if (audio.audioQueue.size() < 100) {  // Ajuster selon les besoins
                    audio.audioQueue.push(frame);
                } else {
                    av_frame_free(&frame);
                }
                lock.unlock();
            }
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
}

void VideoPlayer::cleanup() {
    // S'assurer que stop() est appelé d'abord
    stop();

    if (decodeThread.joinable()) {
        decodeThread.join();
    }

    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

    if (audio.swr_ctx) {
        swr_free(&audio.swr_ctx);
    }

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

    if (audio.codec_ctx) {
        avcodec_free_context(&audio.codec_ctx);
    }

    if (codecContext) {
        avcodec_free_context(&codecContext);
    }

    if (formatContext) {
        avformat_close_input(&formatContext);
    }

    SDL_Quit();
}

bool VideoPlayer::initializeAudio() {
    // Trouver le stream audio
    audio.stream_index = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio.stream_index = i;
            audio.stream = formatContext->streams[i];
            break;
        }
    }

    if (audio.stream_index == -1) {
        std::cerr << "Pas de stream audio trouvé" << std::endl;
        return false;
    }

    // Configurer le codec audio
    const AVCodec* codec = avcodec_find_decoder(audio.stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec audio non supporté" << std::endl;
        return false;
    }

    audio.codec_ctx = avcodec_alloc_context3(codec);
    if (!audio.codec_ctx) {
        std::cerr << "Impossible d'allouer le contexte codec audio" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(audio.codec_ctx, audio.stream->codecpar) < 0) {
        std::cerr << "Impossible de copier les paramètres audio" << std::endl;
        return false;
    }

    if (avcodec_open2(audio.codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Impossible d'ouvrir le codec audio" << std::endl;
        return false;
    }

    // Configurer SDL Audio avec une taille de buffer plus appropriée
    SDL_AudioSpec wanted_spec, spec;
    wanted_spec.freq = audio.codec_ctx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = audio.codec_ctx->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;  // Réduire la taille du buffer
    wanted_spec.callback = audioCallback;
    wanted_spec.userdata = this;

    audioDeviceId = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);
    if (audioDeviceId == 0) {
        std::cerr << "Impossible d'ouvrir le périphérique audio: " << SDL_GetError() << std::endl;
        return false;
    }

    // Initialiser le resampler
    audio.swr_ctx = swr_alloc();
    if (!audio.swr_ctx) {
        return false;
    }

    // Configurer les paramètres du resampler
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_ch_layout = audio.codec_ctx->ch_layout;

    int ret = swr_alloc_set_opts2(&audio.swr_ctx,
        &out_ch_layout,                    // out_ch_layout
        AV_SAMPLE_FMT_S16,                 // out_sample_fmt
        spec.freq,                         // out_sample_rate
        &in_ch_layout,                     // in_ch_layout
        audio.codec_ctx->sample_fmt,       // in_sample_fmt
        audio.codec_ctx->sample_rate,      // in_sample_rate
        0,                                 // log_offset
        nullptr                            // log_ctx
    );

    if (ret < 0) {
        std::cerr << "Impossible de configurer le resampler audio" << std::endl;
        return false;
    }

    if (swr_init(audio.swr_ctx) < 0) {
        std::cerr << "Impossible d'initialiser le resampler audio" << std::endl;
        return false;
    }

    std::cout << "\nAudio Info:" << std::endl;
    std::cout << "- Codec: " << avcodec_get_name(audio.stream->codecpar->codec_id) << std::endl;
    std::cout << "- Sample rate: " << audio.codec_ctx->sample_rate << " Hz" << std::endl;
    std::cout << "- Channels: " << audio.codec_ctx->ch_layout.nb_channels << std::endl;
    std::cout << "- Sample format: " << av_get_sample_fmt_name(audio.codec_ctx->sample_fmt) << std::endl;

    // Démarrer l'audio
    SDL_PauseAudioDevice(audioDeviceId, 0);
    std::cout << "Audio device opened and started" << std::endl;
    return true;
}

void VideoPlayer::audioCallback(void* userdata, Uint8* stream, int len) {
    VideoPlayer* player = static_cast<VideoPlayer*>(userdata);
    std::unique_lock<std::mutex> lock(player->audio.audioMutex);

    memset(stream, 0, len);

    if (player->audio.audioQueue.empty()) {
        return;
    }

    AVFrame* frame = player->audio.audioQueue.front();
    player->audio.audioQueue.pop();
    lock.unlock();

    // Calculer la taille du buffer de sortie
    int out_samples = av_rescale_rnd(
        swr_get_delay(player->audio.swr_ctx, frame->sample_rate) + frame->nb_samples,
        frame->sample_rate,
        frame->sample_rate,
        AV_ROUND_UP);

    // Allouer le buffer temporaire
    uint8_t* audio_buf = new uint8_t[len * 2];  // * 2 pour avoir de la marge
    uint8_t* out_buffer[2] = { audio_buf, nullptr };
    int out_linesize;

    // Convertir l'audio
    int samples_converted = swr_convert(
        player->audio.swr_ctx,
        out_buffer,
        out_samples,
        (const uint8_t**)frame->data,
        frame->nb_samples);

    if (samples_converted > 0) {
        // Calculer la taille réelle des données converties
        int buffer_size = av_samples_get_buffer_size(
            &out_linesize,
            player->audio.codec_ctx->ch_layout.nb_channels,
            samples_converted,
            AV_SAMPLE_FMT_S16,
            1);

        // Mixer l'audio dans le stream de sortie
        if (buffer_size > 0) {
            SDL_MixAudioFormat(
                stream,
                audio_buf,
                AUDIO_S16SYS,
                std::min(buffer_size, len),
                SDL_MIX_MAXVOLUME / 2
            );
        }
    }

    // Nettoyer
    delete[] audio_buf;
    av_frame_free(&frame);
}

void VideoPlayer::stop() {
    // Mettre le flag d'arrêt avant tout
    isRunning = false;
    isDecodingFinished = true;

    // Arrêter l'audio immédiatement
    if (audioDeviceId) {
        SDL_PauseAudioDevice(audioDeviceId, 1);
        SDL_CloseAudioDevice(audioDeviceId);
        audioDeviceId = 0;
    }

    // Forcer le réveil des threads en attente
    videoCondition.notify_all();
    audio.audioCondition.notify_all();

    // Attendre que le thread de décodage se termine
    if (decodeThread.joinable()) {
        decodeThread.join();
    }

    // Nettoyer les queues
    {
        std::unique_lock<std::mutex> audioLock(audio.audioMutex);
        while (!audio.audioQueue.empty()) {
            AVFrame* frame = audio.audioQueue.front();
            audio.audioQueue.pop();
            av_frame_free(&frame);
        }
    }

    {
        std::unique_lock<std::mutex> videoLock(videoMutex);
        while (!videoFrameQueue.empty()) {
            AVFrame* frame = videoFrameQueue.front();
            videoFrameQueue.pop();
            av_frame_free(&frame);
        }
    }

    // Forcer la sortie du programme
    std::exit(0);
}

struct PerformanceMetrics {
    double avg_decode_time;
    double avg_render_time;
    double frame_drops;
    double sync_diff;
    int queue_size;
};