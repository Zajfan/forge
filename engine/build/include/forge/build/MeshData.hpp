#pragma once

#include <forge/geo/Math.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace forge::build {

// ─── Vertex ───────────────────────────────────────────────────────────────────

/// Interleaved vertex layout: 32 bytes, one cache line.
///
/// GPU attribute bindings (matches brush.vert):
///   location 0 → position  (vec3, offset  0)
///   location 1 → normal    (vec3, offset 12)
///   location 2 → uv        (vec2, offset 24)
#pragma pack(push, 1)
struct Vertex {
    glm::vec3 position;  //  0..11   12 bytes
    glm::vec3 normal;    // 12..23   12 bytes
    glm::vec2 uv;        // 24..31    8 bytes
};                       // total:   32 bytes
#pragma pack(pop)

static_assert(sizeof(Vertex) == 32, "Vertex must be exactly 32 bytes");

// ─── MeshData ─────────────────────────────────────────────────────────────────

/// A single-material CPU-side mesh ready for GPU upload.
///
/// Triangulated: every 3 indices form one triangle.
/// Indices are uint32 so meshes can exceed 65535 vertices.
struct MeshData {
    std::vector<Vertex>    vertices;
    std::vector<uint32_t>  indices;
    std::string            materialId;  ///< matches BrushFace::materialId
    geo::AABB              bounds;      ///< tight AABB in the mesh's local space

    [[nodiscard]] bool        empty()         const noexcept { return vertices.empty(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
    [[nodiscard]] std::size_t vertexCount()   const noexcept { return vertices.size(); }
};

// ─── EntityMesh ───────────────────────────────────────────────────────────────

/// All geometry for one entity, split into per-material submeshes.
/// Each submesh maps to one draw call (one material bind).
struct EntityMesh {
    std::vector<MeshData> submeshes;  ///< one entry per unique materialId
    geo::AABB             bounds;     ///< union of all submesh bounds

    [[nodiscard]] bool        empty()         const noexcept { return submeshes.empty(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept {
        std::size_t n = 0;
        for (const auto& s : submeshes) n += s.triangleCount();
        return n;
    }
    [[nodiscard]] std::size_t vertexCount() const noexcept {
        std::size_t n = 0;
        for (const auto& s : submeshes) n += s.vertexCount();
        return n;
    }
};

} // namespace forge::build
