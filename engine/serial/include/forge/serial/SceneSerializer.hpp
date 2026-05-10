#pragma once

#include <forge/scene.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace forge::serial {

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
//   { "type":"brush", "name":"...", "solid":true, "visible":true,
//     "transform": { "translation":[...], "rotation":[...], "scale":[...] },
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

} // namespace forge::serial
