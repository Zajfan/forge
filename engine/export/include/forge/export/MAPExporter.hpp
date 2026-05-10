#pragma once

#include <forge/scene.hpp>

#include <filesystem>
#include <string>

namespace forge::export_ {

/// Options controlling Quake MAP export behaviour.
struct MAPExportOptions {
    /// Quake MAP format version.
    /// 220 = Valve 220 format (uses UV vectors, supported by TrenchBroom).
    /// 100 = Standard Quake format (offset/scale/rotation UVs).
    int mapVersion = 220;

    /// Default texture name for faces without a materialId.
    std::string defaultTexture = "__TB_empty";

    /// Coordinate scale: FORGE units → Quake units.
    /// Quake uses 1 unit = ~2.54cm. 1.0 = no conversion.
    double scale = 1.0;

    /// If true, wrap BrushEntities in a "worldspawn" entity (required by BSP
    /// compilers). If false, every BrushEntity becomes a separate func_brush.
    bool useWorldspawn = true;
};

/// Export a Scene to Quake .map format.
///
/// The .map format stores brush geometry as raw plane equations (3-point form),
/// making it the most lossless export — since FORGE uses the same plane model.
///
/// Compatible with: TrenchBroom, JACK, GTKRadiant, NetRadiant, ericw-tools.
///
/// @param scene       The scene to export.
/// @param outputPath  Output file path (including .map extension).
/// @param options     Export behaviour options.
///
/// @returns true on success, false if the file could not be written.
[[nodiscard]] bool exportMAP(
    const scene::Scene&          scene,
    const std::filesystem::path& outputPath,
    const MAPExportOptions&      options = {}) noexcept;

} // namespace forge::export_
