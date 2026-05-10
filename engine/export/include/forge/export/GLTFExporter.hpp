#pragma once

#include <forge/scene.hpp>
#include <filesystem>
#include <string>

namespace forge::export_ {

struct GLTFExportOptions {
    bool   binary        = true;    ///< true = .glb, false = .gltf + .bin
    bool   embedTextures = false;   ///< embed base-colour PNGs (not yet supported)
    double scale         = 1.0;     ///< coordinate scale factor
    bool   applyTransforms = true;  ///< bake entity transforms into vertices
};

/// Export a Scene to GLTF 2.0 / GLB.
///
/// Each BrushEntity becomes one GLTF node + mesh (merged by material into primitives).
/// Each unique materialId becomes a PBR material with a flat metallic=0 / roughness=0.8
/// base colour derived from the material name hash.
/// PointEntities are emitted as empty nodes with custom extras (classname + properties).
///
/// @param scene       Source scene.
/// @param outputPath  Output path. Extension is replaced with .glb or .gltf automatically.
/// @returns Empty string on success, error description on failure.
[[nodiscard]] std::string exportGLTF(
    const scene::Scene&          scene,
    const std::filesystem::path& outputPath,
    const GLTFExportOptions&     options = {}) noexcept;

} // namespace forge::export_
