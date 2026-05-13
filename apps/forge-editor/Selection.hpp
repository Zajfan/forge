#pragma once

#include <forge/scene.hpp>
#include <forge/gfx/Camera.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

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

struct VertexSelection {
    scene::EntityId entityId = scene::kInvalidEntityId;
    std::size_t     brushIdx = 0;
    glm::dvec3      position = {};

    [[nodiscard]] bool valid() const noexcept { return entityId != scene::kInvalidEntityId; }
    void clear() noexcept {
        entityId = scene::kInvalidEntityId;
        brushIdx = 0;
        position = {};
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
        geo::AABB bounds;
        if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {
            if (!be->visible) continue;
            bounds = be->worldBounds();
        } else if (const auto* pe = std::get_if<scene::PointEntity>(&entity)) {
            bounds.expand(pe->transform.translation);
            bounds.expand(pe->transform.translation + glm::dvec3{16.0, 16.0, 16.0});
            bounds.expand(pe->transform.translation - glm::dvec3{16.0, 16.0, 16.0});
        } else if (const auto* me = std::get_if<scene::MeshEntity>(&entity)) {
            if (!me->visible) continue;
            bounds.expand(me->transform.translation);
            bounds.expand(me->transform.translation + glm::dvec3{16.0, 16.0, 16.0});
            bounds.expand(me->transform.translation - glm::dvec3{16.0, 16.0, 16.0});
        } else {
            continue;
        }
        if (!bounds.isValid()) continue;
        float t = 0.f;
        if (rayVsAABB(orig, dir, bounds, t) && t < bestT) {
            bestT  = t;
            bestId = id;
        }
    }
    return bestId;
}

// ─── MultiSelection ──────────────────────────────────────────────────────────

/// Selection of zero or more entities.
///
/// Replaces the single-entity Selection for entity-level picking.
/// The first entity added is the "primary" — gizmo targets it.
///
/// Shift+click: toggle membership.
/// Click (no shift): replace with single entity.
struct MultiSelection {
    std::vector<scene::EntityId> ids;

    [[nodiscard]] bool empty() const noexcept { return ids.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return ids.size(); }

    /// Primary entity (gizmo target, properties panel focus).
    [[nodiscard]] scene::EntityId primary() const noexcept {
        return ids.empty() ? scene::kInvalidEntityId : ids[0];
    }

    [[nodiscard]] bool contains(scene::EntityId id) const noexcept {
        return std::ranges::find(ids, id) != ids.end();
    }

    void add(scene::EntityId id) noexcept {
        if (!contains(id)) ids.push_back(id);
    }

    void remove(scene::EntityId id) noexcept {
        std::erase(ids, id);
    }

    void toggle(scene::EntityId id) noexcept {
        if (contains(id)) remove(id); else add(id);
    }

    void set(scene::EntityId id) noexcept {
        ids.clear();
        if (id != scene::kInvalidEntityId) ids.push_back(id);
    }

    void selectEntity(scene::EntityId id) noexcept { set(id); }

    void clear() noexcept { ids.clear(); }

    /// Combined world-space AABB of all selected BrushEntities.
    [[nodiscard]] geo::AABB combinedBounds(const scene::Scene& scene) const noexcept {
        geo::AABB box;
        for (auto id : ids) {
            const auto* e = scene.getEntity(id);
            if (!e) continue;
            if (const auto* be = std::get_if<scene::BrushEntity>(e))
                box.expand(be->worldBounds());
        }
        return box;
    }
};

} // namespace forge::editor
