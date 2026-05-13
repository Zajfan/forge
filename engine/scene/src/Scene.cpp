#include "forge/scene/Scene.hpp"

#include <glm/gtx/matrix_decompose.hpp>
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

// ─── Scene — entity hierarchy ─────────────────────────────────────────────────

void Scene::setParent(EntityId child, EntityId parent) noexcept {
    if (parent == kInvalidEntityId)
        parentMap_.erase(child);
    else
        parentMap_[child] = parent;
}

EntityId Scene::parentOf(EntityId child) const noexcept {
    const auto it = parentMap_.find(child);
    return it != parentMap_.end() ? it->second : kInvalidEntityId;
}

std::vector<EntityId> Scene::childrenOf(EntityId parent) const noexcept {
    std::vector<EntityId> result;
    for (const auto& [child, par] : parentMap_)
        if (par == parent) result.push_back(child);
    return result;
}

Transform Scene::worldTransform(EntityId id) const noexcept {
    // Walk the parent chain and compose transforms
    // Uses iterative approach to avoid recursion on deep hierarchies
    std::vector<EntityId> chain;
    EntityId current = id;
    while (current != kInvalidEntityId) {
        chain.push_back(current);
        current = parentOf(current);
    }

    // Compose in reverse (root → leaf)
    glm::dmat4 world(1.0);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Entity* e = getEntity(*it);
        if (e) world = world * entityTransform(*e).matrix();
    }

    // Decompose back to Transform
    Transform result;
    glm::dvec3 scale, translation, skew;
    glm::dvec4 perspective;
    glm::dquat rotation;
    if (glm::decompose(world, scale, rotation, translation, skew, perspective)) {
        result.translation = translation;
        result.rotation    = rotation;
        result.scale       = scale;
    }
    return result;
}

} // namespace forge::scene
