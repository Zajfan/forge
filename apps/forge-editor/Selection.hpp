#pragma once

#include <forge/scene.hpp>
#include <forge/gfx/Camera.hpp>
#include <glm/vec2.hpp>
#include <optional>

namespace forge::editor {

// ─── Selection ────────────────────────────────────────────────────────────────

struct Selection {
    scene::EntityId entityId = scene::kInvalidEntityId;

    [[nodiscard]] bool hasEntity() const noexcept {
        return entityId != scene::kInvalidEntityId;
    }

    void clear()                             noexcept { entityId = scene::kInvalidEntityId; }
    void selectEntity(scene::EntityId id)   noexcept { entityId = id; }
};

// ─── Raycast ─────────────────────────────────────────────────────────────────

/// Ray vs AABB intersection (slab method).
/// @param t  Distance along ray to first hit (only valid on true return).
[[nodiscard]] inline bool rayVsAABB(
    glm::vec3       origin,
    glm::vec3       dir,
    const geo::AABB& box,
    float&           t) noexcept
{
    const glm::vec3 inv = 1.f / dir;
    const glm::vec3 t0  = (glm::vec3(box.mins) - origin) * inv;
    const glm::vec3 t1  = (glm::vec3(box.maxs) - origin) * inv;
    const glm::vec3 tNearV = glm::min(t0, t1);
    const glm::vec3 tFarV  = glm::max(t0, t1);
    const float tNear = std::max({tNearV.x, tNearV.y, tNearV.z});
    const float tFar  = std::min({tFarV.x,  tFarV.y,  tFarV.z });
    if (tNear > tFar || tFar < 0.f) return false;
    t = tNear >= 0.f ? tNear : tFar;
    return true;
}

/// Pick the closest BrushEntity under a viewport click.
///
/// @param mouseNDC    Mouse position in Normalised Device Coordinates
///                    (top-left = {-1, 1}, bottom-right = {1, -1}).
/// @param scene       The scene to test against.
/// @param camera      The camera providing the VP matrix.
/// @param aspect      Viewport aspect ratio.
///
/// @returns EntityId of the closest hit, or kInvalidEntityId.
[[nodiscard]] inline scene::EntityId pickEntity(
    glm::vec2              mouseNDC,
    const scene::Scene&    scene,
    const gfx::OrbitCamera& camera,
    float                  aspect) noexcept
{
    const glm::mat4 invVP = glm::inverse(camera.vpMatrix(aspect));

    // Unproject two points on the ray (near and far clip)
    const glm::vec4 nearH = invVP * glm::vec4(mouseNDC, -1.f, 1.f);
    const glm::vec4 farH  = invVP * glm::vec4(mouseNDC,  1.f, 1.f);
    const glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farP  = glm::vec3(farH)  / farH.w;

    const glm::vec3 origin = nearP;
    const glm::vec3 dir    = glm::normalize(farP - nearP);

    scene::EntityId bestId = scene::kInvalidEntityId;
    float           bestT  = std::numeric_limits<float>::max();

    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->visible) continue;

        const geo::AABB bounds = be->worldBounds();
        if (!bounds.isValid()) continue;

        float t = 0.f;
        if (rayVsAABB(origin, dir, bounds, t) && t < bestT) {
            bestT  = t;
            bestId = id;
        }
    }

    return bestId;
}

} // namespace forge::editor
