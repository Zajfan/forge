#pragma once

#include <glm/vec3.hpp>
#include <filesystem>
#include <string>
#include <array>
#include <memory>

// Forward-declare miniaudio types to keep this header clean
typedef struct ma_engine ma_engine;
typedef struct ma_sound  ma_sound;

namespace forge::audio {

// ─── AudioEngine ─────────────────────────────────────────────────────────────

/// Spatial audio engine built on miniaudio.
///
/// Supports:
///   - 3D positioned one-shot sounds (pooled, fire-and-forget)
///   - 2D ambient/music playback
///   - Listener position + orientation (updated each frame from camera/player)
///
/// Lifecycle:
///   init() → setListenerXxx() + play() per frame → shutdown()
class AudioEngine {
public:
    static constexpr int kSoundPoolSize = 32; ///< Concurrent positioned sounds

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Initialise the default audio device.
    [[nodiscard]] bool init() noexcept;

    void shutdown() noexcept;

    [[nodiscard]] bool valid() const noexcept { return initialised_; }

    // ── Sound root ────────────────────────────────────────────────────────────

    void setSoundRoot(const std::filesystem::path& root) noexcept;

    // ── Listener (update every frame) ─────────────────────────────────────────

    /// Set the listener position and orientation (player / camera).
    void setListenerTransform(glm::vec3 pos,
                               glm::vec3 forwardDir,
                               glm::vec3 upDir = {0.f,1.f,0.f}) noexcept;

    // ── Playback ──────────────────────────────────────────────────────────────

    /// Play a fire-and-forget 3D positioned sound.
    /// The engine manages the sound slot lifetime — no cleanup needed.
    ///
    /// @param file      Path relative to soundRoot_, or absolute.
    /// @param worldPos  Position of the sound source in world space.
    /// @param volume    Linear volume multiplier (0–1, default 1).
    /// @param maxRange  Distance at which the sound becomes inaudible.
    void play3D(const std::string& file,
                glm::vec3 worldPos,
                float volume   = 1.f,
                float maxRange = 1024.f) noexcept;

    /// Play a 2D (non-spatial) ambient sound.
    /// @param loop  If true, loops until stopped.
    void play2D(const std::string& file,
                float volume = 0.6f,
                bool  loop   = false) noexcept;

    /// Stop all currently playing sounds.
    void stopAll() noexcept;

    /// Set master volume (0–1).
    void setMasterVolume(float v) noexcept;

    // ── Per-frame tick ────────────────────────────────────────────────────────

    /// Free finished sound pool slots.  Call once per frame.
    void update() noexcept;

private:
    struct SoundSlot;

    bool                        initialised_ = false;
    std::filesystem::path       soundRoot_;
    std::unique_ptr<ma_engine>  engine_;
    std::array<SoundSlot, kSoundPoolSize> pool_;

    [[nodiscard]] std::filesystem::path resolve(const std::string& f) const noexcept;
    [[nodiscard]] int findFreeSlot() const noexcept;
};

} // namespace forge::audio
