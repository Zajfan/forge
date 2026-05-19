#pragma once

#include <forge/scene.hpp>
#include <forge/geo.hpp>

#include <format>
#include <memory>
#include <string>
#include <optional>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace forge::editor {

// ─── Command base ────────────────────────────────────────────────────────────

struct Command {
    virtual ~Command() = default;
    virtual void execute(scene::Scene&) = 0;
    virtual void undo   (scene::Scene&) = 0;
    virtual std::string describe() const = 0;
};

// ─── CommandStack ────────────────────────────────────────────────────────────

class CommandStack {
public:
    void push(std::unique_ptr<Command> cmd, scene::Scene& scene) {
        if (cursor_ < history_.size())
            history_.erase(history_.begin() + static_cast<ptrdiff_t>(cursor_), history_.end());
        cmd->execute(scene);
        history_.push_back(std::move(cmd));
        ++cursor_;
    }
    void undo(scene::Scene& s) { if (canUndo()) history_[--cursor_]->undo(s); }
    void redo(scene::Scene& s) { if (canRedo()) history_[cursor_++]->execute(s); }

    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < history_.size(); }
    [[nodiscard]] std::string lastUndoLabel() const { return canUndo() ? history_[cursor_-1]->describe() : ""; }
    [[nodiscard]] std::string lastRedoLabel() const { return canRedo() ? history_[cursor_]->describe()   : ""; }
    [[nodiscard]] std::vector<std::string> recentUndoLabels(std::size_t maxCount = 3) const {
        std::vector<std::string> labels;
        const std::size_t count = std::min(maxCount, cursor_);
        labels.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            labels.push_back(history_[cursor_ - 1 - i]->describe());
        }
        return labels;
    }
    [[nodiscard]] std::size_t size()   const noexcept { return history_.size(); }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    void clear() noexcept { history_.clear(); cursor_ = 0; }

private:
    std::vector<std::unique_ptr<Command>> history_;
    std::size_t cursor_ = 0;
};

// ─── Move entity ─────────────────────────────────────────────────────────────

struct MoveEntityCommand final : Command {
    scene::EntityId entityId;
    glm::dvec3      oldPos, newPos;

    MoveEntityCommand(scene::EntityId id, glm::dvec3 o, glm::dvec3 n)
        : entityId(id), oldPos(o), newPos(n) {}

    void execute(scene::Scene& s) override { setPos(s, newPos); }
    void undo   (scene::Scene& s) override { setPos(s, oldPos); }
    std::string describe() const override  { return std::format("Move entity {}", entityId); }

private:
    void setPos(scene::Scene& s, glm::dvec3 p) {
        if (auto* e = s.getEntity(entityId))
            std::visit([&](auto& x){ x.transform.translation = p; }, *e);
    }
};

// ─── Add brush entity ────────────────────────────────────────────────────────

struct AddBrushEntityCommand final : Command {
    scene::BrushEntity entity;
    scene::EntityId    addedId = scene::kInvalidEntityId;

    explicit AddBrushEntityCommand(scene::BrushEntity e) : entity(std::move(e)) {}

    void execute(scene::Scene& s) override { addedId = s.addEntity(entity); }
    void undo   (scene::Scene& s) override {
        if (addedId != scene::kInvalidEntityId) {
            s.removeEntity(addedId); addedId = scene::kInvalidEntityId;
        }
    }
    std::string describe() const override { return std::format("Add '{}'", entity.name); }
};

// ─── Add brush to existing brush entity ─────────────────────────────────────

struct AddBrushToEntityCommand final : Command {
    scene::EntityId entityId = scene::kInvalidEntityId;
    geo::Brush      brush;
    std::size_t     addedIndex = 0;
    bool            added = false;

    AddBrushToEntityCommand(scene::EntityId id, geo::Brush b)
        : entityId(id), brush(std::move(b)) {}

    void execute(scene::Scene& s) override {
        auto* entity = s.getEntity(entityId);
        auto* be = entity ? std::get_if<scene::BrushEntity>(entity) : nullptr;
        if (!be) return;
        addedIndex = be->brushes.size();
        be->brushes.push_back(brush);
        added = true;
    }

    void undo(scene::Scene& s) override {
        if (!added) return;
        auto* entity = s.getEntity(entityId);
        auto* be = entity ? std::get_if<scene::BrushEntity>(entity) : nullptr;
        if (!be || addedIndex >= be->brushes.size()) return;
        be->brushes.erase(be->brushes.begin() + static_cast<std::ptrdiff_t>(addedIndex));
    }

    std::string describe() const override {
        return std::format("Add brush to entity {}", entityId);
    }
};

// ─── Delete entity ───────────────────────────────────────────────────────────

struct DeleteEntityCommand final : Command {
    scene::EntityId savedId;
    scene::Entity   savedEntity;
    bool            hasSaved = false;

    explicit DeleteEntityCommand(scene::EntityId id) : savedId(id) {}

    void execute(scene::Scene& s) override {
        if (auto* e = s.getEntity(savedId)) { savedEntity = *e; hasSaved = true; }
        s.removeEntity(savedId);
    }
    void undo(scene::Scene& s) override {
        if (hasSaved) s.addEntity(savedEntity);
    }
    std::string describe() const override { return std::format("Delete entity {}", savedId); }
};

// ─── Rename entity ───────────────────────────────────────────────────────────

struct RenameEntityCommand final : Command {
    scene::EntityId entityId;
    std::string     oldName, newName;

    RenameEntityCommand(scene::EntityId id, std::string o, std::string n)
        : entityId(id), oldName(std::move(o)), newName(std::move(n)) {}

    void execute(scene::Scene& s) override { setName(s, newName); }
    void undo   (scene::Scene& s) override { setName(s, oldName); }
    std::string describe() const override  { return std::format("Rename to '{}'", newName); }

private:
    void setName(scene::Scene& s, const std::string& n) {
        if (auto* e = s.getEntity(entityId))
            std::visit([&](auto& x){ x.name = n; }, *e);
    }
};

// ─── Set entity layer/group metadata ────────────────────────────────────────

struct SetEntityLayerCommand final : Command {
    scene::EntityId entityId;
    std::unordered_map<scene::EntityId, std::string>& entityLayers;
    std::unordered_map<std::string, bool>&            layerVisibility;
    std::unordered_map<std::string, bool>&            layerLocked;
    std::unordered_map<std::string, glm::vec3>&       layerTint;
    std::string oldLayer;
    std::string newLayer;

    SetEntityLayerCommand(
        scene::EntityId id,
        std::unordered_map<scene::EntityId, std::string>& layers,
        std::unordered_map<std::string, bool>& visibility,
        std::unordered_map<std::string, bool>& locked,
        std::unordered_map<std::string, glm::vec3>& tint,
        std::string oldValue,
        std::string newValue)
        : entityId(id)
        , entityLayers(layers)
        , layerVisibility(visibility)
        , layerLocked(locked)
        , layerTint(tint)
        , oldLayer(std::move(oldValue))
        , newLayer(std::move(newValue)) {}

    void execute(scene::Scene& s) override { setLayer(s, newLayer); }
    void undo   (scene::Scene& s) override { setLayer(s, oldLayer); }
    std::string describe() const override  { return std::format("Set layer '{}'", newLayer); }

private:
    void setLayer(scene::Scene& s, const std::string& value) {
        if (!s.hasEntity(entityId)) return;
        entityLayers[entityId] = value;
        if (!layerVisibility.contains(value)) layerVisibility[value] = true;
        if (!layerLocked.contains(value)) layerLocked[value] = false;
        if (!layerTint.contains(value)) layerTint[value] = {1.f, 1.f, 1.f};
    }
};

struct SetEntityGroupCommand final : Command {
    scene::EntityId entityId;
    std::unordered_map<scene::EntityId, std::string>& entityGroups;
    std::string oldGroup;
    std::string newGroup;

    SetEntityGroupCommand(
        scene::EntityId id,
        std::unordered_map<scene::EntityId, std::string>& groups,
        std::string oldValue,
        std::string newValue)
        : entityId(id)
        , entityGroups(groups)
        , oldGroup(std::move(oldValue))
        , newGroup(std::move(newValue)) {}

    void execute(scene::Scene& s) override { setGroup(s, newGroup); }
    void undo   (scene::Scene& s) override { setGroup(s, oldGroup); }
    std::string describe() const override  { return std::format("Set group '{}'", newGroup); }

private:
    void setGroup(scene::Scene& s, const std::string& value) {
        if (!s.hasEntity(entityId)) return;
        entityGroups[entityId] = value;
    }
};

// ─── Set entity property value ──────────────────────────────────────────────

struct SetEntityPropertyCommand final : Command {
    scene::EntityId           entityId = scene::kInvalidEntityId;
    std::string               key;
    std::optional<scene::PropertyValue> oldValue;
    scene::PropertyValue      newValue;

    SetEntityPropertyCommand(
        scene::EntityId id,
        std::string propertyKey,
        std::optional<scene::PropertyValue> oldPropertyValue,
        scene::PropertyValue newPropertyValue)
        : entityId(id)
        , key(std::move(propertyKey))
        , oldValue(std::move(oldPropertyValue))
        , newValue(std::move(newPropertyValue)) {}

    void execute(scene::Scene& s) override {
        if (auto* props = propertiesForEntity(s, entityId)) {
            (*props)[key] = newValue;
        }
    }

    void undo(scene::Scene& s) override {
        if (auto* props = propertiesForEntity(s, entityId)) {
            if (oldValue.has_value()) {
                (*props)[key] = *oldValue;
            } else {
                props->erase(key);
            }
        }
    }

    std::string describe() const override {
        return std::format("Set property '{}'", key);
    }

private:
    static std::unordered_map<std::string, scene::PropertyValue>*
    propertiesForEntity(scene::Scene& s, scene::EntityId id) {
        auto* e = s.getEntity(id);
        if (!e) return nullptr;
        if (auto* be = std::get_if<scene::BrushEntity>(e)) return &be->properties;
        if (auto* pe = std::get_if<scene::PointEntity>(e)) return &pe->properties;
        return nullptr;
    }
};

// ─── Set brush bool field (solid/visible) ───────────────────────────────────

struct SetBrushBoolCommand final : Command {
    enum class Field { Solid, Visible };

    scene::EntityId entityId = scene::kInvalidEntityId;
    Field           field = Field::Solid;
    bool            oldValue = false;
    bool            newValue = false;

    SetBrushBoolCommand(scene::EntityId id, Field f, bool oldV, bool newV)
        : entityId(id), field(f), oldValue(oldV), newValue(newV) {}

    void execute(scene::Scene& s) override { apply(s, newValue); }
    void undo(scene::Scene& s) override { apply(s, oldValue); }

    std::string describe() const override {
        const char* fieldName = (field == Field::Solid) ? "solid" : "visible";
        return std::format("Set brush {} {}", fieldName, newValue ? "on" : "off");
    }

private:
    void apply(scene::Scene& s, bool value) {
        auto* e = s.getEntity(entityId);
        auto* be = e ? std::get_if<scene::BrushEntity>(e) : nullptr;
        if (!be) return;
        if (field == Field::Solid) be->solid = value;
        else be->visible = value;
    }
};

// ─── Set brush classname (with property snapshots) ──────────────────────────

struct SetBrushClassnameCommand final : Command {
    scene::EntityId entityId = scene::kInvalidEntityId;
    std::string oldClassname;
    std::string newClassname;
    std::unordered_map<std::string, scene::PropertyValue> oldProperties;
    std::unordered_map<std::string, scene::PropertyValue> newProperties;

    SetBrushClassnameCommand(
        scene::EntityId id,
        std::string oldCls,
        std::string newCls,
        std::unordered_map<std::string, scene::PropertyValue> oldProps,
        std::unordered_map<std::string, scene::PropertyValue> newProps)
        : entityId(id)
        , oldClassname(std::move(oldCls))
        , newClassname(std::move(newCls))
        , oldProperties(std::move(oldProps))
        , newProperties(std::move(newProps)) {}

    void execute(scene::Scene& s) override { apply(s, newClassname, newProperties); }
    void undo(scene::Scene& s) override { apply(s, oldClassname, oldProperties); }

    std::string describe() const override {
        return std::format("Set classname '{}'", newClassname);
    }

private:
    void apply(
        scene::Scene& s,
        const std::string& classname,
        const std::unordered_map<std::string, scene::PropertyValue>& properties)
    {
        auto* e = s.getEntity(entityId);
        auto* be = e ? std::get_if<scene::BrushEntity>(e) : nullptr;
        if (!be) return;
        be->classname = classname;
        be->properties = properties;
    }
};

// ─── Set point classname (with property snapshots) ──────────────────────────

struct SetPointClassnameCommand final : Command {
    scene::EntityId entityId = scene::kInvalidEntityId;
    std::string oldClassname;
    std::string newClassname;
    std::unordered_map<std::string, scene::PropertyValue> oldProperties;
    std::unordered_map<std::string, scene::PropertyValue> newProperties;

    SetPointClassnameCommand(
        scene::EntityId id,
        std::string oldCls,
        std::string newCls,
        std::unordered_map<std::string, scene::PropertyValue> oldProps,
        std::unordered_map<std::string, scene::PropertyValue> newProps)
        : entityId(id)
        , oldClassname(std::move(oldCls))
        , newClassname(std::move(newCls))
        , oldProperties(std::move(oldProps))
        , newProperties(std::move(newProps)) {}

    void execute(scene::Scene& s) override { apply(s, newClassname, newProperties); }
    void undo(scene::Scene& s) override { apply(s, oldClassname, oldProperties); }

    std::string describe() const override {
        return std::format("Set point classname '{}'", newClassname);
    }

private:
    void apply(
        scene::Scene& s,
        const std::string& classname,
        const std::unordered_map<std::string, scene::PropertyValue>& properties)
    {
        auto* e = s.getEntity(entityId);
        auto* pe = e ? std::get_if<scene::PointEntity>(e) : nullptr;
        if (!pe) return;
        pe->classname = classname;
        pe->properties = properties;
    }
};

// ─── Batch delete ────────────────────────────────────────────────────────────
// Atomically deletes multiple entities in one command (one undo step).

struct BatchDeleteCommand final : Command {
    struct Snapshot { scene::EntityId id; scene::Entity data; };
    std::vector<scene::EntityId> targets;
    std::vector<Snapshot>        saved;

    explicit BatchDeleteCommand(std::vector<scene::EntityId> ids)
        : targets(std::move(ids)) {}

    void execute(scene::Scene& s) override {
        saved.clear();
        for (auto id : targets)
            if (auto* e = s.getEntity(id)) { saved.push_back({ id, *e }); s.removeEntity(id); }
    }
    void undo(scene::Scene& s) override {
        for (auto& snap : saved) s.addEntity(snap.data);
    }
    std::string describe() const override {
        return std::format("Delete {} entities", targets.size());
    }
};

// ─── Set face material ───────────────────────────────────────────────────────

struct SetFaceMaterialCommand final : Command {
    scene::EntityId entityId;
    std::size_t     brushIdx, faceIdx;
    std::string     oldMat, newMat;

    SetFaceMaterialCommand(scene::EntityId id, std::size_t bi, std::size_t fi,
                           std::string o, std::string n)
        : entityId(id), brushIdx(bi), faceIdx(fi)
        , oldMat(std::move(o)), newMat(std::move(n)) {}

    void execute(scene::Scene& s) override { setMat(s, newMat); }
    void undo   (scene::Scene& s) override { setMat(s, oldMat); }
    std::string describe() const override  { return std::format("Paint '{}'", newMat); }

private:
    void setMat(scene::Scene& s, const std::string& m) {
        if (auto* e = s.getEntity(entityId))
            if (auto* be = std::get_if<scene::BrushEntity>(e))
                if (brushIdx < be->brushes.size() && faceIdx < be->brushes[brushIdx].faces.size())
                    be->brushes[brushIdx].faces[faceIdx].materialId = m;
    }
};

// ─── Batch set face material (for drag-to-paint) ────────────────────────────────

struct BatchSetFaceMaterialCommand final : Command {
    struct FaceRef {
        scene::EntityId entityId;
        std::size_t     brushIdx, faceIdx;
        std::string     oldMat, newMat;
    };

    std::vector<FaceRef> faces;

    explicit BatchSetFaceMaterialCommand(std::vector<FaceRef> f)
        : faces(std::move(f)) {}

    void execute(scene::Scene& s) override {
        for (const auto& f : faces) {
            if (auto* e = s.getEntity(f.entityId))
                if (auto* be = std::get_if<scene::BrushEntity>(e))
                    if (f.brushIdx < be->brushes.size() && f.faceIdx < be->brushes[f.brushIdx].faces.size())
                        be->brushes[f.brushIdx].faces[f.faceIdx].materialId = f.newMat;
        }
    }

    void undo(scene::Scene& s) override {
        for (const auto& f : faces) {
            if (auto* e = s.getEntity(f.entityId))
                if (auto* be = std::get_if<scene::BrushEntity>(e))
                    if (f.brushIdx < be->brushes.size() && f.faceIdx < be->brushes[f.brushIdx].faces.size())
                        be->brushes[f.brushIdx].faces[f.faceIdx].materialId = f.oldMat;
        }
    }

    std::string describe() const override {
        if (faces.size() == 1) return std::format("Paint '{}'", faces[0].newMat);
        return std::format("Paint {} faces", faces.size());
    }
};

// ─── Set face UV ─────────────────────────────────────────────────────────────
// Stores before/after UV state for one face (offset, scale, rotation).

struct SetFaceUVCommand final : Command {
    scene::EntityId entityId;
    std::size_t     brushIdx, faceIdx;

    struct UVState { glm::vec2 offset, scale; float rotation; };
    UVState old_, new_;

    SetFaceUVCommand(scene::EntityId id, std::size_t bi, std::size_t fi,
                     UVState o, UVState n)
        : entityId(id), brushIdx(bi), faceIdx(fi), old_(o), new_(n) {}

    void execute(scene::Scene& s) override { apply(s, new_); }
    void undo   (scene::Scene& s) override { apply(s, old_); }
    std::string describe() const override  { return "UV edit"; }

private:
    void apply(scene::Scene& s, const UVState& uv) {
        if (auto* e = s.getEntity(entityId))
            if (auto* be = std::get_if<scene::BrushEntity>(e))
                if (brushIdx < be->brushes.size() &&
                    faceIdx < be->brushes[brushIdx].faces.size()) {
                    auto& face = be->brushes[brushIdx].faces[faceIdx];
                    face.uvOffset   = uv.offset;
                    face.uvScale    = uv.scale;
                    face.uvRotation = uv.rotation;
                    be->brushes[brushIdx].invalidate();
                }
    }
};

// ─── Clip brush ──────────────────────────────────────────────────────────────
// Splits the brushes of one entity along a plane; replaces them with the
// front and back fragments.

struct ClipBrushCommand final : Command {
    scene::EntityId            entityId;
    std::vector<geo::Brush>    originalBrushes;  // for undo
    std::vector<geo::Brush>    resultBrushes;    // front+back fragments

    ClipBrushCommand(scene::EntityId id,
                     std::vector<geo::Brush> orig,
                     std::vector<geo::Brush> result)
        : entityId(id)
        , originalBrushes(std::move(orig))
        , resultBrushes(std::move(result)) {}

    void execute(scene::Scene& s) override { setBrushes(s, resultBrushes); }
    void undo   (scene::Scene& s) override { setBrushes(s, originalBrushes); }
    std::string describe() const override  { return "Clip brush"; }

private:
    void setBrushes(scene::Scene& s, const std::vector<geo::Brush>& b) {
        if (auto* e = s.getEntity(entityId))
            if (auto* be = std::get_if<scene::BrushEntity>(e))
                { be->brushes = b; for (auto& br : be->brushes) br.invalidate(); }
    }
};

// ─── Hollow entity ───────────────────────────────────────────────────────────
// Replaces a solid single-brush entity with N wall entities.

struct HollowEntityCommand final : Command {
    scene::EntityId                 solidId;
    scene::BrushEntity              solidEntity;          // for undo restore
    std::vector<scene::EntityId>    wallIds;              // created by execute

    explicit HollowEntityCommand(scene::EntityId id, scene::BrushEntity original)
        : solidId(id), solidEntity(std::move(original)) {}

    void execute(scene::Scene& s) override {
        if (solidEntity.brushes.empty()) return;
        const auto walls = geo::hollowBrush(solidEntity.brushes[0], wallThickness);

        // Remove original solid
        s.removeEntity(solidId);
        wallIds.clear();

        for (std::size_t i = 0; i < walls.size(); ++i) {
            scene::BrushEntity we;
            we.name      = std::format("{}_wall_{}", solidEntity.name, i);
            we.transform = solidEntity.transform;
            we.brushes   = { walls[i] };
            wallIds.push_back(s.addEntity(std::move(we)));
        }
    }

    void undo(scene::Scene& s) override {
        for (auto id : wallIds) s.removeEntity(id);
        wallIds.clear();
        s.addEntity(solidEntity);
    }

    std::string describe() const override {
        return std::format("Hollow '{}'", solidEntity.name);
    }

    double wallThickness = 16.0;
};

// ─── CSG subtract ────────────────────────────────────────────────────────────
// Carves the cutter entity out of all overlapping solid brush entities.

struct CSGSubtractCommand final : Command {
    struct EntitySnapshot {
        scene::EntityId    id;
        scene::BrushEntity data;
    };

    scene::EntityId         cutterId;
    scene::BrushEntity      cutterData;        // to restore on undo

    std::vector<EntitySnapshot>           originals;  // affected entities before
    std::vector<scene::EntityId>          fragmentIds; // entities added by execute

    CSGSubtractCommand(scene::EntityId cId, scene::BrushEntity cData)
        : cutterId(cId), cutterData(std::move(cData)) {}

    void execute(scene::Scene& s) override {
        originals.clear();
        fragmentIds.clear();

        // Collect cutter brush list
        std::vector<geo::Brush> cutterBrushes = cutterData.brushes;

        for (auto& [id, entity] : s.entities) {
            if (id == cutterId) continue;
            auto* be = std::get_if<scene::BrushEntity>(&entity);
            if (!be || !be->solid) continue;

            // AABB overlap check
            const geo::AABB aBounds = be->worldBounds();
            const geo::AABB cBounds = cutterData.worldBounds();
            if (!aBounds.overlaps(cBounds)) continue;

            // Save original
            originals.push_back({ id, *be });
        }

        // Apply CSG on copies, then update scene
        for (auto& snap : originals) {
            std::vector<geo::Brush> fragments;
            for (const auto& subjectBrush : snap.data.brushes) {
                auto result = geo::csgSubtract(subjectBrush,
                    std::span<const geo::Brush>(cutterBrushes));
                for (auto& f : result) fragments.push_back(std::move(f));
            }

            if (fragments.empty()) {
                // Fully consumed — remove entity
                s.removeEntity(snap.id);
            } else {
                // Replace brushes in-place
                auto* e = s.getEntity(snap.id);
                if (auto* be = std::get_if<scene::BrushEntity>(e)) {
                    be->brushes = std::move(fragments);
                    for (auto& b : be->brushes) b.invalidate();
                }
            }
        }

        // Remove the cutter
        s.removeEntity(cutterId);
    }

    void undo(scene::Scene& s) override {
        // Delete any fragment entities we added
        for (auto fid : fragmentIds) s.removeEntity(fid);
        fragmentIds.clear();

        // Restore all original entities
        for (const auto& snap : originals) {
            if (auto* e = s.getEntity(snap.id)) {
                if (auto* be = std::get_if<scene::BrushEntity>(e)) {
                    *be = snap.data;
                    for (auto& b : be->brushes) b.invalidate();
                }
            } else {
                // Entity was deleted — re-add it
                s.addEntity(snap.data);
            }
        }

        // Restore cutter
        s.addEntity(cutterData);
    }

    std::string describe() const override { return "CSG Subtract"; }
};

// ─── CSG intersect ───────────────────────────────────────────────────────────
// Keeps only the overlapping volume of two entities.

struct CSGIntersectCommand final : Command {
    scene::EntityId         aId, bId;
    scene::BrushEntity      aData, bData;       // to restore on undo
    std::vector<scene::EntityId> resultIds;    // intersected fragments

    CSGIntersectCommand(scene::EntityId aid, scene::EntityId bid, 
                        scene::BrushEntity adata, scene::BrushEntity bdata)
        : aId(aid), bId(bid), aData(std::move(adata)), bData(std::move(bdata)) {}

    void execute(scene::Scene& s) override {
        resultIds.clear();

        if (aData.brushes.empty() || bData.brushes.empty()) return;

        std::vector<geo::Brush> intersected;
        for (const auto& aBrush : aData.brushes) {
            for (const auto& bBrush : bData.brushes) {
                auto result = geo::csgIntersect(aBrush, bBrush);
                for (auto& f : result) intersected.push_back(std::move(f));
            }
        }

        if (!intersected.empty()) {
            scene::BrushEntity re;
            re.name = std::format("{} ∩ {}", aData.name, bData.name);
            re.transform = aData.transform;
            re.brushes = std::move(intersected);
            resultIds.push_back(s.addEntity(std::move(re)));
        }

        // Remove original entities
        s.removeEntity(aId);
        s.removeEntity(bId);
    }

    void undo(scene::Scene& s) override {
        for (auto id : resultIds) s.removeEntity(id);
        resultIds.clear();
        s.addEntity(aData);
        s.addEntity(bData);
    }

    std::string describe() const override { return "CSG Intersect"; }
};

// ─── CSG union ────────────────────────────────────────────────────────────────
// Combines two entities into their union (all parts of either).

struct CSGUnionCommand final : Command {
    scene::EntityId         aId, bId;
    scene::BrushEntity      aData, bData;       // to restore on undo
    std::vector<scene::EntityId> resultIds;    // union fragments

    CSGUnionCommand(scene::EntityId aid, scene::EntityId bid,
                    scene::BrushEntity adata, scene::BrushEntity bdata)
        : aId(aid), bId(bid), aData(std::move(adata)), bData(std::move(bdata)) {}

    void execute(scene::Scene& s) override {
        resultIds.clear();

        if (aData.brushes.empty() || bData.brushes.empty()) {
            // One is empty, keep the other
            if (aData.brushes.empty()) s.removeEntity(aId);
            else s.removeEntity(bId);
            return;
        }

        // Compute union: all fragments from both
        scene::BrushEntity ue;
        ue.name = std::format("{} ∪ {}", aData.name, bData.name);
        ue.transform = aData.transform;

        for (const auto& aBrush : aData.brushes) {
            for (const auto& bBrush : bData.brushes) {
                auto result = geo::csgUnion(aBrush, bBrush);
                for (auto& f : result) ue.brushes.push_back(std::move(f));
            }
        }

        if (!ue.brushes.empty()) {
            resultIds.push_back(s.addEntity(std::move(ue)));
        }

        s.removeEntity(aId);
        s.removeEntity(bId);
    }

    void undo(scene::Scene& s) override {
        for (auto id : resultIds) s.removeEntity(id);
        resultIds.clear();
        s.addEntity(aData);
        s.addEntity(bData);
    }

    std::string describe() const override { return "CSG Union"; }
};

// ─── CSG xor ──────────────────────────────────────────────────────────────────
// Computes symmetric difference (parts in one or other, but not both).

struct CSGXorCommand final : Command {
    scene::EntityId         aId, bId;
    scene::BrushEntity      aData, bData;       // to restore on undo
    std::vector<scene::EntityId> resultIds;    // xor fragments

    CSGXorCommand(scene::EntityId aid, scene::EntityId bid,
                  scene::BrushEntity adata, scene::BrushEntity bdata)
        : aId(aid), bId(bid), aData(std::move(adata)), bData(std::move(bdata)) {}

    void execute(scene::Scene& s) override {
        resultIds.clear();

        if (aData.brushes.empty() || bData.brushes.empty()) return;

        std::vector<geo::Brush> xored;
        for (const auto& aBrush : aData.brushes) {
            for (const auto& bBrush : bData.brushes) {
                auto result = geo::csgXor(aBrush, bBrush);
                for (auto& f : result) xored.push_back(std::move(f));
            }
        }

        if (!xored.empty()) {
            scene::BrushEntity xe;
            xe.name = std::format("{} ⊕ {}", aData.name, bData.name);
            xe.transform = aData.transform;
            xe.brushes = std::move(xored);
            resultIds.push_back(s.addEntity(std::move(xe)));
        }

        s.removeEntity(aId);
        s.removeEntity(bId);
    }

    void undo(scene::Scene& s) override {
        for (auto id : resultIds) s.removeEntity(id);
        resultIds.clear();
        s.addEntity(aData);
        s.addEntity(bData);
    }

    std::string describe() const override { return "CSG XOR"; }
};

// ─── Move vertex ─────────────────────────────────────────────────────────────
// Moves one vertex of a brush by recomputing the adjacent face planes.
// Stores per-face plane snapshots for exact undo.

struct MoveVertexCommand final : Command {
    scene::EntityId entityId;
    std::size_t     brushIdx;
    glm::dvec3      oldPos;
    glm::dvec3      newPos;

    struct FacePlane { std::size_t faceIdx; geo::Plane before; geo::Plane after; };
    std::vector<FacePlane> affected;

    MoveVertexCommand(scene::EntityId id, std::size_t bi,
                      glm::dvec3 oP, glm::dvec3 nP)
        : entityId(id), brushIdx(bi), oldPos(oP), newPos(nP) {}

    void execute(scene::Scene& s) override { apply(s, true); }
    void undo   (scene::Scene& s) override { apply(s, false); }
    std::string describe() const override  { return "Move vertex"; }

private:
    void apply(scene::Scene& s, bool forward) {
        auto* e = s.getEntity(entityId);
        if (!e) return;
        auto* be = std::get_if<scene::BrushEntity>(e);
        if (!be || brushIdx >= be->brushes.size()) return;
        auto& brush = be->brushes[brushIdx];

        for (auto& fp : affected) {
            if (fp.faceIdx < brush.faces.size())
                brush.faces[fp.faceIdx].plane = forward ? fp.after : fp.before;
        }
        brush.invalidate();
    }
};

/// Compute the new face planes after moving vertex oldPos → newPos in brush.
/// Fills cmd.affected with per-face plane snapshots.
inline void computeVertexMove(geo::Brush& brush,
                               glm::dvec3  oldPos,
                               glm::dvec3  newPos,
                               MoveVertexCommand& cmd)
{
    constexpr double eps = 1e-3;
    const auto& polys = brush.allFacePolygons();

    for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
        const auto& poly = polys[fi];
        for (std::size_t vi = 0; vi < poly.size(); ++vi) {
            if (glm::length(poly[vi] - oldPos) > eps) continue;

            const std::size_t n     = poly.size();
            const glm::dvec3& vPrev = poly[(vi + n - 1) % n];
            const glm::dvec3& vNext = poly[(vi + 1)     % n];

            // New plane through vPrev, newPos, vNext  (same CCW winding as before)
            const geo::Plane newPlane = geo::Plane::fromPoints(vPrev, newPos, vNext);

            // Sanity: normal must agree with the old normal direction
            // (large disagreement = degenerate move — skip)
            if (glm::dot(newPlane.normal, brush.faces[fi].plane.normal) < 0.0)
                continue;  // skip face if winding flipped

            cmd.affected.push_back({ fi, brush.faces[fi].plane, newPlane });
            break; // each face contains the vertex at most once
        }
    }
}

// ─── Snap all entities to grid ────────────────────────────────────────────────

struct SnapToGridCommand final : Command {
    double gridSize;

    struct EntitySnap {
        scene::EntityId id;
        glm::dvec3      before;
        glm::dvec3      after;
    };
    std::vector<EntitySnap> snaps;

    explicit SnapToGridCommand(double g) : gridSize(g) {}

    void execute(scene::Scene& s) override {
        snaps.clear();
        for (auto& [id, entity] : s.entities) {
            const glm::dvec3 before = scene::entityTransform(entity).translation;
            const glm::dvec3 after  = {
                std::round(before.x / gridSize) * gridSize,
                std::round(before.y / gridSize) * gridSize,
                std::round(before.z / gridSize) * gridSize,
            };
            if (glm::length(after - before) > 1e-6) {
                snaps.push_back({ id, before, after });
                std::visit([&](auto& e){ e.transform.translation = after; }, entity);
            }
        }
    }

    void undo(scene::Scene& s) override {
        for (const auto& snap : snaps)
            if (auto* e = s.getEntity(snap.id))
                std::visit([&](auto& ent){ ent.transform.translation = snap.before; }, *e);
    }

    std::string describe() const override {
        return std::format("Snap {} entities to grid ({})", snaps.size(), gridSize);
    }
};

// ─── Batch move (multi-selection) ────────────────────────────────────────────

struct BatchMoveCommand final : Command {
    struct Entry { scene::EntityId id; glm::dvec3 delta; };
    std::vector<Entry> entries;

    void execute(scene::Scene& s) override { apply(s,  1.0); }
    void undo   (scene::Scene& s) override { apply(s, -1.0); }
    std::string describe() const override {
        return std::format("Move {} entities", entries.size());
    }

private:
    void apply(scene::Scene& s, double sign) {
        for (const auto& e : entries)
            if (auto* ent = s.getEntity(e.id))
                std::visit([&](auto& x){ x.transform.translation += sign * e.delta; }, *ent);
    }
};

// ─── Duplicate selection ─────────────────────────────────────────────────────

struct DuplicateEntitiesCommand final : Command {
    std::vector<scene::BrushEntity> originals;       // what to copy
    std::vector<scene::EntityId>    cloneIds;         // IDs of clones (assigned on execute)
    glm::dvec3                      offset = {32,0,32}; // placement offset

    void execute(scene::Scene& s) override {
        cloneIds.clear();
        for (auto& orig : originals) {
            scene::BrushEntity clone = orig;
            clone.name += "_copy";
            clone.transform.translation += offset;
            cloneIds.push_back(s.addEntity(std::move(clone)));
        }
    }
    void undo(scene::Scene& s) override {
        for (auto id : cloneIds) s.removeEntity(id);
        cloneIds.clear();
    }
    std::string describe() const override {
        return std::format("Duplicate {} entities", originals.size());
    }
};

} // namespace forge::editor
