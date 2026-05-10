#pragma once

#include <forge/scene.hpp>
#include <forge/geo/Primitives.hpp>

#include <format>
#include <memory>
#include <string>
#include <vector>

namespace forge::editor {

// ─── Command ─────────────────────────────────────────────────────────────────

struct Command {
    virtual ~Command() = default;
    virtual void execute(scene::Scene& scene) = 0;
    virtual void undo   (scene::Scene& scene) = 0;
    virtual std::string describe() const      = 0;
};

// ─── CommandStack ────────────────────────────────────────────────────────────

/// Linear undo/redo history.
///
/// Pushing a new command while there are undone entries truncates the future
/// history (standard undo model).
class CommandStack {
public:
    void push(std::unique_ptr<Command> cmd, scene::Scene& scene) {
        // Truncate any undone future
        if (cursor_ < history_.size())
            history_.erase(history_.begin() + static_cast<ptrdiff_t>(cursor_),
                           history_.end());

        cmd->execute(scene);
        history_.push_back(std::move(cmd));
        ++cursor_;
    }

    void undo(scene::Scene& scene) {
        if (!canUndo()) return;
        --cursor_;
        history_[cursor_]->undo(scene);
    }

    void redo(scene::Scene& scene) {
        if (!canRedo()) return;
        history_[cursor_]->execute(scene);
        ++cursor_;
    }

    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < history_.size(); }

    [[nodiscard]] std::string lastUndoLabel() const {
        if (!canUndo()) return "";
        return history_[cursor_ - 1]->describe();
    }
    [[nodiscard]] std::string lastRedoLabel() const {
        if (!canRedo()) return "";
        return history_[cursor_]->describe();
    }

    [[nodiscard]] std::size_t size()   const noexcept { return history_.size(); }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

    void clear() noexcept { history_.clear(); cursor_ = 0; }

private:
    std::vector<std::unique_ptr<Command>> history_;
    std::size_t cursor_ = 0;
};

// ─── Concrete commands ────────────────────────────────────────────────────────

/// Translate a BrushEntity (or any entity with a transform).
struct MoveEntityCommand final : Command {
    scene::EntityId entityId;
    glm::dvec3      oldTranslation;
    glm::dvec3      newTranslation;

    MoveEntityCommand(scene::EntityId id,
                      glm::dvec3 oldT, glm::dvec3 newT)
        : entityId(id), oldTranslation(oldT), newTranslation(newT) {}

    void execute(scene::Scene& s) override { setTranslation(s, newTranslation); }
    void undo   (scene::Scene& s) override { setTranslation(s, oldTranslation); }

    std::string describe() const override {
        return std::format("Move entity {}", entityId);
    }

private:
    static void setTranslation(scene::Scene& s, glm::dvec3 t) {
        if (auto* e = s.getEntity(entityId)) {
            std::visit([&](auto& ent) { ent.transform.translation = t; }, *e);
        }
    }
    scene::EntityId entityId_ = 0;  // quiet unused-field warning; entityId above is the real one
};

/// Add a BrushEntity (primitives menu).
struct AddBrushEntityCommand final : Command {
    scene::BrushEntity  entity;
    scene::EntityId     addedId = scene::kInvalidEntityId;

    explicit AddBrushEntityCommand(scene::BrushEntity e) : entity(std::move(e)) {}

    void execute(scene::Scene& s) override {
        addedId = s.addEntity(entity);
    }
    void undo(scene::Scene& s) override {
        if (addedId != scene::kInvalidEntityId) {
            s.removeEntity(addedId);
            addedId = scene::kInvalidEntityId;
        }
    }
    std::string describe() const override {
        return std::format("Add '{}'", entity.name);
    }
};

/// Delete an entity.
struct DeleteEntityCommand final : Command {
    scene::EntityId savedId;
    scene::Entity   savedEntity;
    bool            hasSaved = false;

    explicit DeleteEntityCommand(scene::EntityId id) : savedId(id) {}

    void execute(scene::Scene& s) override {
        if (auto* e = s.getEntity(savedId)) {
            savedEntity = *e;
            hasSaved    = true;
        }
        s.removeEntity(savedId);
    }
    void undo(scene::Scene& s) override {
        if (hasSaved) {
            // Re-insert with same id by temporarily using addEntity
            // (id will be new but entity is restored)
            s.addEntity(savedEntity);
        }
    }
    std::string describe() const override {
        return std::format("Delete entity {}", savedId);
    }
};

/// Rename an entity.
struct RenameEntityCommand final : Command {
    scene::EntityId entityId;
    std::string     oldName;
    std::string     newName;

    RenameEntityCommand(scene::EntityId id, std::string oldN, std::string newN)
        : entityId(id), oldName(std::move(oldN)), newName(std::move(newN)) {}

    void execute(scene::Scene& s) override { setName(s, newName); }
    void undo   (scene::Scene& s) override { setName(s, oldName); }
    std::string describe() const override  { return std::format("Rename to '{}'", newName); }

private:
    void setName(scene::Scene& s, const std::string& n) {
        if (auto* e = s.getEntity(entityId))
            std::visit([&](auto& ent) { ent.name = n; }, *e);
    }
};

} // namespace forge::editor
