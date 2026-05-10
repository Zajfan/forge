#include "forge/export/MAPExporter.hpp"

#include <fstream>
#include <format>
#include <cmath>
#include <array>

namespace forge::export_ {

namespace {

// ─── Three-point form ─────────────────────────────────────────────────────────
//
// The Quake MAP format defines each face as three points on the plane.
// The cross product (p1-p0) × (p2-p0) gives the outward normal.
// We use a CCW winding viewed from the front (outside), matching our convention.

std::array<glm::dvec3, 3>
threePointsFromPlane(const geo::Plane& plane, double spacing = 16.0) noexcept {
    const glm::dvec3& n = plane.normal;

    // Build a perpendicular basis on the plane surface
    glm::dvec3 helper;
    if (std::abs(n.x) < 0.9)
        helper = { 1.0, 0.0, 0.0 };
    else
        helper = { 0.0, 1.0, 0.0 };

    const glm::dvec3 u = glm::normalize(glm::cross(n, helper));
    const glm::dvec3 v = glm::cross(n, u); // already unit length

    // Point on the plane
    const glm::dvec3 origin = n * plane.distance;

    // Three well-spaced points, CCW viewed from outside (cross(u,v) = n)
    return {
        origin,
        origin + u * spacing,
        origin + v * spacing,
    };
}

// Write a MAP face line (Standard Quake format)
void writeFaceStandard(
    std::ostream& out,
    const geo::BrushFace& face,
    double scale,
    const std::string& defaultTex) noexcept
{
    const auto [p0, p1, p2] = threePointsFromPlane(face.plane);

    const std::string& tex = face.materialId.empty() ? defaultTex : face.materialId;

    // Extract the final component of the material path (texture name only)
    std::string texName = tex;
    if (const auto pos = texName.find_last_of("/\\"); pos != std::string::npos)
        texName = texName.substr(pos + 1);

    out << std::format(
        "( {:.3f} {:.3f} {:.3f} ) "
        "( {:.3f} {:.3f} {:.3f} ) "
        "( {:.3f} {:.3f} {:.3f} ) "
        "{} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f}\n",
        p0.x * scale, p0.y * scale, p0.z * scale,
        p1.x * scale, p1.y * scale, p1.z * scale,
        p2.x * scale, p2.y * scale, p2.z * scale,
        texName,
        static_cast<double>(face.uvOffset.x),
        static_cast<double>(face.uvOffset.y),
        static_cast<double>(face.uvRotation),
        static_cast<double>(face.uvScale.x),
        static_cast<double>(face.uvScale.y));
}

// Write a MAP face line (Valve 220 format — UV vectors)
void writeFaceValve220(
    std::ostream& out,
    const geo::BrushFace& face,
    double scale,
    const std::string& defaultTex) noexcept
{
    const auto [p0, p1, p2] = threePointsFromPlane(face.plane);
    const glm::dvec3& n = face.plane.normal;

    // Compute UV axis vectors (same logic as OBJ planar projection)
    glm::dvec3 helper;
    if (std::abs(n.x) < 0.9)
        helper = { 1.0, 0.0, 0.0 };
    else
        helper = { 0.0, 1.0, 0.0 };

    const glm::dvec3 uAxis = glm::normalize(glm::cross(n, helper));
    const glm::dvec3 vAxis = glm::cross(n, uAxis);

    const std::string& tex = face.materialId.empty() ? defaultTex : face.materialId;
    std::string texName = tex;
    if (const auto pos = texName.find_last_of("/\\"); pos != std::string::npos)
        texName = texName.substr(pos + 1);

    // Valve 220:
    //   ( p0 ) ( p1 ) ( p2 ) TEXTURE [ uX uY uZ offset ] [ vX vY vZ offset ] rotation xscale yscale
    out << std::format(
        "( {:.3f} {:.3f} {:.3f} ) "
        "( {:.3f} {:.3f} {:.3f} ) "
        "( {:.3f} {:.3f} {:.3f} ) "
        "{} "
        "[ {:.6f} {:.6f} {:.6f} {:.3f} ] "
        "[ {:.6f} {:.6f} {:.6f} {:.3f} ] "
        "{:.3f} {:.3f} {:.3f}\n",
        p0.x * scale, p0.y * scale, p0.z * scale,
        p1.x * scale, p1.y * scale, p1.z * scale,
        p2.x * scale, p2.y * scale, p2.z * scale,
        texName,
        uAxis.x, uAxis.y, uAxis.z, static_cast<double>(face.uvOffset.x),
        vAxis.x, vAxis.y, vAxis.z, static_cast<double>(face.uvOffset.y),
        static_cast<double>(face.uvRotation),
        static_cast<double>(face.uvScale.x),
        static_cast<double>(face.uvScale.y));
}

void writeBrush(
    std::ostream& out,
    const geo::Brush& brush,
    int brushIdx,
    const MAPExportOptions& options) noexcept
{
    out << "// brush " << brushIdx << "\n";
    out << "{\n";
    for (const auto& face : brush.faces) {
        out << "  ";
        if (options.mapVersion == 220)
            writeFaceValve220(out, face, options.scale, options.defaultTexture);
        else
            writeFaceStandard(out, face, options.scale, options.defaultTexture);
    }
    out << "}\n";
}

} // anonymous namespace

// ─── exportMAP ───────────────────────────────────────────────────────────────

bool exportMAP(
    const scene::Scene&          scene,
    const std::filesystem::path& outputPath,
    const MAPExportOptions&      options) noexcept
{
    std::ofstream out(outputPath);
    if (!out) return false;

    out << "// FORGE MAP Export\n";
    out << "// Scene: " << scene.name << "\n";
    out << "// Format: Valve " << options.mapVersion << "\n\n";

    int globalBrushIdx = 0;

    // ── worldspawn entity ─────────────────────────────────────────────────────
    if (options.useWorldspawn) {
        out << "// entity 0\n";
        out << "{\n";
        out << "\"classname\" \"worldspawn\"\n";
        out << "\"_forge_scene\" \"" << scene.name << "\"\n";
        out << "\n";

        for (const auto& [id, entity] : scene.entities) {
            if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {
                if (!be->solid) continue; // non-solid brushes go in func_illusionary
                for (const auto& brush : be->brushes) {
                    writeBrush(out, brush, globalBrushIdx++, options);
                    out << "\n";
                }
            }
        }
        out << "}\n\n";
    }

    // ── Non-solid brush entities → func_illusionary ───────────────────────────
    int entityIdx = 1;
    for (const auto& [id, entity] : scene.entities) {
        if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {
            if (be->solid && options.useWorldspawn) continue;

            out << "// entity " << entityIdx++ << "\n";
            out << "{\n";
            out << "\"classname\" \"" << (be->solid ? "func_brush" : "func_illusionary") << "\"\n";
            out << "\"_forge_name\" \"" << be->name << "\"\n";
            out << "\n";

            for (const auto& brush : be->brushes) {
                writeBrush(out, brush, globalBrushIdx++, options);
                out << "\n";
            }
            out << "}\n\n";
        }
    }

    // ── Point entities ────────────────────────────────────────────────────────
    for (const auto& [id, entity] : scene.entities) {
        if (const auto* pe = std::get_if<scene::PointEntity>(&entity)) {
            const glm::dvec3 pos = pe->transform.translation * options.scale;

            out << "// entity " << entityIdx++ << "\n";
            out << "{\n";
            out << "\"classname\" \"" << pe->classname << "\"\n";
            out << std::format("\"origin\" \"{:.3f} {:.3f} {:.3f}\"\n",
                pos.x, pos.y, pos.z);

            for (const auto& [key, val] : pe->properties) {
                std::visit([&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        out << "\"" << key << "\" \"" << v << "\"\n";
                    else if constexpr (std::is_same_v<T, int>)
                        out << "\"" << key << "\" \"" << v << "\"\n";
                    else if constexpr (std::is_same_v<T, float>)
                        out << std::format("\"{}\" \"{:.3f}\"\n", key, v);
                    else if constexpr (std::is_same_v<T, bool>)
                        out << "\"" << key << "\" \"" << (v ? "1" : "0") << "\"\n";
                    else if constexpr (std::is_same_v<T, glm::vec3>)
                        out << std::format("\"{}\" \"{:.3f} {:.3f} {:.3f}\"\n",
                            key, v.x, v.y, v.z);
                }, val);
            }
            out << "}\n\n";
        }
    }

    return out.good();
}

} // namespace forge::export_
