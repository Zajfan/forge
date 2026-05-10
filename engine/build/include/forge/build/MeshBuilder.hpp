#pragma once

#include "MeshData.hpp"
#include <forge/scene.hpp>

namespace forge::build {

// ─── UV projection ────────────────────────────────────────────────────────────

/// Compute planar UV for a world-space position given a face normal and mapping
/// parameters. Uses dominant-axis projection (the same convention as Quake/
/// Valve MAP format UV projection).
[[nodiscard]] glm::vec2 planarUV(
    glm::vec3 worldPos,
    glm::vec3 faceNormal,
    glm::vec2 uvOffset  = {0.f, 0.f},
    glm::vec2 uvScale   = {1.f, 1.f},
    float     uvRotDeg  = 0.f) noexcept;

// ─── Single-face mesh ─────────────────────────────────────────────────────────

/// Build a triangulated mesh for one face of a brush.
/// Vertices are in the brush's LOCAL space.
/// Returns an empty MeshData if the face polygon has fewer than 3 vertices.
[[nodiscard]] MeshData buildFaceMesh(
    const geo::Brush& brush,
    std::size_t       faceIdx) noexcept;

// ─── Brush mesh ───────────────────────────────────────────────────────────────

/// Build a complete EntityMesh for a single brush — one submesh per unique
/// materialId. Vertices are in the brush's LOCAL space.
[[nodiscard]] EntityMesh buildBrushMesh(const geo::Brush& brush) noexcept;

// ─── Entity mesh ──────────────────────────────────────────────────────────────

/// Build a complete EntityMesh for a BrushEntity — all brushes merged, grouped
/// by materialId.
///
/// @param entity         The BrushEntity to build from.
/// @param applyTransform If true, vertex positions are in WORLD space
///                       (entity transform applied). If false, LOCAL space.
[[nodiscard]] EntityMesh buildEntityMesh(
    const scene::BrushEntity& entity,
    bool                      applyTransform = true) noexcept;

// ─── Scene mesh ───────────────────────────────────────────────────────────────

/// Build one EntityMesh per visible BrushEntity in the scene.
/// Returns {EntityId, EntityMesh} pairs in scene entity order.
[[nodiscard]] std::vector<std::pair<scene::EntityId, EntityMesh>>
buildSceneMeshes(const scene::Scene& scene) noexcept;

} // namespace forge::build
