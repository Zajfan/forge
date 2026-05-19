#pragma once

#include <forge/scene.hpp>
#include <forge/gfx/Material.hpp>
#include <expected>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <tuple>
#include <cstdio>

namespace forge::serial {

/// Current schema version of .forge project files.
inline constexpr const char* kForgeSchemaVersion = "0.2.0";

/// Parse a semver version string (e.g., "0.2.0") into (major, minor, patch).
/// @returns {major, minor, patch} tuple; all -1 on parse error.
inline std::tuple<int, int, int> parseVersion(std::string_view version) noexcept {
    int major = 0, minor = 0, patch = 0;
    int count = std::sscanf(version.data(), "%d.%d.%d", &major, &minor, &patch);
    return count == 3 ? std::make_tuple(major, minor, patch) : std::make_tuple(-1, -1, -1);
}

/// Compare two semver version strings.
/// @returns negative if a < b, 0 if a == b, positive if a > b.
inline int compareVersions(std::string_view a, std::string_view b) noexcept {
    auto [aMajor, aMinor, aPatch] = parseVersion(a);
    auto [bMajor, bMinor, bPatch] = parseVersion(b);
    if (aMajor < 0 || bMajor < 0) return 0; // parse error: treat as equal
    if (aMajor != bMajor) return aMajor - bMajor;
    if (aMinor != bMinor) return aMinor - bMinor;
    return aPatch - bPatch;
}

struct EditorMetadata {
    std::unordered_map<scene::EntityId, std::string> entityLayers;
    std::unordered_map<scene::EntityId, std::string> entityGroups;
    std::unordered_map<std::string, bool> layerVisibility;
    std::unordered_map<std::string, bool> layerLocked;
    std::unordered_map<std::string, glm::vec3> layerTint;
    std::string soloLayer;
    std::string groupFilter;
};

struct ProjectData {
    scene::Scene scene;
    gfx::MaterialLibrary materials;
    EditorMetadata editor;
    std::string schemaNote;
};

// ─── Scene file format ───────────────────────────────────────────────────────
//
// Files use the .forge extension.  Format is JSON (UTF-8, pretty-printed).
//
// Top-level structure:
//   {
//     "forge_version": "0.1.0",
//     "name": "...",
//     "author": "...",
//     "entities": { "<id>": { "type": "brush"|"point"|"mesh", ... }, ... },
//     "lighting":  { "ambientColor": [...], "sunDirection": [...], ... },
//     "fog":       { "density": 0.0, "color": [...] }
//   }
//
// A brush entity:
//   { "type":"brush", "name":"...", "classname":"...",
//     "solid":true, "visible":true,
//     "transform": { "translation":[...], "rotation":[...], "scale":[...] },
//     "properties": { "key": {"t":"s","v":"..."}, ... },
//     "brushes": [ { "id":"...", "faces": [ { "plane":{"normal":[...],"distance":0},
//                    "materialId":"...", "uvOffset":[0,0], "uvScale":[1,1],
//                    "uvRotation":0 }, ... ] }, ... ] }
//
// A point entity:
//   { "type":"point", "name":"...", "classname":"...",
//     "transform": { ... },
//     "properties": { "key": {"t":"f","v":300.0}, ... } }
//     (t = "s"|"i"|"f"|"b"|"v3")
// ─────────────────────────────────────────────────────────────────────────────

/// Serialise a Scene to JSON and write to disk.
/// @returns empty string on success, error message on failure.
[[nodiscard]] std::string saveScene(
    const scene::Scene&          scene,
    const std::filesystem::path& path) noexcept;

/// Deserialise a Scene from a .forge JSON file.
/// @returns Scene on success, error message on failure.
[[nodiscard]] std::expected<scene::Scene, std::string>
loadScene(const std::filesystem::path& path) noexcept;

/// Serialise a full editor project (scene + material library) to disk.
/// @returns empty string on success, error message on failure.
[[nodiscard]] std::string saveProject(
    const scene::Scene&          scene,
    const gfx::MaterialLibrary&  materials,
    const EditorMetadata&        editor,
    const std::filesystem::path& path) noexcept;

/// Deserialise a full editor project (scene + material library) from disk.
/// Scene-only files are supported; material library will be empty in that case.
[[nodiscard]] std::expected<ProjectData, std::string>
loadProject(const std::filesystem::path& path) noexcept;

} // namespace forge::serial
