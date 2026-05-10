#include "forge/scene/Scene.hpp"

namespace forge::scene {

// ─── BrushEntity ─────────────────────────────────────────────────────────────

geo::AABB BrushEntity::worldBounds() const noexcept {
    geo::AABB box;
    for (const auto& brush : brushes) {
        for (const auto& localVert : brush.vertices()) {
            box.expand(transform.transformPoint(localVert));
        }
    }
    return box;
}

std::size_t BrushEntity::faceCount() const noexcept {
    std::size_t count = 0;
    for (const auto& b : brushes) count += b.faces.size();
    return count;
}

// ─── Scene — entity lifecycle ────────────────────────────────────────────────

EntityId Scene::addEntity(Entity e) noexcept {
    const EntityId id = nextId_++;
    entities.emplace(id, std::move(e));
    return id;
}

void Scene::removeEntity(EntityId id) noexcept {
    entities.erase(id);
}

Entity* Scene::getEntity(EntityId id) noexcept {
    if (const auto it = entities.find(id); it != entities.end())
        return &it->second;
    return nullptr;
}

const Entity* Scene::getEntity(EntityId id) const noexcept {
    if (const auto it = entities.find(id); it != entities.end())
        return &it->second;
    return nullptr;
}

// ─── Scene — typed queries ────────────────────────────────────────────────────

std::pair<EntityId, PointEntity*>
Scene::findByClassname(const std::string& classname) noexcept {
    for (auto& [id, entity] : entities) {
        if (auto* pe = std::get_if<PointEntity>(&entity)) {
            if (pe->classname == classname)
                return { id, pe };
        }
    }
    return { kInvalidEntityId, nullptr };
}

std::vector<std::pair<EntityId, PointEntity*>>
Scene::findAllByClassname(const std::string& classname) noexcept {
    std::vector<std::pair<EntityId, PointEntity*>> result;
    for (auto& [id, entity] : entities) {
        if (auto* pe = std::get_if<PointEntity>(&entity)) {
            if (pe->classname == classname)
                result.emplace_back(id, pe);
        }
    }
    return result;
}

// ─── Scene — world bounds ────────────────────────────────────────────────────

geo::AABB Scene::worldBounds() const noexcept {
    geo::AABB box;
    for (const auto& [id, entity] : entities) {
        if (const auto* be = std::get_if<BrushEntity>(&entity))
            box.expand(be->worldBounds());
    }
    return box;
}

// ─── Scene — stats ────────────────────────────────────────────────────────────

Scene::Stats Scene::stats() const noexcept {
    Stats s;
    for (const auto& [id, entity] : entities) {
        std::visit([&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BrushEntity>) {
                ++s.brushEntityCount;
                s.totalBrushCount += e.brushes.size();
                s.totalFaceCount  += e.faceCount();
            } else if constexpr (std::is_same_v<T, PointEntity>) {
                ++s.pointEntityCount;
            } else if constexpr (std::is_same_v<T, MeshEntity>) {
                ++s.meshEntityCount;
            }
        }, entity);
    }
    return s;
}

} // namespace forge::scene
