#pragma once

#include "Transform.hpp"
#include <forge/geo.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace forge::scene {

// ─── EntityId ─────────────────────────────────────────────────────────────────

using EntityId = uint64_t;
inline constexpr EntityId kInvalidEntityId = 0;

// ─── Property value ───────────────────────────────────────────────────────────

/// Typed property value for PointEntity key-value store.
/// Maps cleanly to Quake .map / FGD property types.
using PropertyValue = std::variant<
    std::string,
    int,
    float,
    bool,
    glm::vec3   // for colour / vector properties
>;

// ─── BrushEntity ─────────────────────────────────────────────────────────────

/// A solid or non-solid world geometry entity made of one or more convex brushes.
///
/// Brushes are stored in LOCAL space. Apply `transform` to convert to world space.
///
/// In the compiler/build pipeline:
///   - solid=true  → physics collider generated
///   - solid=false → render-only (clip brushes, trigger volumes, etc.)
struct BrushEntity {
    std::string              name      = "brush";
    std::string              classname;
    Transform                transform;
    std::vector<geo::Brush>  brushes;
    bool                     solid   = true;
    bool                     visible = true;
    std::string              layer   = "default";
    std::unordered_map<std::string, PropertyValue> properties;

    template<typename T>
    [[nodiscard]] T property(const std::string& key, T defaultValue = T{}) const noexcept {
        if (const auto it = properties.find(key); it != properties.end()) {
            if (const T* val = std::get_if<T>(&it->second))
                return *val;
        }
        return defaultValue;
    }

    template<typename T>
    void set(const std::string& key, T value) {
        properties[key] = PropertyValue{ std::move(value) };
    }

    /// Tight world-space AABB covering all brushes.
    [[nodiscard]] geo::AABB worldBounds() const noexcept;

    /// Total number of faces across all brushes.
    [[nodiscard]] std::size_t faceCount() const noexcept;
};

// ─── PointEntity ─────────────────────────────────────────────────────────────

/// A positioned entity with no geometry — lights, spawn points, triggers,
/// game-logic objects. Fully described by its classname and key-value properties.
///
/// Standard classnames: "light", "player_start", "info_player_start",
///                      "trigger_once", "func_door", "target_speaker", …
struct PointEntity {
    std::string                              name;
    std::string                              classname = "info_null";
    Transform                                transform;
    std::unordered_map<std::string, PropertyValue> properties;

    /// Get a typed property, returning a default if absent or wrong type.
    template<typename T>
    [[nodiscard]] T property(const std::string& key, T defaultValue = T{}) const noexcept {
        if (const auto it = properties.find(key); it != properties.end()) {
            if (const T* val = std::get_if<T>(&it->second))
                return *val;
        }
        return defaultValue;
    }

    /// Set a property value.
    template<typename T>
    void set(const std::string& key, T value) {
        properties[key] = PropertyValue{ std::move(value) };
    }
};

// ─── MeshEntity ──────────────────────────────────────────────────────────────

/// A static or animated mesh asset placed in the world.
/// The asset itself (geometry, materials) lives in the asset manager;
/// this entity just holds a reference by path and its world transform.
struct MeshEntity {
    std::string name;
    Transform   transform;
    std::string assetPath;   ///< Relative path to the mesh asset (GLTF, OBJ, …)
    bool        castShadow   = true;
    bool        visible      = true;
};

// ─── Entity (discriminated union) ────────────────────────────────────────────

using Entity = std::variant<BrushEntity, PointEntity, MeshEntity>;

/// Return the entity's name regardless of its concrete type.
[[nodiscard]] inline const std::string& entityName(const Entity& e) noexcept {
    return std::visit([](const auto& x) -> const std::string& { return x.name; }, e);
}

/// Return the entity's transform regardless of its concrete type.
[[nodiscard]] inline const Transform& entityTransform(const Entity& e) noexcept {
    return std::visit([](const auto& x) -> const Transform& { return x.transform; }, e);
}

} // namespace forge::scene
