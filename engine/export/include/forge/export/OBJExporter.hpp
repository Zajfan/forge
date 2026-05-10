#pragma once

#include <forge/scene.hpp>

#include <filesystem>
#include <string>

namespace forge::export_ {

/// Options controlling OBJ export behaviour.
struct OBJExportOptions {
    /// Apply entity transforms to produce world-space geometry.
    /// If false, brush geometry is exported in local entity space.
    bool applyTransforms = true;

    /// Merge all brushes from all BrushEntities into a single mesh object.
    /// If false, each BrushEntity becomes a separate OBJ object (o …).
    bool mergeEntities = false;

    /// Weld vertices that are within this distance of each other.
    /// 0.0 = no welding (each polygon emits its own vertices).
    double weldEpsilon = 1e-4;

    /// Flip UV V coordinate (some tools expect 0 at top, others at bottom).
    bool flipV = false;

    /// Scale factor applied to all coordinates on export.
    double scale = 1.0;
};

/// Export a Scene to Wavefront OBJ format.
///
/// Writes two files:
///   <outputPath>.obj  — geometry (vertices, normals, UVs, face definitions)
///   <outputPath>.mtl  — material library (one entry per unique materialId)
///
/// All brush face polygons are triangulated via fan triangulation.
/// Face normals come directly from the brush plane normal (exact, not averaged).
///
/// @param scene       The scene to export.
/// @param outputPath  Output file path WITHOUT extension (e.g. "output/mymap").
///                    Extensions .obj and .mtl are appended automatically.
/// @param options     Export behaviour options.
///
/// @returns true on success, false if the file could not be written.
[[nodiscard]] bool exportOBJ(
    const scene::Scene&      scene,
    const std::filesystem::path& outputPath,
    const OBJExportOptions&  options = {}) noexcept;

} // namespace forge::export_
