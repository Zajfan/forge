#pragma once

#include "Selection.hpp"   // for rayVsAABB
#include <forge/scene.hpp>
#include <forge/gfx/Camera.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace forge::editor {

// ─── FaceHit ─────────────────────────────────────────────────────────────────

struct FaceHit {
    scene::EntityId entityId = scene::kInvalidEntityId;
    std::size_t     brushIdx = 0;
    std::size_t     faceIdx  = 0;
    float           t        = 0.f;
    glm::vec3       point    = {};

    [[nodiscard]] bool valid() const noexcept {
        return entityId != scene::kInvalidEntityId;
    }
};

// ─── Möller–Trumbore triangle intersection ───────────────────────────────────

/// Ray vs triangle.  Returns false if parallel or behind origin.
/// @param t  Distance along ray to intersection (only valid when true).
[[nodiscard]] inline bool rayTriangle(
    glm::vec3 orig, glm::vec3 dir,
    glm::vec3 v0, glm::vec3 v1, glm::vec3 v2,
    float& t) noexcept
{
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 h  = glm::cross(dir, e2);
    const float     a  = glm::dot(e1, h);
    if (std::abs(a) < 1e-7f) return false;   // parallel
    const float     f  = 1.f / a;
    const glm::vec3 s  = orig - v0;
    const float     u  = f * glm::dot(s, h);
    if (u < 0.f || u > 1.f) return false;
    const glm::vec3 q  = glm::cross(s, e1);
    const float     v  = f * glm::dot(dir, q);
    if (v < 0.f || u + v > 1.f) return false;
    t = f * glm::dot(e2, q);
    return t > 1e-6f;
}

// ─── Ray vs convex polygon ────────────────────────────────────────────────────

/// Ray vs a convex polygon given as an ordered vertex list (fan-triangulated).
[[nodiscard]] inline bool rayPolygon(
    glm::vec3 orig, glm::vec3 dir,
    std::span<const glm::dvec3> poly,
    float& t) noexcept
{
    if (poly.size() < 3) return false;
    const glm::vec3 v0 = glm::vec3(poly[0]);
    float bestT = std::numeric_limits<float>::max();
    bool  hit   = false;
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
        float tt;
        if (rayTriangle(orig, dir,
                        v0,
                        glm::vec3(poly[i]),
                        glm::vec3(poly[i + 1]), tt)
            && tt < bestT)
        {
            bestT = tt;
            hit   = true;
        }
    }
    if (hit) t = bestT;
    return hit;
}

// ─── Scene face pick ─────────────────────────────────────────────────────────

/// Pick the closest brush face under a viewport click.
///
/// Applies entity transforms so picking is done in world space.
/// Falls back to AABB rejection before testing individual face polygons.
///
/// @param mouseNDC  Cursor in Normalised Device Coordinates
///                  (centre = {0,0}, top-left = {-1,1}).
[[nodiscard]] inline std::optional<FaceHit> pickFace(
    glm::vec2              mouseNDC,
    const scene::Scene&    scene,
    const gfx::OrbitCamera& camera,
    float                  aspect) noexcept
{
    const glm::mat4 invVP = glm::inverse(camera.vpMatrix(aspect));

    // Unproject near and far points
    const glm::vec4 nearH = invVP * glm::vec4(mouseNDC, -1.f, 1.f);
    const glm::vec4 farH  = invVP * glm::vec4(mouseNDC,  1.f, 1.f);
    const glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farP  = glm::vec3(farH)  / farH.w;

    const glm::vec3 orig = nearP;
    const glm::vec3 dir  = glm::normalize(farP - nearP);

    FaceHit best;
    float   bestT = std::numeric_limits<float>::max();

    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->visible) continue;

        // Broad-phase AABB reject
        float dummy;
        if (!rayVsAABB(orig, dir, be->worldBounds(), dummy)) continue;

        for (std::size_t bi = 0; bi < be->brushes.size(); ++bi) {
            const auto& brush = be->brushes[bi];
            const auto& polys = brush.allFacePolygons();

            for (std::size_t fi = 0; fi < polys.size(); ++fi) {
                if (polys[fi].size() < 3) continue;

                // Transform face polygon to world space
                std::vector<glm::dvec3> worldPoly;
                worldPoly.reserve(polys[fi].size());
                for (const auto& lp : polys[fi])
                    worldPoly.push_back(be->transform.transformPoint(lp));

                float t;
                if (rayPolygon(orig, dir, worldPoly, t) && t < bestT) {
                    bestT = t;
                    best  = { id, bi, fi, t, orig + t * dir };
                }
            }
        }
    }

    if (best.valid()) return best;
    return std::nullopt;
}

} // namespace forge::editor
