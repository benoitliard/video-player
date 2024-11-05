#include "AudioManager.h"
#include "../utils/Logger.h"

AudioManager::AudioManager() : deviceId(0), isInitialized(false) {
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
    isInitialized = true;
    SDL_PauseAudioDevice(deviceId, 0);
    Logger::logInfo("Audio playback started");
    return true;
}

void AudioManager::pushFrame(AVFrame* frame) {
    if (!isInitialized) return;

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
    audio->state.audioQueue.pop();
    lock.unlock();

    int out_samples = av_rescale_rnd(
        swr_get_delay(audio->state.swr_ctx, frame->sample_rate) + frame->nb_samples,
        frame->sample_rate,
        frame->sample_rate,
        AV_ROUND_UP);

    uint8_t* audio_buf = new uint8_t[len * 2];
    uint8_t* out_buffer[2] = { audio_buf, nullptr };
    int out_linesize;

    int samples_converted = swr_convert(
        audio->state.swr_ctx,
        out_buffer,
        out_samples,
        (const uint8_t**)frame->data,
        frame->nb_samples);

    if (samples_converted > 0) {
        int buffer_size = av_samples_get_buffer_size(
            &out_linesize,
            audio->state.codec_ctx->ch_layout.nb_channels,
            samples_converted,
            AV_SAMPLE_FMT_S16,
            1);

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

    delete[] audio_buf;
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
    isInitialized = false;
}