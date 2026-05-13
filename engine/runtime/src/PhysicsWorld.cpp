#include "forge/runtime/PhysicsWorld.hpp"

// ── Jolt headers ─────────────────────────────────────────────────────────────
// All Jolt includes are confined to this translation unit.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/RegisterTypes.h>

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <thread>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace forge::runtime {

// ─── Object layers ────────────────────────────────────────────────────────────

namespace Layers {
    static constexpr JPH::ObjectLayer STATIC = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::uint32 COUNT       = 2;
}

namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer STATIC(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint32 COUNT = 2;
}

// ─── BroadPhaseLayerInterface ─────────────────────────────────────────────────

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::COUNT];
public:
    BPLayerInterface() {
        mObjectToBroadPhase[Layers::STATIC] = BPLayers::STATIC;
        mObjectToBroadPhase[Layers::MOVING] = BPLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override {
        return BPLayers::COUNT;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        JPH_ASSERT(layer < Layers::COUNT);
        return mObjectToBroadPhase[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BPLayers::STATIC ? "STATIC" : "MOVING";
    }
#endif
};

// ─── ObjectVsBroadPhaseLayerFilter ────────────────────────────────────────────

class OVBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override {
        switch (obj) {
        case Layers::STATIC:
            return bp == BPLayers::MOVING; // static only interacts with movers
        case Layers::MOVING:
            return true;                   // movers interact with everything
        default:
            return false;
        }
    }
};

// ─── ObjectLayerPairFilter ────────────────────────────────────────────────────

class OLPFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        // Static-static: no collision (saves broadphase work)
        if (a == Layers::STATIC && b == Layers::STATIC) return false;
        return true;
    }
};

// ─── PhysicsWorld::Impl ───────────────────────────────────────────────────────

// Internal per-body metadata
struct DynamicBodyEntry {
    JPH::BodyID joltId;
};

struct TriggerEntry {
    JPH::BodyID               joltId;
    PhysicsWorld::TriggerCallback onEnter;
    glm::vec3                 position;
    glm::vec3                 halfExt;
    bool                      wasOccupied = false;
};

struct PhysicsWorld::Impl {
    BPLayerInterface                         bpLayer;
    OVBPFilter                               ovbpFilter;
    OLPFilter                                olpFilter;
    std::unique_ptr<JPH::TempAllocatorImpl>  tempAlloc;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem>      system;
    std::vector<JPH::BodyID>                 staticBodies;

    // Dynamic bodies
    PhysicsWorld::BodyHandle                 nextBodyHandle = 0;
    std::unordered_map<PhysicsWorld::BodyHandle, DynamicBodyEntry> dynamicBodies;

    // Trigger volumes
    PhysicsWorld::TriggerHandle              nextTriggerHandle = 0;
    std::unordered_map<PhysicsWorld::TriggerHandle, TriggerEntry> triggers;

    // Fixed-step accumulator
    float accumulator = 0.f;
    static constexpr float kStep = 1.f / 60.f;
};

// ─── Global init ─────────────────────────────────────────────────────────────

static bool sGlobalInit = false;

void PhysicsWorld::initGlobal() noexcept {
    if (sGlobalInit) return;
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    sGlobalInit = true;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

PhysicsWorld::PhysicsWorld()  = default;
PhysicsWorld::~PhysicsWorld() { shutdown(); }
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept            = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

bool PhysicsWorld::init() noexcept {
    if (!sGlobalInit) {
        std::cerr << "[PhysicsWorld] initGlobal() not called\n";
        return false;
    }

    impl_ = std::make_unique<Impl>();

    // Allocators and thread pool
    impl_->tempAlloc = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024); // 16 MB
    const int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
    impl_->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

    // Physics system
    const JPH::uint cMaxBodies              = 2048;
    const JPH::uint cNumBodyMutexes         = 0;     // 0 = auto
    const JPH::uint cMaxBodyPairs           = 4096;
    const JPH::uint cMaxContactConstraints  = 2048;

    impl_->system = std::make_unique<JPH::PhysicsSystem>();
    impl_->system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                        impl_->bpLayer, impl_->ovbpFilter, impl_->olpFilter);

    impl_->system->SetGravity(JPH::Vec3(0.f, -9.81f * 64.f, 0.f)); // Quake-scale gravity

    return true;
}

void PhysicsWorld::shutdown() noexcept {
    if (!impl_) return;
    // Remove all bodies before destroying the system
    if (impl_->system) {
        auto& bi = impl_->system->GetBodyInterface();
        for (auto id : impl_->staticBodies) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
        impl_->staticBodies.clear();
        for (auto& [h, e] : impl_->dynamicBodies) {
            bi.RemoveBody(e.joltId);
            bi.DestroyBody(e.joltId);
        }
        impl_->dynamicBodies.clear();
        for (auto& [h, t] : impl_->triggers) {
            bi.RemoveBody(t.joltId);
            bi.DestroyBody(t.joltId);
        }
        impl_->triggers.clear();
    }
    impl_.reset();
}

bool PhysicsWorld::valid() const noexcept { return impl_ && impl_->system; }

// ─── Add scene geometry ───────────────────────────────────────────────────────

static JPH::Ref<JPH::Shape>
brushToShape(const geo::Brush& brush, const glm::mat4& worldMat) noexcept {
    const auto& verts = brush.vertices();
    if (verts.size() < 4) return nullptr;

    JPH::ConvexHullShapeSettings settings;
    settings.mPoints.reserve(verts.size());

    for (const auto& lv : verts) {
        // Apply entity world transform to get world-space vertex
        const glm::vec4 wv = worldMat * glm::vec4(glm::vec3(lv), 1.f);
        settings.mPoints.push_back(JPH::Vec3(wv.x, wv.y, wv.z));
    }

    settings.mMaxConvexRadius = 0.05f;  // small for tight geometry

    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError()) {
        // Fall back to AABB shape if convex hull fails
        const geo::AABB b = brush.bounds();
        const glm::vec3 mins = glm::vec3(worldMat * glm::vec4(glm::vec3(b.mins), 1.f));
        const glm::vec3 maxs = glm::vec3(worldMat * glm::vec4(glm::vec3(b.maxs), 1.f));
        const glm::vec3 halfExt = (maxs - mins) * 0.5f;
        const JPH::Vec3 jHalf(std::abs(halfExt.x), std::abs(halfExt.y), std::abs(halfExt.z));
        return new JPH::BoxShape(jHalf + JPH::Vec3(0.01f, 0.01f, 0.01f));
    }

    return result.Get();
}

void PhysicsWorld::addSceneGeometry(const scene::Scene& scene) noexcept {
    if (!impl_) return;
    auto& bi = impl_->system->GetBodyInterface();

    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->solid) continue;
        if (be->classname == "func_door" ||
            be->classname == "func_plat" ||
            be->classname == "func_rotating")
            continue;

        const glm::mat4 worldMat = glm::mat4(be->transform.matrix());

        for (const auto& brush : be->brushes) {
            auto shape = brushToShape(brush, worldMat);
            if (!shape) continue;

            JPH::BodyCreationSettings bcs(
                shape,
                JPH::RVec3::sZero(),        // origin (transform baked into shape)
                JPH::Quat::sIdentity(),
                JPH::EMotionType::Static,
                Layers::STATIC);

            const JPH::BodyID bodyId = bi.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
            if (!bodyId.IsInvalid())
                impl_->staticBodies.push_back(bodyId);
        }
    }

    // Optimise the broadphase after adding static geometry
    impl_->system->OptimizeBroadPhase();
}

// ─── Step ─────────────────────────────────────────────────────────────────────

void PhysicsWorld::step(float dt) noexcept {
    if (!impl_) return;
    impl_->accumulator += dt;
    // Cap accumulator to avoid spiral-of-death on slow frames
    impl_->accumulator = std::min(impl_->accumulator, Impl::kStep * 4.f);

    while (impl_->accumulator >= Impl::kStep) {
        impl_->system->Update(Impl::kStep, 1,
            impl_->tempAlloc.get(), impl_->jobSystem.get());
        impl_->accumulator -= Impl::kStep;
    }
    // ── Trigger volume overlap checks ──────────────────────────────────────
    // Simple AABB overlap check against dynamic bodies for trigger callbacks
    for (auto& [th, trigger] : impl_->triggers) {
        const glm::vec3 tmin = trigger.position - trigger.halfExt;
        const glm::vec3 tmax = trigger.position + trigger.halfExt;

        bool occupied = false;
        for (const auto& [bh, entry] : impl_->dynamicBodies) {
            const auto& bi = impl_->system->GetBodyInterface();
            const JPH::Vec3 pos = bi.GetPosition(entry.joltId);
            const glm::vec3 bp(pos.GetX(), pos.GetY(), pos.GetZ());
            if (bp.x >= tmin.x && bp.x <= tmax.x &&
                bp.y >= tmin.y && bp.y <= tmax.y &&
                bp.z >= tmin.z && bp.z <= tmax.z) {
                occupied = true;
                if (trigger.onEnter)
                    trigger.onEnter(scene::kInvalidEntityId);
                break;
            }
        }
        trigger.wasOccupied = occupied;
    }}

// ─── Raycast ─────────────────────────────────────────────────────────────────

std::optional<PhysicsWorld::RayHit>
PhysicsWorld::raycast(glm::vec3 origin, glm::vec3 dir, float maxDist) const noexcept {
    if (!impl_) return std::nullopt;

    const JPH::RRayCast ray{
        JPH::RVec3(origin.x, origin.y, origin.z),
        JPH::Vec3(dir.x, dir.y, dir.z) * maxDist
    };

    JPH::RayCastResult hit;
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, hit,
        JPH::SpecifiedBroadPhaseLayerFilter(BPLayers::STATIC),
        JPH::SpecifiedObjectLayerFilter(Layers::STATIC)))
        return std::nullopt;

    const glm::vec3 point  = origin + dir * hit.mFraction * maxDist;
    const glm::vec3 normal = {}; // normal retrieval requires additional query

    return RayHit{ point, normal, hit.mFraction * maxDist };
}

// ─── Native handles ───────────────────────────────────────────────────────────

void* PhysicsWorld::nativeSystem()    const noexcept { return impl_ ? impl_->system.get()    : nullptr; }
void* PhysicsWorld::nativeTempAlloc() const noexcept { return impl_ ? impl_->tempAlloc.get() : nullptr; }
int   PhysicsWorld::movingLayer()     const noexcept { return Layers::MOVING; }
// ─── Dynamic bodies ────────────────────────────────────────────────────────────────

PhysicsWorld::BodyHandle
PhysicsWorld::addDynamicBody(const geo::Brush& brush,
                              glm::vec3 position,
                              float mass) noexcept {
    if (!impl_) return kInvalidBody;

    const auto& verts = brush.vertices();
    if (verts.empty()) return kInvalidBody;

    // Build convex hull shape from brush vertices
    JPH::ConvexHullShapeSettings settings;
    settings.mPoints.reserve(verts.size());
    for (const auto& v : verts)
        settings.mPoints.push_back(JPH::Vec3((float)v.x, (float)v.y, (float)v.z));
    settings.mMaxConvexRadius = 0.05f;

    auto result = settings.Create();
    JPH::Ref<JPH::Shape> shape;
    if (result.HasError()) {
        // Fallback: unit sphere via settings
        JPH::SphereShapeSettings ss(16.f);
        auto sr = ss.Create();
        if (sr.HasError()) return kInvalidBody;
        shape = sr.Get();
    } else {
        shape = result.Get();
    }

    JPH::BodyCreationSettings bcs(
        shape,
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING);
    bcs.mMassPropertiesOverride.mMass = mass;
    bcs.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;

    auto& bi = impl_->system->GetBodyInterface();
    const JPH::BodyID joltId = bi.CreateAndAddBody(bcs, JPH::EActivation::Activate);
    if (joltId.IsInvalid()) return kInvalidBody;

    const BodyHandle handle = impl_->nextBodyHandle++;
    impl_->dynamicBodies[handle] = { joltId };
    return handle;
}

void PhysicsWorld::applyImpulse(BodyHandle handle, glm::vec3 impulse) noexcept {
    if (!impl_) return;
    const auto it = impl_->dynamicBodies.find(handle);
    if (it == impl_->dynamicBodies.end()) return;
    impl_->system->GetBodyInterface().AddImpulse(
        it->second.joltId,
        JPH::Vec3(impulse.x, impulse.y, impulse.z));
}

PhysicsWorld::BodyState
PhysicsWorld::getBodyState(BodyHandle handle) const noexcept {
    if (!impl_) return {};
    const auto it = impl_->dynamicBodies.find(handle);
    if (it == impl_->dynamicBodies.end()) return {};

    const auto& bi = impl_->system->GetBodyInterface();
    const JPH::Vec3 pos  = bi.GetPosition(it->second.joltId);
    const JPH::Quat quat = bi.GetRotation(it->second.joltId);

    BodyState state;
    state.valid    = true;
    state.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
    state.rotation = glm::quat(quat.GetW(), quat.GetX(), quat.GetY(), quat.GetZ());
    return state;
}

void PhysicsWorld::removeDynamicBody(BodyHandle handle) noexcept {
    if (!impl_) return;
    const auto it = impl_->dynamicBodies.find(handle);
    if (it == impl_->dynamicBodies.end()) return;
    auto& bi = impl_->system->GetBodyInterface();
    bi.RemoveBody(it->second.joltId);
    bi.DestroyBody(it->second.joltId);
    impl_->dynamicBodies.erase(it);
}

std::vector<PhysicsWorld::DynamicBodySnapshot>
PhysicsWorld::snapshotDynamicBodies() const noexcept {
    if (!impl_) return {};
    std::vector<DynamicBodySnapshot> out;
    out.reserve(impl_->dynamicBodies.size());
    const auto& bi = impl_->system->GetBodyInterface();
    for (const auto& [handle, entry] : impl_->dynamicBodies) {
        const JPH::Vec3 pos  = bi.GetPosition(entry.joltId);
        const JPH::Quat quat = bi.GetRotation(entry.joltId);
        out.push_back({
            handle,
            glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ()),
            glm::quat(quat.GetW(), quat.GetX(), quat.GetY(), quat.GetZ())
        });
    }
    return out;
}

// ─── Trigger volumes ─────────────────────────────────────────────────────────────────

PhysicsWorld::TriggerHandle
PhysicsWorld::addTriggerVolume(glm::vec3 position,
                                glm::vec3 halfExt,
                                TriggerCallback onEnter) noexcept {
    if (!impl_) return kInvalidTrigger;

    // Triggers are sensor bodies (no collision response, just overlap detection)
    JPH::Ref<JPH::BoxShape> shape = new JPH::BoxShape(
        JPH::Vec3(halfExt.x, halfExt.y, halfExt.z));

    JPH::BodyCreationSettings bcs(
        shape,
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::STATIC);
    bcs.mIsSensor = true;

    auto& bi = impl_->system->GetBodyInterface();
    const JPH::BodyID joltId = bi.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
    if (joltId.IsInvalid()) return kInvalidTrigger;

    const TriggerHandle handle = impl_->nextTriggerHandle++;
    impl_->triggers[handle] = { joltId, std::move(onEnter), position, halfExt, false };
    return handle;
}

void PhysicsWorld::removeTriggerVolume(TriggerHandle handle) noexcept {
    if (!impl_) return;
    const auto it = impl_->triggers.find(handle);
    if (it == impl_->triggers.end()) return;
    auto& bi = impl_->system->GetBodyInterface();
    bi.RemoveBody(it->second.joltId);
    bi.DestroyBody(it->second.joltId);
    impl_->triggers.erase(it);
}
} // namespace forge::runtime
