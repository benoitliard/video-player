#include "AudioManager.h"
#include "../utils/Logger.h"

AudioManager::AudioManager() : deviceId(0), volume(1.0f), initialized(false) {
    state.swr_ctx = nullptr;
    state.stream = nullptr;
    state.codec_ctx = nullptr;
    state.stream_index = -1;
    state.clock = 0.0;
}

AudioManager::~AudioManager() {
    cleanup();
}

bool AudioManager::initialize(AVCodecContext* codecContext, AVStream* stream) {
    state.codec_ctx = codecContext;
    state.stream = stream;

    Logger::logInfo("Initializing audio with sample rate: " + std::to_string(codecContext->sample_rate) + 
                   " Hz, channels: " + std::to_string(codecContext->ch_layout.nb_channels));

    SDL_AudioSpec wanted_spec, spec;
    wanted_spec.freq = codecContext->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codecContext->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;
    wanted_spec.callback = audioCallback;
    wanted_spec.userdata = this;

    deviceId = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, 0);
    if (deviceId == 0) {
        Logger::logError("Failed to open audio device: " + std::string(SDL_GetError()));
        return false;
    }

    Logger::logInfo("Audio device opened with freq: " + std::to_string(spec.freq) + 
                   " Hz, channels: " + std::to_string(spec.channels));

    // Initialize resampler
    state.swr_ctx = swr_alloc();
    if (!state.swr_ctx) {
        Logger::logError("Failed to allocate resampler context");
        return false;
    }

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_ch_layout = codecContext->ch_layout;

    int ret = swr_alloc_set_opts2(&state.swr_ctx,
        &out_ch_layout,
        AV_SAMPLE_FMT_S16,
        spec.freq,
        &in_ch_layout,
        codecContext->sample_fmt,
        codecContext->sample_rate,
        0,
        nullptr
    );

    if (ret < 0) {
        Logger::logError("Failed to set resampler options");
        return false;
    }

    if (swr_init(state.swr_ctx) < 0) {
        Logger::logError("Failed to initialize resampler");
        return false;
    }

    Logger::logInfo("Audio resampler initialized");
    initialized = true;
    SDL_PauseAudioDevice(deviceId, 0);
    Logger::logInfo("Audio playback started");
    return true;
}

void AudioManager::pushFrame(AVFrame* frame) {
    if (!initialized) return;

    std::unique_lock<std::mutex> lock(state.audioMutex);
    state.audioQueue.push(frame);
    lock.unlock();
    state.audioCondition.notify_one();
}

void AudioManager::audioCallback(void* userdata, Uint8* stream, int len) {
    AudioManager* audio = static_cast<AudioManager*>(userdata);
    std::unique_lock<std::mutex> lock(audio->state.audioMutex);

    memset(stream, 0, len);

    if (audio->state.audioQueue.empty()) {
        return;
    }

    AVFrame* frame = audio->state.audioQueue.front();
    if (!frame) {
        audio->state.audioQueue.pop();
        return;
    }

    // Calculer le nombre d'échantillons à convertir
    int out_samples = av_rescale_rnd(
        swr_get_delay(audio->state.swr_ctx, frame->sample_rate) + frame->nb_samples,
        frame->sample_rate,
        frame->sample_rate,
        AV_ROUND_UP
    );

    // Allouer le buffer temporaire
    uint8_t* buffer = nullptr;
    int buffer_size = av_samples_get_buffer_size(
        nullptr,
        2,  // Toujours en stéréo
        out_samples,
        AV_SAMPLE_FMT_S16,
        0
    );

    if (buffer_size < 0) {
        audio->state.audioQueue.pop();
        av_frame_free(&frame);
        return;
    }

    buffer = reinterpret_cast<uint8_t*>(av_malloc(buffer_size));
    if (!buffer) {
        audio->state.audioQueue.pop();
        av_frame_free(&frame);
        return;
    }

    // Convertir l'audio
    int samples_converted = swr_convert(
        audio->state.swr_ctx,
        &buffer,
        out_samples,
        (const uint8_t**)frame->data,
        frame->nb_samples
    );

    if (samples_converted > 0) {
        int actual_buffer_size = samples_converted * 2 * sizeof(int16_t);
        SDL_MixAudioFormat(
            stream,
            buffer,
            AUDIO_S16SYS,
            std::min(len, actual_buffer_size),
            static_cast<int>(SDL_MIX_MAXVOLUME * audio->volume)
        );

        // Mettre à jour l'horloge audio
        if (frame->pts != AV_NOPTS_VALUE) {
            audio->state.clock = frame->pts * av_q2d(audio->state.stream->time_base);
        }
    }

    av_freep(&buffer);
    audio->state.audioQueue.pop();
    av_frame_free(&frame);
}

void AudioManager::cleanup() {
    if (deviceId) {
        SDL_PauseAudioDevice(deviceId, 1);
        SDL_CloseAudioDevice(deviceId);
        deviceId = 0;
    }

    if (state.swr_ctx) {
        swr_free(&state.swr_ctx);
        state.swr_ctx = nullptr;
    }

    std::unique_lock<std::mutex> lock(state.audioMutex);
    while (!state.audioQueue.empty()) {
        AVFrame* frame = state.audioQueue.front();
        state.audioQueue.pop();
        av_frame_free(&frame);
    }
}

void AudioManager::stop() {
    cleanup();
    initialized = false;
}

// Ajouter cette méthode pour obtenir l'horloge audio
double AudioManager::getAudioClock() const {
    return state.clock;
}