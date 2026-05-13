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
    std::string            version = "1.0.0";
    
    // Entity metadata (NEW in v1.0.0)
    bool                   solid = true;
    bool                   visible = true;
    std::string            layer = "default";
};

// ─── Instantiation Options ─────────────────────────────────────────────────────

/// Options for placing a prefab instance in the scene.
struct PrefabInstantiationOptions {
    glm::dvec3             position     = {};
    glm::dquat             rotation     = glm::identity<glm::dquat>();
    glm::dvec3             scale        = {1.0, 1.0, 1.0};
    
    // Entity state overrides (if set, override prefab defaults)
    std::optional<bool>           solid_override    = std::nullopt;
    std::optional<bool>           visible_override  = std::nullopt;
    std::optional<std::string>    layer_override    = std::nullopt;
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

/// Build a scene BrushEntity from a prefab, placed with the given options.
///
/// The returned entity has been transformed by the instantiation options.
/// If no options provided, defaults to world origin with identity rotation/scale.
[[nodiscard]] scene::BrushEntity instantiatePrefab(
    const Prefab& prefab,
    const PrefabInstantiationOptions& opts = {}) noexcept;

/// Build a scene BrushEntity from a prefab at a simple world position (backward compat).
///
/// Equivalent to: instantiatePrefab(prefab, PrefabInstantiationOptions{.position = worldPosition})
[[nodiscard]] inline scene::BrushEntity instantiatePrefab(
    const Prefab& prefab,
    glm::dvec3    worldPosition) noexcept {
    return instantiatePrefab(prefab, PrefabInstantiationOptions{.position = worldPosition});
}

} // namespace forge::editor
