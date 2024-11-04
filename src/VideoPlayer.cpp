#include "VideoPlayer.h"
#include <iostream>
#include <uWS/App.h>
#ifdef __APPLE__
    #include <sys/sysctl.h>
#endif
#include <fstream>
#include <chrono>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

VideoPlayer::VideoPlayer() 
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , formatContext(nullptr)
    , codecContext(nullptr)
    , frame(nullptr)
    , packet(nullptr)
    , swsContext(nullptr)
    , videoStreamIndex(-1)
    , isRunning(false)
    , audioStreamIndex(-1)
    , audioCodecContext(nullptr)
    , audioDevice(0)
    , audioFrame(nullptr)
    , audioBuffer(nullptr)
    , audioBufferSize(0)
    , audioBufferIndex(0)
    , swrContext(nullptr)
    , audioConvertedData(nullptr)
    , audioConvertedSize(0)
    , videoPts(0)
    , audioPts(0)
    , videoTimeBase(0)
    , audioTimeBase(0)
    , audioDeviceBufferSize(0) {
    
    // Allouer audioFrame ici
    audioFrame = av_frame_alloc();

    metrics = {
        std::chrono::steady_clock::now(),
        std::queue<double>(),
        0,
        0,
        0.0,
        0.0,
        0,
        0.0
    };
}

VideoPlayer::~VideoPlayer() {
    cleanup();
}

bool VideoPlayer::initialize(const std::string& videoPath, int websocketPort) {
    if (!initializeSDL()) {
        return false;
    }

    if (!openVideoFile(videoPath)) {
        return false;
    }

    if (!setupWebSocket(websocketPort)) {
        return false;
    }

    isRunning = true;
    return true;
}

bool VideoPlayer::initializeSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow(
        "Video Player",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        1280,
        720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

bool VideoPlayer::openVideoFile(const std::string& videoPath) {
    formatContext = avformat_alloc_context();
    if (avformat_open_input(&formatContext, videoPath.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "Could not open video file" << std::endl;
        return false;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return false;
    }

    // Trouver le flux vidéo
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

    // Trouver le flux audio
    audioStreamIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            break;
        }
    }

    if (audioStreamIndex != -1) {
        // Configuration du codec audio
        const AVCodec* audioCodec = avcodec_find_decoder(formatContext->streams[audioStreamIndex]->codecpar->codec_id);
        audioCodecContext = avcodec_alloc_context3(audioCodec);
        avcodec_parameters_to_context(audioCodecContext, formatContext->streams[audioStreamIndex]->codecpar);
        
        if (avcodec_open2(audioCodecContext, audioCodec, nullptr) < 0) {
            std::cerr << "Could not open audio codec" << std::endl;
            return false;
        }

        // Initialiser le resampler audio
        swrContext = swr_alloc();
        if (!swrContext) {
            std::cerr << "Could not allocate resampler context" << std::endl;
            return false;
        }

        // Configurer le resampler
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        
        swr_alloc_set_opts2(&swrContext,
            &out_ch_layout,                    // out_ch_layout
            AV_SAMPLE_FMT_S16,                 // out_sample_fmt
            audioCodecContext->sample_rate,     // out_sample_rate
            &audioCodecContext->ch_layout,      // in_ch_layout
            audioCodecContext->sample_fmt,      // in_sample_fmt
            audioCodecContext->sample_rate,     // in_sample_rate
            0,                                  // log_offset
            nullptr                            // log_ctx
        );

        if (!swrContext) {
            std::cerr << "Could not allocate resampler context" << std::endl;
            return false;
        }

        if (swr_init(swrContext) < 0) {
            std::cerr << "Failed to initialize the resampling context" << std::endl;
            return false;
        }

        // Configuration de SDL Audio avec un buffer plus grand
        SDL_AudioSpec wanted_spec, spec;
        wanted_spec.freq = audioCodecContext->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = 2;
        wanted_spec.silence = 0;
        wanted_spec.samples = AUDIO_BUFFER_SIZE;  // Utiliser une plus grande taille
        wanted_spec.callback = nullptr;

        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);
        if (audioDevice == 0) {
            std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
            return false;
        }

        // Stocker les timebase pour la synchronisation
        videoTimeBase = av_q2d(formatContext->streams[videoStreamIndex]->time_base);
        audioTimeBase = av_q2d(formatContext->streams[audioStreamIndex]->time_base);
        
        audioDeviceBufferSize = spec.size;
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    // Configuration du codec vidéo avec plus de vérifications
    const AVCodec* codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec not found" << std::endl;
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        std::cerr << "Could not copy codec params" << std::endl;
        return false;
    }

    // Configuration spécifique pour la plateforme
    #ifdef __arm__
        // Configuration optimisée pour RPi 5
        if (codec->id == AV_CODEC_ID_HEVC) {
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "strict", "-2", 0);
            av_dict_set(&opts, "threads", "4", 0);  // RPi 5 a 4 cœurs
            av_dict_set(&opts, "hwaccel", "v4l2", 0);
            
            // Activer l'accélération matérielle pour H.265
            if (!initializeHardwareDecoding()) {
                std::cerr << "Warning: Hardware acceleration not available for H.265" << std::endl;
            }
            
            int ret = avcodec_open2(codecContext, codec, &opts);
            av_dict_free(&opts);
            
            if (ret < 0) {
                std::cerr << "Could not open HEVC codec with hardware acceleration" << std::endl;
                return false;
            }
        } else if (codec->id == AV_CODEC_ID_H264) {
            std::cout << "Warning: H.264 will use software decoding on RPi 5" << std::endl;
            // Configuration pour décodage logiciel H.264
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "threads", "4", 0);
            int ret = avcodec_open2(codecContext, codec, &opts);
            av_dict_free(&opts);
            if (ret < 0) {
                std::cerr << "Could not open H.264 codec" << std::endl;
                return false;
            }
        }
    #else
        // Configuration pour macOS
        // ... (code existant)
    #endif

    // Supprimer les configurations précédentes du scaler
    if (swsContext) {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

    // Configuration unique du scaler
    swsContext = sws_getContext(
        codecContext->width,
        codecContext->height,
        codecContext->pix_fmt,
        codecContext->width,
        codecContext->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,  // Utiliser uniquement SWS_BILINEAR
        nullptr,
        nullptr,
        nullptr
    );

    if (!swsContext) {
        std::cerr << "Could not initialize the conversion context" << std::endl;
        return false;
    }

    // Créer la texture SDL avec la bonne taille
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        codecContext->width,
        codecContext->height
    );

    if (!texture) {
        std::cerr << "Could not create SDL texture: " << SDL_GetError() << std::endl;
        return false;
    }

    // Allouer les frames
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
        std::cerr << "Could not allocate frame or packet" << std::endl;
        return false;
    }

    // Démarrer les threads
    isDecodingFinished = false;
    try {
        decodeThread = std::thread(&VideoPlayer::decodeThreadFunction, this);
        if (audioStreamIndex != -1) {
            audioThread = std::thread(&VideoPlayer::audioThreadFunction, this);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to start threads: " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool VideoPlayer::setupWebSocket(int port) {
    // Configuration basique du WebSocket
    uWS::App().ws<WebSocketData>("/*", {
        .open = [](auto* ws) {
            ws->getUserData()->isPlaying = false;
            ws->getUserData()->currentFrame = 0;
            std::cout << "Client connected" << std::endl;
        },
        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            ws->send(message, opCode);
        }
    }).listen(port, [port](auto* listen_socket) {
        if (listen_socket) {
            std::cout << "Listening on port " << port << std::endl;
        }
    });

    return true;
}

void VideoPlayer::run() {
    while (isRunning) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                isRunning = false;
            }
        }

        processFrame();
    }
}

void VideoPlayer::processFrame() {
    auto frameStart = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(videoMutex);
    if (videoFrameQueue.empty()) {
        lock.unlock();
        SDL_Delay(1);  // Ajouter un petit délai si pas de frame
        return;
    }

    AVFrame* frame = videoFrameQueue.front();
    videoFrameQueue.pop();
    lock.unlock();
    videoCondition.notify_one();

    if (!frame) {
        std::cerr << "Null frame received" << std::endl;
        return;
    }

    // Synchronisation avec l'audio
    if (audioStreamIndex != -1) {
        double pts = frame->best_effort_timestamp * videoTimeBase;
        double audioTime = audioPts * audioTimeBase;
        double diff = pts - audioTime;

        // Si la vidéo est en avance, attendre
        if (diff > 0) {
            SDL_Delay(static_cast<Uint32>(diff * 1000));
        }
    } else {
        // Si pas d'audio, maintenir un framerate constant
        SDL_Delay(1000/30);  // 30 FPS
    }

    renderFrame(frame);
    
    // Mesurer le temps de traitement
    auto frameEnd = std::chrono::steady_clock::now();
    auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        frameEnd - frameStart).count();

    // Mettre à jour les PTS
    videoPts = frame->best_effort_timestamp;
    
    updateMetrics();
    printMetrics();

    if (processingTime > MAX_FRAME_PROCESSING_TIME) {
        std::cout << "Frame processing took " << processingTime << "ms (too long)" << std::endl;
        metrics.droppedFrames++;
    }

    av_frame_free(&frame);
}

void VideoPlayer::renderFrame(AVFrame* frame) {
    if (!frame || !frame->data[0]) {
        return;
    }

    try {
        // Allouer les buffers YUV
        int ySize = frame->width * frame->height;
        int uvSize = (frame->width * frame->height) / 4;
        
        std::vector<uint8_t> yPlane(ySize);
        std::vector<uint8_t> uPlane(uvSize);
        std::vector<uint8_t> vPlane(uvSize);
        
        uint8_t* planes[3] = {yPlane.data(), uPlane.data(), vPlane.data()};
        int strides[3] = {
            frame->width,
            frame->width / 2,
            frame->width / 2
        };

        // Conversion d'échelle
        sws_scale(swsContext,
            frame->data, frame->linesize, 0, frame->height,
            planes, strides
        );

        // Mise à jour de la texture
        SDL_UpdateYUVTexture(
            texture,
            nullptr,
            planes[0], strides[0],
            planes[1], strides[1],
            planes[2], strides[2]
        );

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

    } catch (const std::exception& e) {
        std::cerr << "Error in renderFrame: " << e.what() << std::endl;
    }
}

void VideoPlayer::cleanup() {
    // Arrêter les threads proprement
    isDecodingFinished = true;
    if (decodeThread.joinable()) decodeThread.join();
    if (audioThread.joinable()) audioThread.join();

    // Nettoyer les queues
    while (!videoFrameQueue.empty()) {
        av_frame_free(&videoFrameQueue.front());
        videoFrameQueue.pop();
    }

    if (hwFrame) {
        av_frame_free(&hwFrame);
    }
    if (hwDeviceCtx) {
        av_buffer_unref(&hwDeviceCtx);
    }

    if (audioDevice) {
        SDL_PauseAudioDevice(audioDevice, 1);
        SDL_CloseAudioDevice(audioDevice);
        audioDevice = 0;
    }

    if (swrContext) {
        swr_free(&swrContext);
    }

    if (audioConvertedData) {
        if (audioConvertedData[0]) {
            av_freep(&audioConvertedData[0]);
        }
        av_freep(&audioConvertedData);
    }

    if (audioFrame) {
        av_frame_free(&audioFrame);
    }

    if (audioCodecContext) {
        avcodec_free_context(&audioCodecContext);
    }

    if (swsContext) {
        sws_freeContext(swsContext);
    }

    if (frame) {
        av_frame_free(&frame);
    }

    if (packet) {
        av_packet_unref(packet);
        av_packet_free(&packet);
    }

    if (codecContext) {
        avcodec_free_context(&codecContext);
    }

    if (formatContext) {
        avformat_close_input(&formatContext);
    }

    if (texture) {
        SDL_DestroyTexture(texture);
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }

    if (window) {
        SDL_DestroyWindow(window);
    }

    SDL_Quit();
}

void VideoPlayer::updateMetrics() {
    auto now = std::chrono::steady_clock::now();
    auto frameTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - metrics.lastFrameTime).count() / 1000.0;
    
    metrics.frameTimes.push(frameTime);
    if (metrics.frameTimes.size() > METRICS_WINDOW) {
        metrics.frameTimes.pop();
    }

    // Calculer FPS moyen
    double totalTime = 0;
    std::queue<double> tempQueue = metrics.frameTimes;
    while (!tempQueue.empty()) {
        totalTime += tempQueue.front();
        tempQueue.pop();
    }
    metrics.avgFPS = metrics.frameTimes.size() / totalTime;

    // Mesurer la taille de la file audio
    metrics.audioQueueSize = SDL_GetQueuedAudioSize(audioDevice);
    metrics.audioLatency = static_cast<double>(metrics.audioQueueSize) / 
        (audioCodecContext->sample_rate * 4); // 4 = 2 channels * 2 bytes per sample

    metrics.lastFrameTime = now;
    metrics.frameCount++;
}

void VideoPlayer::printMetrics() {
    if (metrics.frameCount % METRICS_PRINT_INTERVAL == 0) {
        std::cout << "\033[2J\033[1;1H";  // Effacer l'écran
        std::cout << "=== Performance Metrics ===" << std::endl;
        std::cout << "FPS: " << metrics.avgFPS << std::endl;
        std::cout << "Dropped Frames: " << metrics.droppedFrames << std::endl;
        std::cout << "Audio Queue Size: " << metrics.audioQueueSize << " bytes" << std::endl;
        std::cout << "Audio Latency: " << metrics.audioLatency * 1000 << " ms" << std::endl;
        std::cout << "Video PTS: " << videoPts << std::endl;
        std::cout << "Audio PTS: " << audioPts << std::endl;
        std::cout << "PTS Diff: " << videoPts - audioPts << std::endl;
        
        // Informations sur le codec
        std::cout << "\n=== Codec Info ===" << std::endl;
        std::cout << "Video Codec: " << avcodec_get_name(codecContext->codec_id) << std::endl;
        std::cout << "Resolution: " << codecContext->width << "x" << codecContext->height << std::endl;
        std::cout << "Pixel Format: " << static_cast<int>(codecContext->pix_fmt) << std::endl;  // Afficher l'enum directement
        if (audioCodecContext) {
            std::cout << "Audio Codec: " << avcodec_get_name(audioCodecContext->codec_id) << std::endl;
            std::cout << "Sample Rate: " << audioCodecContext->sample_rate << std::endl;
            std::cout << "Channels: " << audioCodecContext->ch_layout.nb_channels << std::endl;
        }
    }
}

// Ajouter cette méthode pour pré-charger les frames
void VideoPlayer::preloadFrames() {
    for (int i = 0; i < 5; i++) {  // Pré-charger 5 frames
        AVPacket* preloadPacket = av_packet_alloc();
        if (av_read_frame(formatContext, preloadPacket) >= 0) {
            if (preloadPacket->stream_index == videoStreamIndex) {
                avcodec_send_packet(codecContext, preloadPacket);
            }
        }
        av_packet_free(&preloadPacket);
    }
}

bool VideoPlayer::initializeHardwareDecoding() {
    // Pour RPi 5, utiliser V4L2 au lieu de VideoToolbox
    #ifdef __arm__
        int err = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_V4L2,
                                      nullptr, nullptr, 0);
    #else
        // Pour macOS, utiliser VideoToolbox
        int err = av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                      nullptr, nullptr, 0);
    #endif

    if (err < 0) {
        std::cerr << "Hardware acceleration not available" << std::endl;
        return false;
    }

    codecContext->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
    return true;
}

void VideoPlayer::decodeThreadFunction() {
    AVPacket* threadPacket = av_packet_alloc();
    if (!threadPacket) {
        std::cerr << "Could not allocate packet in decode thread" << std::endl;
        return;
    }

    while (!isDecodingFinished) {
        if (!formatContext) {
            break;
        }

        int ret = av_read_frame(formatContext, threadPacket);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                break;
            }
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error reading frame: " << errbuf << std::endl;
            SDL_Delay(10);
            continue;
        }

        if (threadPacket->stream_index == videoStreamIndex) {
            std::unique_lock<std::mutex> lock(videoMutex);
            if (videoFrameQueue.size() >= MAX_QUEUE_SIZE) {
                lock.unlock();
                av_packet_unref(threadPacket);
                SDL_Delay(1);
                continue;
            }

            ret = avcodec_send_packet(codecContext, threadPacket);
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                std::cerr << "Error sending packet to decoder: " << errbuf << std::endl;
                lock.unlock();
                av_packet_unref(threadPacket);
                continue;
            }

            while (ret >= 0) {
                AVFrame* newFrame = av_frame_alloc();
                if (!newFrame) {
                    std::cerr << "Could not allocate frame" << std::endl;
                    break;
                }

                ret = avcodec_receive_frame(codecContext, newFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&newFrame);
                    break;
                } else if (ret < 0) {
                    av_frame_free(&newFrame);
                    break;
                }

                videoFrameQueue.push(newFrame);
                videoCondition.notify_one();
            }
        }
        av_packet_unref(threadPacket);
    }

    av_packet_free(&threadPacket);
}

void VideoPlayer::audioThreadFunction() {
    while (!isDecodingFinished) {
        if (!formatContext || !packet) {
            break;
        }

        std::unique_lock<std::mutex> lock(audioMutex);
        if (audioFrameQueue.size() >= AUDIO_QUEUE_SIZE) {
            audioCondition.wait(lock);
            continue;
        }
        lock.unlock();

        int ret = av_read_frame(formatContext, packet);
        if (ret < 0) break;

        if (packet->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(audioCodecContext, packet) >= 0) {
                while (true) {
                    AVFrame* audioFrame = av_frame_alloc();
                    ret = avcodec_receive_frame(audioCodecContext, audioFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        av_frame_free(&audioFrame);
                        break;
                    }

                    // Convertir l'audio et le mettre dans la file SDL
                    int out_samples = av_rescale_rnd(
                        swr_get_delay(swrContext, audioCodecContext->sample_rate) + audioFrame->nb_samples,
                        audioCodecContext->sample_rate,
                        audioCodecContext->sample_rate,
                        AV_ROUND_UP
                    );

                    uint8_t* buffer;
                    av_samples_alloc(&buffer, nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 0);

                    int converted = swr_convert(swrContext, &buffer, out_samples,
                        (const uint8_t**)audioFrame->data, audioFrame->nb_samples);

                    if (converted > 0) {
                        int buffer_size = av_samples_get_buffer_size(nullptr, 2, converted, AV_SAMPLE_FMT_S16, 0);
                        SDL_QueueAudio(audioDevice, buffer, buffer_size);
                        audioPts = audioFrame->best_effort_timestamp;
                    }

                    av_freep(&buffer);
                    av_frame_free(&audioFrame);
                }
            }
        }
        av_packet_unref(packet);
    }
}

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

void VideoPlayer::collectSystemInfo() {
    #ifdef __arm__
        sysInfo.platform = "Raspberry Pi 5";
        // Lecture des infos CPU pour RPi
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos) {
                sysInfo.cpuInfo = line.substr(line.find(":") + 2);
                break;
            }
        }
    #else
        sysInfo.platform = "macOS";
        // Lecture des infos CPU pour macOS
        #ifdef __APPLE__
            size_t len = 100;
            char cpuModel[100];
            sysctlbyname("machdep.cpu.brand_string", &cpuModel, &len, NULL, 0);
            sysInfo.cpuInfo = cpuModel;
        #else
            sysInfo.cpuInfo = "Unknown CPU";
        #endif
    #endif

    sysInfo.cpuCores = std::thread::hardware_concurrency();
    sysInfo.hasHardwareAccel = hwDeviceCtx != nullptr;
}

void VideoPlayer::printDetailedMetrics() {
    static int frameCount = 0;
    frameCount++;

    if (frameCount % 300 == 0) { // Toutes les 300 frames
        std::cout << "\n=== Detailed Performance Report ===" << std::endl;
        std::cout << "Platform: " << sysInfo.platform << std::endl;
        std::cout << "CPU: " << sysInfo.cpuInfo << std::endl;
        std::cout << "CPU Cores: " << sysInfo.cpuCores << std::endl;
        std::cout << "Hardware Acceleration: " << (sysInfo.hasHardwareAccel ? "Yes" : "No") << std::endl;
        
        std::cout << "\nVideo Stats:" << std::endl;
        std::cout << "Resolution: " << codecContext->width << "x" << codecContext->height << std::endl;
        std::cout << "Codec: " << avcodec_get_name(codecContext->codec_id) << std::endl;
        std::cout << "Average FPS: " << metrics.avgFPS << std::endl;
        std::cout << "Frame Processing Time (avg): " << metrics.avgProcessingTime << "ms" << std::endl;
        std::cout << "Dropped Frames: " << metrics.droppedFrames << std::endl;
        
        std::cout << "\nMemory Usage:" << std::endl;
        std::cout << "Video Queue Size: " << videoFrameQueue.size() << "/" << MAX_QUEUE_SIZE << std::endl;
        std::cout << "Audio Queue Size: " << metrics.audioQueueSize << " bytes" << std::endl;
        
        std::cout << "\nSync Stats:" << std::endl;
        std::cout << "Audio Latency: " << metrics.audioLatency * 1000 << "ms" << std::endl;
        std::cout << "A/V Sync Offset: " << (videoPts - audioPts) * videoTimeBase * 1000 << "ms" << std::endl;
        
        std::cout << "================================\n" << std::endl;
    }
}