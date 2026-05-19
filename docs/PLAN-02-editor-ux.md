# Phase 2: Editor UX Foundations
**Goal:** Improve editor responsiveness, multi-entity workflows, and property inspection. Make common operations (undo/redo feedback, multi-select visibility, entity property inspection) fast and visual.

**Context:** Editor is functional with validation, keyboard nav, and dual viewport. Now we need UX polish: better feedback on edits, faster entity management, and property editing.

---

## Tasks

### Task 2.1: Add Undo/Redo Stack with Status Feedback
**Type:** Feature (Infrastructure + UI)

**Action:**
1. Create `apps/forge-editor/UndoStack.hpp` — simple `UndoStack` class holding a deque of undo/redo frames, plus `undo()` / `redo()` / `push(frame)` methods
2. Add `undoStack_` member to `EditorApp` + Ctrl+Z / Ctrl+Shift+Z hotkeys
3. Each undo frame captures the full scene state snapshot
4. On undo/redo, reload that snapshot + show status "Undo [frame_name]" for 1s
5. Update `docs/EDITOR_TOOL_FIELD_GUIDE.md` with undo/redo shortcuts

**Files:**
- `apps/forge-editor/UndoStack.hpp` — new file
- `apps/forge-editor/EditorApp.hpp` — add `UndoStack undoStack_`, track state before major ops
- `apps/forge-editor/EditorApp.cpp` — Ctrl+Z/Ctrl+Shift+Z handling, pushes scene snapshot
- `docs/EDITOR_TOOL_FIELD_GUIDE.md` — document undo/redo

**Verify:**
- Ctrl+Z undoes last entity move/creation
- Ctrl+Shift+Z redoes
- Status message shows at top for ~1 second
- No crashes on undo stack overflow (cap at 50 frames)

**Done:** Undo/redo works, hotkeys bound, status feedback visible

---

### Task 2.2: Add Multi-Entity Visibility Toggle from Selection
**Type:** Feature (UI)

**Action:**
1. In `drawToolbar()`, add a dropdown button: "Visibility: [show/hide/toggle]"
2. When dropdown selects "Hide Selected", mark all selected entities with `visible = false`
3. When "Show Selected", set `visible = true`
4. When "Toggle", flip visibility state on each
5. Update Material Editor and validator to respect this flag
6. Add shortcuts: Alt+H (hide), Alt+Shift+H (show all)

**Files:**
- `apps/forge-editor/EditorApp.cpp` — add dropdown in toolbar, Alt+H/Alt+Shift+H hotkeys, update draw loop
- `docs/EDITOR_TOOL_FIELD_GUIDE.md` — document multi-entity visibility

**Verify:**
- Select multiple brush entities
- Press Alt+H — all disappear from viewport
- Press Alt+Shift+H — all reappear
- Toggling visibility doesn't affect selection

**Done:** Visibility toggle works on multi-select, hotkeys functional

---

### Task 2.3: Add Property Inspector Panel
**Type:** Feature (UI)

**Action:**
1. Create `apps/forge-editor/PropertyInspector.hpp/cpp` — panel that displays entity properties in a grid
2. In `drawPanels()`, add `PropertyInspector` panel rendering
3. When an entity is selected, show its transform (translation/rotation/scale) + properties in a table
4. Allow quick inline editing: clicking a property field opens a text/number input
5. On Enter, update the entity property in the scene
6. Show a visual indicator if property is numeric vs string vs vec3

**Files:**
- `apps/forge-editor/PropertyInspector.hpp` — new file, `PropertyInspector` class
- `apps/forge-editor/PropertyInspector.cpp` — new file, rendering + edit logic
- `apps/forge-editor/EditorApp.hpp` — add `PropertyInspector inspector_` member, `showPropertyInspector_` bool
- `apps/forge-editor/EditorApp.cpp` — draw inspector, wire up to selection, menu checkbox for toggle
- `docs/EDITOR_TOOL_FIELD_GUIDE.md` — document Property Inspector

**Verify:**
- Select a point entity with properties (e.g., "team": 0)
- Open Property Inspector panel
- See transform and all properties displayed
- Click a property, edit it, press Enter
- Property is updated in the scene (persist to file on Save)

**Done:** Property Inspector works, inline editing functional

---

## Verification Checklist
- [ ] `cmake --build build-editor --target forge-editor` — 0 build errors
- [ ] Editor starts without crashing
- [ ] Ctrl+Z / Ctrl+Shift+Z undo/redo hotkeys respond with status message
- [ ] Alt+H / Alt+Shift+H multi-entity visibility toggle works
- [ ] Property Inspector panel opens and displays selected entity properties
- [ ] Inline property editing (click, type, Enter) persists changes

## Success Criteria
- Phase 2 complete when:
  - Undo/redo infrastructure is in place + hotkeys working
  - Multi-select visibility toggle implemented + hotkeys functional
  - Property Inspector panel exists and allows inline editing
  - All builds clean, no new warnings

## Output (Summary)
After completion, update `/memories/session/phase-2-summary.md` with:
- Features added: undo/redo, multi-entity visibility, property inspector
- Files created: UndoStack.hpp, PropertyInspector.hpp/cpp
- Files modified: EditorApp.hpp, EditorApp.cpp, EDITOR_TOOL_FIELD_GUIDE.md
- Hotkeys introduced: Ctrl+Z, Ctrl+Shift+Z, Alt+H, Alt+Shift+H
- Any deviations from plan and why

---

**Ready to start?** Phase 2 will make everyday editor workflows much faster.
