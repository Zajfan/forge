#pragma once

#include <forge/scene.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>
#include <functional>

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

    // ── Dynamic bodies ────────────────────────────────────────────────────────

    using BodyHandle = uint32_t;
    static constexpr BodyHandle kInvalidBody = UINT32_MAX;

    /// Add a dynamic rigid body from a brush at the given world position.
    /// @param brush        Convex hull shape source
    /// @param position     Initial world-space position
    /// @param mass         Mass in kg (default 10 kg)
    /// @returns A BodyHandle to track and remove the body later
    [[nodiscard]] BodyHandle addDynamicBody(
        const geo::Brush& brush,
        glm::vec3 position,
        float mass = 10.f) noexcept;

    /// Apply an impulse to a dynamic body (world-space direction, kg*units/s).
    void applyImpulse(BodyHandle handle, glm::vec3 impulse) noexcept;

    /// Get the current world-space transform of a dynamic body.
    struct BodyState { glm::vec3 position; glm::quat rotation; bool valid = false; };
    [[nodiscard]] BodyState getBodyState(BodyHandle handle) const noexcept;

    /// Remove a dynamic body.
    void removeDynamicBody(BodyHandle handle) noexcept;

    /// Collect all dynamic body states (call after step to sync scene).
    struct DynamicBodySnapshot {
        BodyHandle handle;
        glm::vec3  position;
        glm::quat  rotation;
    };
    [[nodiscard]] std::vector<DynamicBodySnapshot> snapshotDynamicBodies() const noexcept;

    // ── Kinematic brush bodies ───────────────────────────────────────────────

    using KinematicHandle = uint32_t;
    static constexpr KinematicHandle kInvalidKinematic = UINT32_MAX;

    /// Add a kinematic rigid body representing a brush entity.
    /// Brush vertices are baked in local space (including entity scale).
    [[nodiscard]] KinematicHandle addKinematicBrushEntity(
        const scene::BrushEntity& entity) noexcept;

    /// Update kinematic body world transform.
    /// Uses MoveKinematic internally so velocity is consistent for contacts.
    void setKinematicTransform(
        KinematicHandle handle,
        const scene::Transform& transform,
        float dt) noexcept;

    /// Remove a kinematic body.
    void removeKinematicBody(KinematicHandle handle) noexcept;

    // ── Trigger volumes ───────────────────────────────────────────────────────

    using TriggerHandle = uint32_t;
    static constexpr TriggerHandle kInvalidTrigger = UINT32_MAX;
    using TriggerCallback = std::function<void(scene::EntityId)>;

    /// Add an AABB trigger volume at position with half-extents.
    /// @param position  Center of the trigger in world space
    /// @param halfExt   Half-extents (size / 2) of the AABB box
    /// @param onEnter   Callback invoked when a MOVING body enters
    [[nodiscard]] TriggerHandle addTriggerVolume(
        glm::vec3 position,
        glm::vec3 halfExt,
        TriggerCallback onEnter) noexcept;

    void removeTriggerVolume(TriggerHandle handle) noexcept;

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
