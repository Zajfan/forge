#include "forge/runtime/PlayerController.hpp"

// Jolt includes (confined to this translation unit)
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

JPH_SUPPRESS_WARNINGS

#include <algorithm>
#include <cmath>
#include <iostream>

namespace forge::runtime {

// ─── PlayerController::Impl ──────────────────────────────────────────────────

struct PlayerController::Impl {
    JPH::Ref<JPH::CharacterVirtual> character;
    glm::vec3 velocity     = {};
    bool      onGround_    = false;

    // Capsule geometry (in game units)
    static constexpr float kRadius     = 20.f;
    static constexpr float kHalfHeight = 70.f;  // half of cylinder part
    // Total capsule height = 2*radius + 2*halfHeight = 180 units

    // Vertical-only velocity carried between frames (for gravity + jump)
    float verticalVel = 0.f;
};

// ─── Lifecycle ────────────────────────────────────────────────────────────────

PlayerController::PlayerController()  = default;
PlayerController::~PlayerController() { shutdown(); }
PlayerController::PlayerController(PlayerController&&) noexcept            = default;
PlayerController& PlayerController::operator=(PlayerController&&) noexcept = default;

bool PlayerController::init(PhysicsWorld& physics, glm::vec3 spawnPos) noexcept {
    if (!physics.valid()) {
        std::cerr << "[PlayerController] PhysicsWorld not valid\n";
        return false;
    }

    auto* sys = static_cast<JPH::PhysicsSystem*>(physics.nativeSystem());

    impl_ = std::make_unique<Impl>();

    // Character shape: capsule
    JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(
        Impl::kHalfHeight, Impl::kRadius);

    // Character settings
    JPH::CharacterVirtualSettings settings;
    settings.mShape         = capsule;
    settings.mMaxSlopeAngle = glm::radians(50.f);
    settings.mMass          = 70.f;
    settings.mMaxStrength   = 1000.f;  // how hard the character pushes objects
    settings.mUp            = JPH::Vec3::sAxisY();
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(),
                                             -Impl::kRadius); // ground-sensing plane

    // Spawn position: character centre = foot + (halfHeight + radius)
    const glm::vec3 centre = spawnPos + glm::vec3(0.f, Impl::kHalfHeight + Impl::kRadius, 0.f);

    impl_->character = new JPH::CharacterVirtual(
        &settings,
        JPH::RVec3(centre.x, centre.y, centre.z),
        JPH::Quat::sIdentity(),
        0,   // userData
        sys);

    // Set camera yaw from player start (face south by default)
    camera.yaw   = 180.f;
    camera.pitch = 0.f;

    return true;
}

void PlayerController::shutdown() noexcept {
    if (impl_) {
        impl_->character = nullptr;
        impl_.reset();
    }
}

bool PlayerController::valid() const noexcept {
    return impl_ && impl_->character;
}

// ─── Per-frame update ─────────────────────────────────────────────────────────

void PlayerController::update(float dt, const gfx::InputState& input,
                               PhysicsWorld& physics) noexcept {
    if (!valid()) return;

    auto* sys     = static_cast<JPH::PhysicsSystem*>(physics.nativeSystem());
    auto* tmpAlloc = static_cast<JPH::TempAllocator*>(physics.nativeTempAlloc());

    // ── Mouse look ───────────────────────────────────────────────────────────
    camera.applyMouseDelta(input.mouse.dx, input.mouse.dy, sensitivity);

    // ── Horizontal movement ───────────────────────────────────────────────────
    const float yawR    = glm::radians(camera.yaw);
    const glm::vec3 fwd = { std::cos(yawR), 0.f, std::sin(yawR) };
    const glm::vec3 rgt = glm::normalize(glm::cross(fwd, {0,1,0}));

    glm::vec3 moveDir = {};
    if (input.keys.w) moveDir += fwd;
    if (input.keys.s) moveDir -= fwd;
    if (input.keys.a) moveDir -= rgt;
    if (input.keys.d) moveDir += rgt;

    const float speed = walkSpeed * (input.keys.shift ? sprintMult : 1.f);
    const float moveLen = glm::length(moveDir);
    glm::vec3 horizVel = moveLen > 1e-4f
        ? (moveDir / moveLen) * speed
        : glm::vec3(0.f);

    // ── Vertical velocity (gravity + jump) ───────────────────────────────────
    const bool groundState =
        impl_->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    impl_->onGround_ = groundState;

    if (groundState) {
        if (impl_->verticalVel < 0.f) impl_->verticalVel = 0.f; // stop sinking
        if (input.keys.q || input.keys.e) // jump: Q or E (space not in our InputState yet)
            impl_->verticalVel = jumpVelocity;
    } else {
        impl_->verticalVel += gravity * dt;
    }

    const glm::vec3 totalVel = horizVel + glm::vec3(0.f, impl_->verticalVel, 0.f);
    impl_->character->SetLinearVelocity(
        JPH::Vec3(totalVel.x, totalVel.y, totalVel.z));

    // ── CharacterVirtual update ───────────────────────────────────────────────
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mStickToFloorStepDown    = JPH::Vec3(0.f, -Impl::kRadius * 0.5f, 0.f);
    updateSettings.mWalkStairsStepUp        = JPH::Vec3(0.f,  Impl::kRadius * 1.5f, 0.f);
    updateSettings.mWalkStairsMinStepForward = Impl::kRadius * 0.25f;
    updateSettings.mWalkStairsStepForwardTest = Impl::kRadius;

    impl_->character->ExtendedUpdate(
        dt,
        JPH::Vec3(0.f, gravity, 0.f),
        updateSettings,
        sys->GetDefaultBroadPhaseLayerFilter(
            static_cast<JPH::BroadPhaseLayer>(0)), // STATIC layer
        sys->GetDefaultLayerFilter(
            static_cast<JPH::ObjectLayer>(physics.movingLayer())),
        {},  // body filter (none)
        {},  // shape filter (none)
        *tmpAlloc);

    // ── Sync camera position ─────────────────────────────────────────────────
    camera.position = eyePosition();
}

// ─── Queries ─────────────────────────────────────────────────────────────────

glm::vec3 PlayerController::footPosition() const noexcept {
    if (!valid()) return {};
    const JPH::RVec3 c = impl_->character->GetPosition();
    // Feet = centre - (halfHeight + radius)
    return { (float)c.GetX(),
             (float)c.GetY() - Impl::kHalfHeight - Impl::kRadius,
             (float)c.GetZ() };
}

glm::vec3 PlayerController::eyePosition() const noexcept {
    return footPosition() + glm::vec3(0.f, eyeHeight, 0.f);
}

bool PlayerController::onGround() const noexcept {
    return impl_ && impl_->onGround_;
}

} // namespace forge::runtime
