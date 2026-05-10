#pragma once

#include <forge/scene.hpp>
#include <forge/gfx/Camera.hpp>
#include <glm/vec2.hpp>
#include <limits>
#include <optional>

namespace forge::editor {

// ─── Entity-level selection ───────────────────────────────────────────────────

struct Selection {
    scene::EntityId entityId = scene::kInvalidEntityId;

    [[nodiscard]] bool hasEntity() const noexcept {
        return entityId != scene::kInvalidEntityId;
    }

    void clear()                           noexcept { entityId = scene::kInvalidEntityId; }
    void selectEntity(scene::EntityId id)  noexcept { entityId = id; }
};

// ─── Face-level selection ─────────────────────────────────────────────────────

struct FaceSelection {
    scene::EntityId entityId = scene::kInvalidEntityId;
    std::size_t     brushIdx = 0;
    std::size_t     faceIdx  = 0;

    [[nodiscard]] bool valid() const noexcept { return entityId != scene::kInvalidEntityId; }
    void clear() noexcept { entityId = scene::kInvalidEntityId; }
    void select(scene::EntityId id, std::size_t b, std::size_t f) noexcept {
        entityId = id; brushIdx = b; faceIdx = f;
    }
};

// ─── Ray vs AABB ─────────────────────────────────────────────────────────────

[[nodiscard]] inline bool rayVsAABB(
    glm::vec3 origin, glm::vec3 dir,
    const geo::AABB& box, float& t) noexcept
{
    const glm::vec3 inv    = 1.f / dir;
    const glm::vec3 t0     = (glm::vec3(box.mins) - origin) * inv;
    const glm::vec3 t1     = (glm::vec3(box.maxs) - origin) * inv;
    const glm::vec3 tNearV = glm::min(t0, t1);
    const glm::vec3 tFarV  = glm::max(t0, t1);
    const float tNear = std::max({tNearV.x, tNearV.y, tNearV.z});
    const float tFar  = std::min({tFarV.x,  tFarV.y,  tFarV.z});
    if (tNear > tFar || tFar < 0.f) return false;
    t = tNear >= 0.f ? tNear : tFar;
    return true;
}

// ─── Entity pick ─────────────────────────────────────────────────────────────

[[nodiscard]] inline scene::EntityId pickEntity(
    glm::vec2 mouseNDC,
    const scene::Scene& scene,
    const gfx::OrbitCamera& camera,
    float aspect) noexcept
{
    const glm::mat4 invVP = glm::inverse(camera.vpMatrix(aspect));
    const glm::vec4 nearH = invVP * glm::vec4(mouseNDC, -1.f, 1.f);
    const glm::vec4 farH  = invVP * glm::vec4(mouseNDC,  1.f, 1.f);
    const glm::vec3 orig  = glm::vec3(nearH) / nearH.w;
    const glm::vec3 dir   = glm::normalize(glm::vec3(farH) / farH.w - orig);

    scene::EntityId bestId = scene::kInvalidEntityId;
    float           bestT  = std::numeric_limits<float>::max();

    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->visible) continue;
        const geo::AABB bounds = be->worldBounds();
        if (!bounds.isValid()) continue;
        float t = 0.f;
        if (rayVsAABB(orig, dir, bounds, t) && t < bestT) {
            bestT  = t;
            bestId = id;
        }
    }
    return bestId;
}

} // namespace forge::editor
