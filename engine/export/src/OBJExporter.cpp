#include "forge/export/OBJExporter.hpp"

#include <fstream>
#include <map>
#include <set>
#include <format>
#include <cmath>

namespace forge::export_ {

namespace {

// ─── Vertex welding ───────────────────────────────────────────────────────────

struct WeldedMesh {
    std::vector<glm::dvec3> positions;
    std::vector<glm::dvec3> normals;
    std::vector<glm::vec2>  uvs;

    struct Face {
        std::string materialId;
        // Each element: {posIdx, uvIdx, normalIdx} — 0-based
        std::vector<std::array<int, 3>> indices;
    };
    std::vector<Face> faces;
};

// Compute planar UV projection for a polygon vertex.
// Uses the dominant axis of the face normal to pick the projection plane.
static glm::vec2 planarUV(
    glm::dvec3 worldPos,
    glm::dvec3 faceNormal,
    glm::vec2  offset,
    glm::vec2  scale,
    float      rotationDeg,
    bool       flipV) noexcept
{
    const glm::dvec3 n = glm::abs(faceNormal);

    // Pick U and V axes based on dominant normal component
    glm::dvec3 uAxis, vAxis;
    if (n.x >= n.y && n.x >= n.z) {      // X-dominant: project onto YZ
        uAxis = { 0, 1, 0 };
        vAxis = { 0, 0, 1 };
    } else if (n.y >= n.x && n.y >= n.z) { // Y-dominant: project onto XZ
        uAxis = { 1, 0, 0 };
        vAxis = { 0, 0, 1 };
    } else {                               // Z-dominant: project onto XY
        uAxis = { 1, 0, 0 };
        vAxis = { 0, 1, 0 };
    }

    // Apply rotation in the face plane
    if (std::abs(rotationDeg) > 0.001f) {
        const double rad = glm::radians(static_cast<double>(rotationDeg));
        const double c   = std::cos(rad);
        const double s   = std::sin(rad);
        const glm::dvec3 ru = c * uAxis - s * vAxis;
        const glm::dvec3 rv = s * uAxis + c * vAxis;
        uAxis = ru;
        vAxis = rv;
    }

    float u = static_cast<float>(glm::dot(worldPos, uAxis));
    float v = static_cast<float>(glm::dot(worldPos, vAxis));

    u = (u / scale.x) + offset.x;
    v = (v / scale.y) + offset.y;

    if (flipV) v = -v;
    return { u, v };
}

// Fan-triangulate a convex polygon (indices into the polygon vertex list)
static std::vector<std::array<int, 3>>
triangulate(int polyVertCount) noexcept {
    std::vector<std::array<int, 3>> tris;
    tris.reserve(static_cast<std::size_t>(polyVertCount - 2));
    for (int i = 1; i < polyVertCount - 1; ++i)
        tris.push_back({ 0, i, i + 1 });
    return tris;
}

} // anonymous namespace

// ─── exportOBJ ───────────────────────────────────────────────────────────────

bool exportOBJ(
    const scene::Scene&          scene,
    const std::filesystem::path& outputPath,
    const OBJExportOptions&      options) noexcept
{
    const std::filesystem::path objPath = std::filesystem::path(outputPath).replace_extension(".obj");
    const std::filesystem::path mtlPath = std::filesystem::path(outputPath).replace_extension(".mtl");

    std::ofstream objFile(objPath);
    std::ofstream mtlFile(mtlPath);

    if (!objFile || !mtlFile) return false;

    // ── OBJ header ────────────────────────────────────────────────────────────
    objFile << "# FORGE OBJ Export\n";
    objFile << "# Scene: " << scene.name << "\n";
    objFile << "mtllib " << mtlPath.filename().string() << "\n\n";

    // ── MTL header ────────────────────────────────────────────────────────────
    mtlFile << "# FORGE MTL Export\n";
    mtlFile << "# Scene: " << scene.name << "\n\n";

    std::set<std::string> writtenMaterials;

    // Running vertex/uv/normal index counters (OBJ uses 1-based global indices)
    int vBase  = 1;
    int vtBase = 1;
    int vnBase = 1;

    // ── Per-entity export ─────────────────────────────────────────────────────
    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->visible) continue;

        const scene::Transform& tf = be->transform;

        objFile << "o " << be->name << "\n";

        for (const auto& brush : be->brushes) {
            const auto& polys = brush.allFacePolygons();

            for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                const auto& face = brush.faces[fi];
                const auto& poly = polys[fi];
                if (poly.size() < 3) continue;

                // World-space vertices and normal
                const glm::dvec3 worldNormal = options.applyTransforms
                    ? tf.transformNormal(face.plane.normal)
                    : face.plane.normal;

                const int vertCount = static_cast<int>(poly.size());

                // Emit vertices
                for (const auto& localPos : poly) {
                    const glm::dvec3 wp = options.applyTransforms
                        ? tf.transformPoint(localPos)
                        : localPos;
                    objFile << std::format("v {:.6f} {:.6f} {:.6f}\n",
                        wp.x * options.scale,
                        wp.y * options.scale,
                        wp.z * options.scale);
                }

                // Emit normal (one per face — flat shading)
                objFile << std::format("vn {:.6f} {:.6f} {:.6f}\n",
                    worldNormal.x, worldNormal.y, worldNormal.z);

                // Emit UVs
                for (const auto& localPos : poly) {
                    const glm::dvec3 wp = options.applyTransforms
                        ? tf.transformPoint(localPos)
                        : localPos;
                    const glm::vec2 uv = planarUV(
                        wp, worldNormal,
                        face.uvOffset, face.uvScale, face.uvRotation,
                        options.flipV);
                    objFile << std::format("vt {:.6f} {:.6f}\n", uv.x, uv.y);
                }

                // Material
                const std::string& mat = face.materialId;
                objFile << "usemtl " << mat << "\n";

                // Write material to MTL if not already done
                if (writtenMaterials.insert(mat).second) {
                    mtlFile << "newmtl " << mat << "\n";
                    mtlFile << "Ka 0.2 0.2 0.2\n";
                    mtlFile << "Kd 0.8 0.8 0.8\n";
                    mtlFile << "Ks 0.0 0.0 0.0\n";
                    mtlFile << "d 1.0\n";
                    mtlFile << "illum 1\n";
                    // Assume texture file = materialId + .png if it contains no path
                    if (mat.find('/') == std::string::npos &&
                        mat.find('\\') == std::string::npos) {
                        mtlFile << "map_Kd " << mat << ".png\n";
                    } else {
                        mtlFile << "map_Kd " << mat << "\n";
                    }
                    mtlFile << "\n";
                }

                // Emit face triangles (fan triangulation)
                const int vb  = vBase;
                const int vnb = vnBase;

                for (const auto& [a, b, c] : triangulate(vertCount)) {
                    // OBJ face format: v/vt/vn (1-based)
                    objFile << std::format("f {0}/{0}/{3} {1}/{1}/{3} {2}/{2}/{3}\n",
                        vb + a, vb + b, vb + c, vnb);
                }

                vBase  += vertCount;
                vtBase += vertCount;
                vnBase += 1;
            }
        }

        objFile << "\n";
    }

    return objFile.good() && mtlFile.good();
}

} // namespace forge::export_
