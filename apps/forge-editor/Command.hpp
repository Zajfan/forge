#pragma once

#include <forge/scene.hpp>
#include <forge/geo.hpp>

#include <format>
#include <memory>
#include <string>
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
    [[nodiscard]] std::size_t size()   const noexcept { return history_.size(); }
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

} // namespace forge::editor

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
