#pragma once

#include <forge/scene.hpp>
#include "Command.hpp"
#include "Selection.hpp"
#include <string>
#include <optional>

namespace forge::editor {

/// Property Inspector panel for viewing and editing selected entity properties
class PropertyInspector {
public:
    /// Draw the property inspector panel
    void draw(scene::Scene& scene, const MultiSelection& selection, bool& isOpen, CommandStack& commands);

private:
    static constexpr std::size_t kEditBufferSize = 256;
    scene::EntityId lastSelectedId_ = scene::kInvalidEntityId;
    std::optional<std::string> editingProperty_;  ///< Currently editing property name (if any)
    char editBuffer_[kEditBufferSize] = {};       ///< Buffer for property value editing
};

} // namespace forge::editor
