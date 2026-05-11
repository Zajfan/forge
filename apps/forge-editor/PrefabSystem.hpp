#pragma once

#include <forge/geo.hpp>
#include <forge/scene.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::editor {

// ─── Prefab ───────────────────────────────────────────────────────────────────

/// A named, reusable collection of brushes.
///
/// File extension: .fprefab  (JSON, subset of the scene format)
///
/// Prefabs are placed as new BrushEntities at the camera target position.
/// Each brush's geometry is in LOCAL space; the entity transform is applied
/// when the prefab is instantiated.
struct Prefab {
    std::string            name;
    std::vector<geo::Brush> brushes;
    std::string            description;
    std::string            author;
    std::string            version = "0.1.0";
};

// ─── Save / Load ─────────────────────────────────────────────────────────────

/// Serialise a BrushEntity's brushes as a .fprefab JSON file.
/// @returns Empty string on success, error description on failure.
[[nodiscard]] std::string savePrefab(
    const Prefab&                prefab,
    const std::filesystem::path& path) noexcept;

/// Load a .fprefab file.
/// @returns Prefab on success, error description on failure.
[[nodiscard]] std::expected<Prefab, std::string>
loadPrefab(const std::filesystem::path& path) noexcept;

/// Build a scene BrushEntity from a prefab, placed at the given world position.
[[nodiscard]] scene::BrushEntity instantiatePrefab(
    const Prefab&    prefab,
    glm::dvec3       worldPosition = {}) noexcept;

} // namespace forge::editor
