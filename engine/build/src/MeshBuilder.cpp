#include "forge/build/MeshBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <unordered_map>

namespace forge::build {

// ─── UV projection ────────────────────────────────────────────────────────────

glm::vec2 planarUV(
    glm::vec3 worldPos,
    glm::vec3 faceNormal,
    glm::vec2 uvOffset,
    glm::vec2 uvScale,
    float     uvRotDeg) noexcept
{
    // Pick UV axes based on the dominant component of the face normal.
    // This matches the Quake MAP format planar UV convention.
    const glm::vec3 absN = glm::abs(faceNormal);

    glm::vec3 uAxis, vAxis;
    if (absN.x >= absN.y && absN.x >= absN.z) {
        // X-dominant → project onto YZ plane
        uAxis = { 0.f, 1.f, 0.f };
        vAxis = { 0.f, 0.f, 1.f };
    } else if (absN.y >= absN.x && absN.y >= absN.z) {
        // Y-dominant → project onto XZ plane
        uAxis = { 1.f, 0.f, 0.f };
        vAxis = { 0.f, 0.f, 1.f };
    } else {
        // Z-dominant → project onto XY plane
        uAxis = { 1.f, 0.f, 0.f };
        vAxis = { 0.f, 1.f, 0.f };
    }

    // Apply UV rotation in the face plane
    if (std::abs(uvRotDeg) > 0.001f) {
        const float rad = glm::radians(uvRotDeg);
        const float c   = std::cos(rad);
        const float s   = std::sin(rad);
        const glm::vec3 ru = c * uAxis - s * vAxis;
        const glm::vec3 rv = s * uAxis + c * vAxis;
        uAxis = ru;
        vAxis = rv;
    }

    // Project and apply scale + offset
    const float u = (glm::dot(worldPos, uAxis) / uvScale.x) + uvOffset.x;
    const float v = (glm::dot(worldPos, vAxis) / uvScale.y) + uvOffset.y;
    return { u, v };
}

// ─── buildFaceMesh ────────────────────────────────────────────────────────────

MeshData buildFaceMesh(const geo::Brush& brush, std::size_t faceIdx) noexcept {
    const auto& face = brush.faces[faceIdx];
    const auto& poly = brush.facePolygon(faceIdx);

    if (poly.size() < 3) return {};

    MeshData mesh;
    mesh.materialId = face.materialId;

    const glm::vec3 normal = glm::vec3(face.plane.normal);
    const int n = static_cast<int>(poly.size());

    // Emit one vertex per polygon vertex
    for (const auto& localPos : poly) {
        const glm::vec3 pos = glm::vec3(localPos);
        const glm::vec2 uv  = planarUV(pos, normal,
                                        face.uvOffset, face.uvScale,
                                        face.uvRotation);
        mesh.vertices.push_back({ pos, normal, uv });
        mesh.bounds.expand(geo::AABB{ glm::dvec3(pos), glm::dvec3(pos) });
    }

    // Fan triangulate
    for (int i = 1; i < n - 1; ++i) {
        mesh.indices.push_back(0);
        mesh.indices.push_back(static_cast<uint32_t>(i));
        mesh.indices.push_back(static_cast<uint32_t>(i + 1));
    }

    return mesh;
}

// ─── buildBrushMesh ──────────────────────────────────────────────────────────

EntityMesh buildBrushMesh(const geo::Brush& brush) noexcept {
    // Accumulate vertices+indices per material
    std::map<std::string, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> byMaterial;

    const auto& polys = brush.allFacePolygons();

    for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
        const auto& face = brush.faces[fi];
        const auto& poly = polys[fi];
        if (poly.size() < 3) continue;

        const glm::vec3 normal = glm::vec3(face.plane.normal);
        auto& [verts, indices] = byMaterial[face.materialId];

        // Polygon vertices
        const uint32_t base = static_cast<uint32_t>(verts.size());
        for (const auto& localPos : poly) {
            const glm::vec3 pos = glm::vec3(localPos);
            const glm::vec2 uv  = planarUV(pos, normal,
                                            face.uvOffset, face.uvScale,
                                            face.uvRotation);
            verts.push_back({ pos, normal, uv });
        }

        // Fan triangulation
        const int n = static_cast<int>(poly.size());
        for (int i = 1; i < n - 1; ++i) {
            indices.push_back(base);
            indices.push_back(base + static_cast<uint32_t>(i));
            indices.push_back(base + static_cast<uint32_t>(i + 1));
        }
    }

    EntityMesh result;
    for (auto& [matId, pair] : byMaterial) {
        auto& [verts, indices] = pair;
        if (verts.empty()) continue;

        MeshData submesh;
        submesh.materialId = matId;
        submesh.vertices   = std::move(verts);
        submesh.indices    = std::move(indices);

        for (const auto& v : submesh.vertices)
            submesh.bounds.expand(geo::AABB{ glm::dvec3(v.position), glm::dvec3(v.position) });

        result.bounds.expand(submesh.bounds);
        result.submeshes.push_back(std::move(submesh));
    }

    return result;
}

// ─── buildEntityMesh ─────────────────────────────────────────────────────────

EntityMesh buildEntityMesh(
    const scene::BrushEntity& entity,
    bool applyTransform) noexcept
{
    // Accumulate per-material data across all brushes
    std::map<std::string, std::pair<std::vector<Vertex>, std::vector<uint32_t>>> byMaterial;

    const scene::Transform& tf = entity.transform;

    for (const auto& brush : entity.brushes) {
        const auto& polys = brush.allFacePolygons();

        for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
            const auto& face = brush.faces[fi];
            const auto& poly = polys[fi];
            if (poly.size() < 3) continue;

            // Face normal in the output space
            const glm::vec3 localNormal = glm::vec3(face.plane.normal);
            const glm::vec3 outNormal   = applyTransform
                ? glm::vec3(tf.transformNormal(face.plane.normal))
                : localNormal;

            auto& [verts, indices] = byMaterial[face.materialId];
            const uint32_t base = static_cast<uint32_t>(verts.size());

            for (const auto& localPos : poly) {
                // Position: apply transform if requested
                const glm::vec3 outPos = applyTransform
                    ? glm::vec3(tf.transformPoint(glm::dvec3(localPos)))
                    : glm::vec3(localPos);

                // UV: always computed in world space for consistent texel density
                const glm::vec3 uvPos = applyTransform ? outPos : glm::vec3(localPos);
                const glm::vec2 uv = planarUV(uvPos, outNormal,
                                               face.uvOffset, face.uvScale,
                                               face.uvRotation);

                verts.push_back({ outPos, outNormal, uv });
            }

            const int n = static_cast<int>(poly.size());
            for (int i = 1; i < n - 1; ++i) {
                indices.push_back(base);
                indices.push_back(base + static_cast<uint32_t>(i));
                indices.push_back(base + static_cast<uint32_t>(i + 1));
            }
        }
    }

    EntityMesh result;
    for (auto& [matId, pair] : byMaterial) {
        auto& [verts, indices] = pair;
        if (verts.empty()) continue;

        MeshData submesh;
        submesh.materialId = matId;
        submesh.vertices   = std::move(verts);
        submesh.indices    = std::move(indices);

        for (const auto& v : submesh.vertices)
            submesh.bounds.expand(geo::AABB{ glm::dvec3(v.position), glm::dvec3(v.position) });

        result.bounds.expand(submesh.bounds);
        result.submeshes.push_back(std::move(submesh));
    }

    return result;
}

// ─── buildSceneMeshes ─────────────────────────────────────────────────────────

std::vector<std::pair<scene::EntityId, EntityMesh>>
buildSceneMeshes(const scene::Scene& scene) noexcept {
    std::vector<std::pair<scene::EntityId, EntityMesh>> result;

    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->visible) continue;

        auto mesh = buildEntityMesh(*be, /*applyTransform=*/true);
        if (!mesh.empty())
            result.emplace_back(id, std::move(mesh));
    }

    return result;
}

} // namespace forge::build
