#pragma once

#include <forge/scene.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <optional>

namespace forge::runtime {

/// Jolt Physics world wrapper.
///
/// All Jolt types are hidden behind a PIMPL — consumers only need this header
/// and do not transitively pull in Jolt's large header tree.
///
/// Lifecycle:
///   PhysicsWorld::initGlobal()   — once at program start (idempotent)
///   world.init()                 — creates PhysicsSystem
///   world.addSceneGeometry(scene)— converts brush geometry → static bodies
///   loop: world.step(dt)
///   world.shutdown()             — destroys all bodies and the system
struct PhysicsWorld {

    // ── One-time global init (call before any PhysicsWorld) ──────────────────
    static void initGlobal() noexcept;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;
    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    [[nodiscard]] bool init()     noexcept;
    void               shutdown() noexcept;
    [[nodiscard]] bool valid()    const noexcept;

    // ── Scene geometry ────────────────────────────────────────────────────────

    /// Convert all solid BrushEntities in the scene to Jolt static bodies.
    /// Entity world transforms are baked into the convex hull vertices.
    void addSceneGeometry(const scene::Scene& scene) noexcept;

    // ── Simulation ────────────────────────────────────────────────────────────

    /// Step the simulation by dt seconds (internally uses a fixed substep).
    void step(float dt) noexcept;

    // ── Queries ───────────────────────────────────────────────────────────────

    struct RayHit {
        glm::vec3 point;
        glm::vec3 normal;
        float     t;
    };

    [[nodiscard]] std::optional<RayHit>
    raycast(glm::vec3 origin, glm::vec3 dir, float maxDist) const noexcept;

    // ── Native handles (needed by PlayerController .cpp only) ─────────────────
    /// Returns JPH::PhysicsSystem* — cast in callers that include Jolt headers.
    [[nodiscard]] void* nativeSystem()    const noexcept;
    [[nodiscard]] void* nativeTempAlloc() const noexcept;
    /// Object layer value for MOVING objects (Layers::MOVING).
    [[nodiscard]] int   movingLayer()     const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace forge::runtime
