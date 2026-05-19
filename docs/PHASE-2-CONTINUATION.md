# Phase 2 Continuation Plan — Next Session

## Current State
- UndoStack infrastructure complete and compiles cleanly
- Editor binary built at `build-editor/bin/forge-editor` (70 MB)
- All Phase 1 tests passing (9 cases, 131 assertions)
- Ready for immediate task execution

---

## Task 2.1.B: Wire Undo/Redo Hotkeys

### Scope
- Bind Ctrl+Shift+Z to `undoStack_.redo()`
- Add status message on redo (e.g., "Redo: Move Entity" for 1 second)
- Push a checkpoint to undo stack when scene is saved
- Test with actual editor: save, undo, redo

### Implementation Steps
1. **In EditorApp.cpp, find the keyboard shortcut block** (around line 1300 in drawToolbar)
2. **Add Ctrl+Shift+Z handler after the existing Ctrl+Y handler:**
   ```cpp
   if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
       const std::string frameName = undoStack_.redo();
       if (!frameName.empty()) {
           setStatus(std::format("Redo: {}", frameName));
           // Load the snapshot from undoStack_.currentFrame()
           // Rebuild meshes and reconcile state
       }
   }
   ```
3. **In EditorApp::saveScene()**, after successful save:
   ```cpp
   // Checkpoint the scene in undo stack
   serial::ProjectData checkpoint{scene_, materialLibrary_, editorMeta_};
   undoStack_.push("Save Scene", checkpoint);
   ```
4. **Update docs/EDITOR_TOOL_FIELD_GUIDE.md:**
   - Add line: `Ctrl+Shift+Z — redo (secondary to Ctrl+Y)`
   - Add line: `Checkpoint created on each scene save`

### Verification Checklist
- [ ] Editor compiles after keyboard binding changes
- [ ] Ctrl+Shift+Z shows status message when redo available
- [ ] Save → Ctrl+Z → (undo works) → Ctrl+Shift+Z → (redo works)
- [ ] Status messages appear and disappear after 1 second

### Expected Compilation Time
~20 seconds (only EditorApp.cpp rebuilds)

---

## Task 2.2: Multi-Entity Visibility Toggle

### Scope
- Add Alt+H hotkey to hide all selected entities
- Add Alt+Shift+H hotkey to show all hidden entities
- Add toolbar dropdown for quick visibility control
- Respect visibility in validation panel and material editor

### Implementation Steps
1. **In EditorApp.hpp**, find the selection members (around line 160)
2. **Add state tracking for hidden entities:**
   ```cpp
   std::unordered_set<scene::EntityId> hiddenEntities_;
   ```
3. **In EditorApp.cpp drawToolbar()**, add dropdown after the ortho/quad toggles:
   ```cpp
   if (ImGui::Button("Visibility")) {
       if (ImGui::BeginPopupContextItem()) {
           if (ImGui::MenuItem("Hide Selected") && !selection_.empty()) {
               for (auto id : selection_.ids) hiddenEntities_.insert(id);
           }
           if (ImGui::MenuItem("Show All")) {
               hiddenEntities_.clear();
           }
           if (ImGui::MenuItem("Toggle Selected") && !selection_.empty()) {
               for (auto id : selection_.ids) {
                   if (hiddenEntities_.count(id)) hiddenEntities_.erase(id);
                   else hiddenEntities_.insert(id);
               }
           }
           ImGui::EndPopup();
       }
   }
   ```
4. **In keyboard handling block** (around line 1300):
   ```cpp
   if (alt && ImGui::IsKeyPressed(ImGuiKey_H) && !shift && !selection_.empty()) {
       for (auto id : selection_.ids) hiddenEntities_.insert(id);
       setStatus(std::format("Hidden {} entities", selection_.ids.size()));
   }
   if (alt && shift && ImGui::IsKeyPressed(ImGuiKey_H)) {
       hiddenEntities_.clear();
       setStatus("Showing all entities");
   }
   ```
5. **In drawViewport()**, skip rendering hidden entities:
   ```cpp
   if (hiddenEntities_.count(entityId)) continue;  // Skip rendering
   ```
6. **Update docs/EDITOR_TOOL_FIELD_GUIDE.md:**
   - Add: `Alt+H — hide selected entities`
   - Add: `Alt+Shift+H — show all (unhide)`

### Verification Checklist
- [ ] Alt+H hides all selected entities from viewport
- [ ] Alt+Shift+H unhides all entities
- [ ] Toolbar dropdown shows hide/show/toggle options
- [ ] Selection remains intact after hide/show
- [ ] Hidden entities don't render in any viewport mode

### Expected Compilation Time
~25 seconds (EditorApp.cpp + maybe gfx)

---

## Task 2.3: Property Inspector Panel

### Scope
- New panel showing selected entity's properties
- Display transform (translation, rotation, scale) + all custom properties
- Inline editing: click field, type value, Enter to persist
- Type indicators (numeric vs string vs vec3)

### Implementation Steps
1. **Create apps/forge-editor/PropertyInspector.hpp:**
   ```cpp
   #pragma once
   #include <forge/scene.hpp>
   #include <string>

   namespace forge::editor {
   class PropertyInspector {
   public:
       void draw(scene::Scene& scene, const MultiSelection& selection);
   private:
       scene::EntityId lastSelectedId_ = scene::kInvalidEntityId;
   };
   } // namespace forge::editor
   ```
2. **Create apps/forge-editor/PropertyInspector.cpp:**
   - Implement `draw()` method
   - Use ImGui table for 2-column layout (property name | value)
   - For each property, create an editable field based on type
   - On Enter key, update the property in the scene
3. **In EditorApp.hpp:**
   - Add `#include "PropertyInspector.hpp"`
   - Add `PropertyInspector inspector_;` member
   - Add `bool showPropertyInspector_ = false;` toggle
4. **In EditorApp.cpp:**
   - In `drawPanels()` section, add:
     ```cpp
     if (showPropertyInspector_) inspector_.draw(scene_, selection_);
     ```
   - In menu bar (Windows → Panels), add checkbox for "Property Inspector"
5. **Update docs/EDITOR_TOOL_FIELD_GUIDE.md:**
   - Add section: "Property Inspector"
   - Describe inline editing workflow

### Verification Checklist
- [ ] PropertyInspector.hpp/cpp compile cleanly
- [ ] Panel opens when toggled from menu
- [ ] Displays selected entity's transform and properties
- [ ] Clicking a property field focuses it for editing
- [ ] Pressing Enter updates the property in the scene
- [ ] Scene can be saved with updated properties

### Expected Compilation Time
~35 seconds (new files + EditorApp recompile)

---

## Build and Test Workflow

### After Each Task, Run
```bash
# Quick edit build (just editor target)
cmake --build build-editor --target forge-editor --parallel 2>&1 | grep -E "error|warning|Linking"

# Verify tests still pass
./build-tests/bin/test-serial 2>&1 | tail -3

# Manual testing: run editor and verify the feature
./build-editor/bin/forge-editor &
# (Test the feature manually, then close)
```

### Troubleshooting
- If compilation fails with "incomplete type", ensure all headers are included
- If runtime crash, check that `selection_` and `scene_` are initialized
- If hotkey doesn't work, verify `ImGui::IsKeyPressed()` is called in the right scope (usually in `drawToolbar()` or main input handling)

---

## Implementation Order (Recommended)

1. **Task 2.1.B** (Undo/Redo hotkeys) — 20 min
2. **Task 2.2** (Visibility toggle) — 30 min
3. **Task 2.3** (Property Inspector) — 45 min
4. **Testing + refinement** — 15 min
5. **Total estimated time** — ~2 hours

---

## Success Criteria for Phase 2

When all three tasks are complete:
- ✅ Ctrl+Shift+Z shows redo status message
- ✅ Alt+H hides selected, Alt+Shift+H shows all
- ✅ Property Inspector panel displays and allows inline editing
- ✅ All three features integrated into editor menu bar
- ✅ Hotkeys documented in EDITOR_TOOL_FIELD_GUIDE.md
- ✅ Editor builds cleanly with no new warnings
- ✅ Tests still pass (9/9)

---

## Context for Next Session

- Serializer layer is rock-solid with 131 passing assertions
- Editor infrastructure is modern C++23, uses ImGui for UI
- Keyboard shortcut pattern: check `ImGui::IsKeyPressed(ImGuiKey_*)` after getting ctrl/shift/alt flags
- Status messages use `setStatus(message)` with 1-second display lifetime
- Selection tracking via `selection_.ids` (std::set of EntityId)
- Scene state stored in `scene_`, materials in `materialLibrary_`, editor state in `entityLayers_`, etc.

---

**Estimated Total Session Duration:** 2-3 hours  
**Difficulty:** Medium (mostly wiring UI to existing infrastructure)  
**Risk:** Low (isolated features, no major architectural changes)
