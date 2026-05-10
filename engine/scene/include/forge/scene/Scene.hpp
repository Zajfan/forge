#pragma once

#include "Entity.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace forge::scene {

// ─── World settings ───────────────────────────────────────────────────────────

struct FogSettings {
    glm::vec3 color   = { 0.5f, 0.5f, 0.5f };
    float     density = 0.f;   ///< 0 = no fog
    float     start   = 256.f;
    float     end     = 2048.f;
};

struct LightingSettings {
    glm::vec3 ambientColor     = { 0.05f, 0.05f, 0.05f };
    glm::vec3 sunDirection     = { -0.5f, -1.0f, -0.5f };
    glm::vec3 sunColor         = { 1.0f, 0.95f, 0.8f };
    float     sunIntensity     = 1.0f;
};

// ─── Scene ────────────────────────────────────────────────────────────────────

/// Container for all entities, world settings, and scene metadata.
///
/// Entities are owned by the scene and addressed by EntityId (uint64).
/// IDs are monotonically increasing and never reused within a scene session.
///
/// The underlying storage is `std::map<EntityId, Entity>` — ordered for
/// deterministic iteration order (important for reproducible exports).
struct Scene {
    std::string     name    = "untitled";
    std::string     author;
    std::string     version = "0.1.0";

    // World physics and rendering settings
    glm::vec3       gravity = { 0.f, -9.81f, 0.f };
    FogSettings     fog;
    LightingSettings lighting;

    // ── Entity storage ────────────────────────────────────────────────────────

    std::map<EntityId, Entity> entities;

    // ── Entity lifecycle ──────────────────────────────────────────────────────

    /// Add an entity and return its assigned ID.
    EntityId addEntity(Entity e) noexcept;

    /// Remove an entity by ID. No-op if ID doesn't exist.
    void removeEntity(EntityId id) noexcept;

    /// Return a pointer to an entity, or nullptr if not found.
    [[nodiscard]] Entity*       getEntity(EntityId id) noexcept;
    [[nodiscard]] const Entity* getEntity(EntityId id) const noexcept;

    /// True if the scene contains an entity with this ID.
    [[nodiscard]] bool hasEntity(EntityId id) const noexcept {
        return entities.contains(id);
    }

    [[nodiscard]] std::size_t entityCount() const noexcept {
        return entities.size();
    }

    // ── Typed queries ─────────────────────────────────────────────────────────

    /// Return all entities of a specific type, paired with their IDs.
    ///
    ///   auto brushes = scene.entitiesOfType<BrushEntity>();
    ///   for (auto& [id, entity] : brushes) { … }
    template<typename T>
    [[nodiscard]] std::vector<std::pair<EntityId, T*>> entitiesOfType() noexcept {
        std::vector<std::pair<EntityId, T*>> result;
        for (auto& [id, entity] : entities) {
            if (T* ptr = std::get_if<T>(&entity))
                result.emplace_back(id, ptr);
        }
        return result;
    }

    template<typename T>
    [[nodiscard]] std::vector<std::pair<EntityId, const T*>> entitiesOfType() const noexcept {
        std::vector<std::pair<EntityId, const T*>> result;
        for (const auto& [id, entity] : entities) {
            if (const T* ptr = std::get_if<T>(&entity))
                result.emplace_back(id, ptr);
        }
        return result;
    }

    /// Return the first PointEntity with the given classname, or nullptr.
    [[nodiscard]] std::pair<EntityId, PointEntity*>
    findByClassname(const std::string& classname) noexcept;

    /// Return all PointEntities with the given classname.
    [[nodiscard]] std::vector<std::pair<EntityId, PointEntity*>>
    findAllByClassname(const std::string& classname) noexcept;

    // ── World-space AABB ──────────────────────────────────────────────────────

    /// Tight AABB enclosing all BrushEntity geometry in world space.
    [[nodiscard]] geo::AABB worldBounds() const noexcept;

    // ── Statistics ────────────────────────────────────────────────────────────

    struct Stats {
        std::size_t brushEntityCount  = 0;
        std::size_t pointEntityCount  = 0;
        std::size_t meshEntityCount   = 0;
        std::size_t totalBrushCount   = 0;
        std::size_t totalFaceCount    = 0;
    };

    [[nodiscard]] Stats stats() const noexcept;

    // ── Clear ─────────────────────────────────────────────────────────────────

    void clear() noexcept { entities.clear(); nextId_ = 1; }

private:
    EntityId nextId_ = 1;
};

} // namespace forge::scene
