#pragma once

#include <string>
#include <vector>
#include <optional>

#include <glm/vec3.hpp>

namespace forge::editor {

// ─── Property definition ─────────────────────────────────────────────────────

enum class PropType { String, Int, Float, Bool, Vec3, Color, Choices };

struct PropChoice { std::string value; std::string label; };

struct PropertyDef {
    std::string          name;
    PropType             type         = PropType::String;
    std::string          defaultValue;
    std::string          description;
    std::vector<PropChoice> choices;  // for PropType::Choices
};

// ─── Entity class definition ─────────────────────────────────────────────────

enum class EntityKind { Point, Brush, Both };

struct EntityClassDef {
    std::string              classname;
    EntityKind               kind        = EntityKind::Point;
    std::string              category;        ///< e.g. "Lights", "Spawns", "Triggers"
    std::string              description;
    glm::vec3                editorColor = { 0.8f, 0.8f, 0.2f }; ///< display colour
    std::vector<PropertyDef> properties;

    /// Return the PropertyDef for a given property name, or nullptr.
    [[nodiscard]] const PropertyDef* findProp(const std::string& name) const noexcept {
        for (const auto& p : properties)
            if (p.name == name) return &p;
        return nullptr;
    }
};

// ─── EntityRegistry ───────────────────────────────────────────────────────────

/// Global registry of entity class definitions.
///
/// Populated from built-in Forge definitions on startup.
/// Consumers (editor Properties panel, Add Entity dialog) query this to know
/// what properties each classname should have with their default values.
class EntityRegistry {
public:
    /// Register all built-in Forge / Quake-compatible entity classes.
    void loadBuiltins() noexcept;

    /// Register a custom class definition.
    void add(EntityClassDef def) noexcept;

    /// Clear all definitions.
    void clear() noexcept;

    // ── Query ─────────────────────────────────────────────────────────────────

    /// Find a class by classname, or nullptr.
    [[nodiscard]] const EntityClassDef* find(const std::string& classname) const noexcept;

    /// All registered classes.
    [[nodiscard]] const std::vector<EntityClassDef>& all() const noexcept { return defs_; }

    /// All classes in a category.
    [[nodiscard]] std::vector<const EntityClassDef*>
    inCategory(const std::string& category) const noexcept;

    /// Unique category names (sorted).
    [[nodiscard]] std::vector<std::string> categories() const noexcept;

private:
    std::vector<EntityClassDef> defs_;
};

/// Global registry instance (initialised in EditorApp::init).
EntityRegistry& globalRegistry() noexcept;

} // namespace forge::editor
