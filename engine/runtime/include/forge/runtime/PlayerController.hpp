#pragma once

#include "FPSCamera.hpp"
#include "PhysicsWorld.hpp"
#include <forge/gfx/Window.hpp>  // InputState
#include <memory>

namespace forge::runtime {

/// First-person player controller.
///
/// Physics: Jolt `CharacterVirtual` — capsule shape, ground detection, slopes.
/// Movement: WASD relative to camera yaw, Space to jump, mouse look.
///
/// Units: game units matching the brush geometry (1 unit ≈ 1 cm in Quake scale).
///   Player height:  ~180 units (capsule radius 20, half-height 70)
///   Eye offset:      160 units above floor
///   Walk speed:      220 units / second
///   Jump velocity:   380 units / second
///   Gravity:        -700 units / second²
struct PlayerController {

    // ── Tuning ────────────────────────────────────────────────────────────────
    float walkSpeed    = 220.f;
    float sprintMult   = 1.8f;
    float jumpVelocity = 380.f;
    float gravity      = -700.f;
    float eyeHeight    = 160.f;   ///< Eye position above the floor (units)
    float sensitivity  = 0.15f;   ///< Mouse sensitivity (deg/px)

    // ── Camera (owned, updated each frame) ────────────────────────────────────
    FPSCamera camera;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    PlayerController();
    ~PlayerController();
    PlayerController(PlayerController&&) noexcept;
    PlayerController& operator=(PlayerController&&) noexcept;
    PlayerController(const PlayerController&)            = delete;
    PlayerController& operator=(const PlayerController&) = delete;

    /// Create the Jolt character at the given spawn position.
    [[nodiscard]] bool init(PhysicsWorld& physics, glm::vec3 spawnPos) noexcept;

    void shutdown() noexcept;

    // ── Per-frame ─────────────────────────────────────────────────────────────

    /// Integrate player movement and update camera position.
    /// @param dt     Frame delta time (seconds, typically 0.016).
    /// @param input  Raw input state from Window::input().
    void update(float dt, const gfx::InputState& input, PhysicsWorld& physics) noexcept;

    // ── Queries ───────────────────────────────────────────────────────────────

    [[nodiscard]] glm::vec3 footPosition() const noexcept;
    [[nodiscard]] glm::vec3 eyePosition()  const noexcept;
    [[nodiscard]] bool      onGround()     const noexcept;
    [[nodiscard]] bool      valid()        const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace forge::runtime
