#include "forge/audio/AudioEngine.hpp"

#include <miniaudio.h>  // declaration only — implementation in miniaudio_impl.cpp

#include <algorithm>
#include <iostream>

namespace forge::audio {

// ─── SoundSlot ────────────────────────────────────────────────────────────────

struct AudioEngine::SoundSlot {
    ma_sound sound;
    bool     inUse  = false;
    bool     is3D   = false;

    void reset() {
        if (inUse) { ma_sound_uninit(&sound); inUse = false; }
    }
};

// ─── Lifecycle ────────────────────────────────────────────────────────────────

AudioEngine::AudioEngine()
    : engine_(std::make_unique<ma_engine>())
    , pool_(std::make_unique<SoundSlot[]>(kSoundPoolSize)) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() noexcept {
    ma_engine_config cfg = ma_engine_config_init();
    // Channels: 2 (stereo); sample rate: 0 = device default
    if (ma_engine_init(&cfg, engine_.get()) != MA_SUCCESS) {
        std::cerr << "[forge::audio] Failed to initialise audio engine\n";
        return false;
    }
    initialised_ = true;
    std::cout << "[forge::audio] Audio engine initialised ("
              << ma_engine_get_sample_rate(engine_.get()) << " Hz)\n";
    return true;
}

void AudioEngine::shutdown() noexcept {
    if (!initialised_) return;
    stopAll();
    ma_engine_uninit(engine_.get());
    initialised_ = false;
}

void AudioEngine::setSoundRoot(const std::filesystem::path& root) noexcept {
    soundRoot_ = root;
}

// ─── Listener ────────────────────────────────────────────────────────────────

void AudioEngine::setListenerTransform(glm::vec3 pos,
                                        glm::vec3 fwd,
                                        glm::vec3 up) noexcept
{
    if (!initialised_) return;
    ma_engine_listener_set_position (engine_.get(), 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(engine_.get(), 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_world_up (engine_.get(), 0, up.x,  up.y,  up.z);
}

// ─── Path resolution ─────────────────────────────────────────────────────────

std::filesystem::path AudioEngine::resolve(const std::string& f) const noexcept {
    const std::filesystem::path p(f);
    if (p.is_absolute() && std::filesystem::exists(p)) return p;
    if (!soundRoot_.empty()) {
        auto full = soundRoot_ / p;
        if (std::filesystem::exists(full)) return full;
    }
    if (std::filesystem::exists(p)) return p;
    return {};
}

// ─── Sound pool ───────────────────────────────────────────────────────────────

int AudioEngine::findFreeSlot() const noexcept {
    for (int i = 0; i < kSoundPoolSize; ++i)
        if (!pool_[i].inUse) return i;
    return -1; // pool full — skip this sound
}

// ─── Playback ─────────────────────────────────────────────────────────────────

void AudioEngine::play3D(const std::string& file,
                          glm::vec3          worldPos,
                          float              volume,
                          float              maxRange) noexcept
{
    if (!initialised_) return;
    const auto path = resolve(file);
    if (path.empty()) {
        std::cerr << "[forge::audio] Sound not found: " << file << '\n';
        return;
    }

    const int slot = findFreeSlot();
    if (slot < 0) return; // pool exhausted

    auto& s = pool_[slot];

    constexpr ma_uint32 flags =
        MA_SOUND_FLAG_NO_SPATIALIZATION * 0 | // keep spatialization ON
        MA_SOUND_FLAG_DECODE |                // decode fully for low-latency
        MA_SOUND_FLAG_ASYNC;                  // load async

    if (ma_sound_init_from_file(engine_.get(), path.string().c_str(),
                                 flags, nullptr, nullptr, &s.sound) != MA_SUCCESS)
    {
        std::cerr << "[forge::audio] Failed to load: " << path << '\n';
        return;
    }

    ma_sound_set_spatialization_enabled(&s.sound, MA_TRUE);
    ma_sound_set_position(&s.sound, worldPos.x, worldPos.y, worldPos.z);
    ma_sound_set_min_distance(&s.sound, 1.f);
    ma_sound_set_max_distance(&s.sound, maxRange);
    ma_sound_set_volume(&s.sound, volume);

    ma_sound_start(&s.sound);
    s.inUse = true;
    s.is3D  = true;
}

void AudioEngine::play2D(const std::string& file, float volume, bool loop) noexcept {
    if (!initialised_) return;
    const auto path = resolve(file);
    if (path.empty()) {
        std::cerr << "[forge::audio] Sound not found: " << file << '\n';
        return;
    }

    const int slot = findFreeSlot();
    if (slot < 0) return;

    auto& s = pool_[slot];

    constexpr ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC;
    if (ma_sound_init_from_file(engine_.get(), path.string().c_str(),
                                 flags, nullptr, nullptr, &s.sound) != MA_SUCCESS)
        return;

    ma_sound_set_spatialization_enabled(&s.sound, MA_FALSE);
    ma_sound_set_volume(&s.sound, volume);
    ma_sound_set_looping(&s.sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&s.sound);
    s.inUse = true;
    s.is3D  = false;
}

void AudioEngine::stopAll() noexcept {
    for (int i = 0; i < kSoundPoolSize; ++i) pool_[i].reset();
}

void AudioEngine::setMasterVolume(float v) noexcept {
    if (initialised_)
        ma_engine_set_volume(engine_.get(), std::clamp(v, 0.f, 1.f));
}

void AudioEngine::update() noexcept {
    if (!initialised_) return;
    for (int i = 0; i < kSoundPoolSize; ++i) {
        auto& slot = pool_[i];
        if (slot.inUse && ma_sound_at_end(&slot.sound)) {
            slot.reset();
        }
    }
}

} // namespace forge::audio
