#pragma once

#include "PhysicsWorld.hpp"
#include "PlayerController.hpp"
#include <forge/scene.hpp>
#include <forge/gfx/Window.hpp>

namespace forge::script { class ScriptEnv; }

namespace forge::runtime {

/// Orchestrates physics, player, and camera for play mode.
///
/// Usage:
///   GameRuntime rt;
///   if (!rt.init(scene, spawnPos)) { /* error */ }
///   // each frame:
///   rt.update(dt, input);
///   renderer.beginFrame({ rt.viewMatrix(), rt.projMatrix(aspect), rt.cameraPosition(), ... });
///   rt.shutdown();
struct GameRuntime {

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    GameRuntime();
    ~GameRuntime();

    /// Build physics world from scene geometry and spawn player.
    /// @param scene     The current editor scene (read-only during play).
    /// @param spawnPos  World-space spawn position (from info_player_start, or default).
    [[nodiscard]] bool init(const scene::Scene& scene, glm::vec3 spawnPos) noexcept;

    void shutdown() noexcept;

    [[nodiscard]] bool valid() const noexcept { return physics_.valid(); }

    // ── Per-frame ─────────────────────────────────────────────────────────────

    /// Step physics, update player movement, update camera.
    void update(float dt, const gfx::InputState& input) noexcept;

    // ── Camera output ─────────────────────────────────────────────────────────

    [[nodiscard]] glm::mat4 viewMatrix()              const noexcept;
    [[nodiscard]] glm::mat4 projMatrix(float aspect)  const noexcept;
    [[nodiscard]] glm::vec3 cameraPosition()          const noexcept;

    // ── Debug info ────────────────────────────────────────────────────────────

    [[nodiscard]] glm::vec3 playerPosition() const noexcept;
    [[nodiscard]] bool      playerOnGround() const noexcept;
    [[nodiscard]] float     playerYaw()      const noexcept;
    [[nodiscard]] bool      playerCrouched() const noexcept;

    /// Instantly move the player to a new world position (respawn).
    void teleportPlayer(glm::vec3 pos) noexcept;

    /// Toggle the player crouch state (half capsule height / eye offset).
    void toggleCrouch() noexcept;

    /// Access the live script environment for the console panel.
    [[nodiscard]] script::ScriptEnv& scriptEnv() noexcept;

private:
    PhysicsWorld      physics_;
    PlayerController  player_;
    std::unique_ptr<script::ScriptEnv> scripts_;
    scene::Scene*     scene_ = nullptr;  ///< non-owning pointer to editor scene
};

} // namespace forge::runtime
