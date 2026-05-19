# FORGE Editor Tool Field Guide

This is a practical map of what each editor tool does, where to find it, and how to avoid blind clicking.

## 1) Where Things Are

- Top menu bar:
  - File: scene I/O and export
  - Edit: undo/redo, selection ops, hide/isolate, CSG preview, validation
  - Add: primitives, prefab insertion, prefab browser toggle
  - View: render/debug toggles, panel toggles, frame commands
- Toolbar strip (top dock row): primary tool buttons and quick toggles
- Viewport (center): camera, picking, gizmos, overlays, drag tools
- Scene Tree (right/top): layers, group filter, entity list, entity context actions
- Properties / Inspector (right/bottom): selected entity, face, clip, and operation controls
- Status bar (bottom): status messages, fps, draw calls, triangle count, brush count, grid/snap state
- Status bar (bottom): status messages, recent actions ribbon (last 3 commands), fps, draw calls, triangle count, brush count, grid/snap state

## 2) Tool Palette (Toolbar)

Entity transform tools:
- SEL (Q): select entities
- MOV (W): move selected entities
- ROT (E): rotate selected entities
- SCL (R): scale selected entities

Face and geometry tools:
- FSEL (T): face selection
- FMOV (Y): move selected face along normal
- CLIP (C): clip mode with clip-plane preview
- PAINT (P): paint material to face(s)
- VTEX (V): vertex selection/editing
- BCR: box-create drag tool

Toolbar utility controls:
- Grid checkbox: show/hide grid rendering
- Wire checkbox: wireframe render mode
- [1]/[4]: single viewport vs quad viewport
- Visibility dropdown: Hide Selected / Show Selected / Toggle Selected / Show All
- Ctrl+Alt+Q: toggle single viewport vs quad viewport
- Ctrl+Shift+Z: redo (alternate hotkey, same action as Ctrl+Y)
- Ctrl+Shift+I: toggle Property Inspector panel
- Snap checkbox + grid combo: snapping enable and grid size
- Undo/Redo buttons with mini labels (for example: `Undo Move entity 12`)
- Play/Stop button (F5 / Esc)

## 3) Core Navigation and Selection

Viewport camera:
- Left drag: orbit (in select-style tools)
- Right or middle drag: pan
- Scroll wheel: zoom

Selection:
- Click in viewport: pick entity or face depending on active tool
- Shift + left drag in Select tool: marquee selection
- Ctrl+A: select all visible and unlocked
- Ctrl+Shift+A: select same group (visible + unlocked)
- F: frame selected (or frame all if nothing selected)

## 4) Organization Controls (Hidden, Isolated, Locked)

Most editing behavior is gated by layer/group visibility and lock state.

Where to change it:
- Scene Tree -> Layers section
  - Visibility checkbox per layer
  - Solo button per layer (S)
  - Lock checkbox per layer (L)
  - Tint per layer
- Scene Tree rows and context menu
  - Hide, Isolate, Show All Hidden / Clear Isolation
- Inspector buttons
  - Hide Selected, Isolate Selected, Show All Hidden / Clear Isolation

What those states do:
- Hidden by layer/entity/isolation/group: not editable and generally not pickable for tools
- Locked layer: entity can be visible but not editable
- Status messages often report why actions fail:
  - Layer is hidden.
  - Layer is locked.
  - Entity hidden by layer/group filter.

Quick visibility hotkeys:
- Alt+H: hide all currently selected entities
- Alt+Shift+H: clear hidden entity state (show all hidden entities)

Toolbar visibility actions:
- Hide Selected: hide every selected entity
- Show Selected: unhide only currently selected entities
- Toggle Selected: flip hidden state per selected entity
- Show All: clear all entity-level hidden flags

Notes:
- Hide/Show hotkeys only affect entity-level hidden state.
- Layer solo/visibility filters still apply after unhide-all.

## 5) Property Inspector (Panel + Inline Edit)

Where to open it:
- View -> Property Inspector
- Ctrl+Shift+I

Current behavior:
- Shows details for single-selection only.
- Multi-selection shows a guard message and disables editing.
- Displays read-only transform values:
  - Translation
  - Rotation (quaternion)
  - Scale
- Displays custom key/value properties for editable entity types.

Inline editing flow:
1. Select one entity.
2. Open Property Inspector.
3. Click a property value cell.
4. Edit the value in the popup dialog.
6. Press Enter or click Apply to commit.
7. Press Escape or click Cancel to discard.
8. Property edits are now undoable/redoable through the normal command history.
9. Main Inspector and Property Inspector now use the same command-backed property edit path.

Type-aware edits:
- string: stored as entered
- int: parsed with integer conversion
- float: parsed with float conversion
- bool: `true`/`1` => true, otherwise false
- vec3: parsed from `x, y, z`

Validation notes:
- Invalid numeric/vector input is ignored and leaves the previous value unchanged.
- Mesh entities currently show transform/entity info only (no custom property map).
- Popup shows lightweight type hints (int/float/bool/vec3 expected format).
- In main Inspector, brush `Solid` and `Visible` toggles are command-backed (undo/redo).
- In main Inspector, Brush `Classname` edits are command-backed (undo/redo) and preserve class default properties.
- In main Inspector, Point Entity `Classname` edits are also command-backed with property snapshots for consistent undo/redo.

## 6) Paint Tool Behavior (Exact Current Rules)

Entry points:
- Activate PAINT via toolbar button or P
- Paint mode shows a small viewport HUD legend with controls (LMB paint, MMB pick, RMB cancel)

Paint action model:
- Left click on a valid face starts a paint drag batch
- Middle click samples the clicked face material into current Paint material (eyedropper)
- While holding left mouse:
  - faces under cursor are accumulated
  - duplicates are ignored
  - batch is capped by Queue Cap (configurable in Inspector -> Paint)
  - queued faces are highlighted in the viewport with a live queue count label
  - a small "queue full" hint appears when the cap is reached
- Left release commits one batch command for all accumulated faces
- Right click cancels current paint drag

Hover highlight model:
- Hover highlight appears only when all are true:
  - active tool is Paint
  - mouse is over viewport item
  - not currently dragging a paint batch
  - picked face belongs to an entity that is visible in editor and not layer-locked
- Hover highlight is cleared when:
  - paint drag begins
  - paint drag ends
  - right-click cancel is used
  - cursor leaves the viewport item
  - hovered face is hidden/isolated-out or locked

Interpretation:
- If you do not see hover highlight in Paint mode, first check Layers visibility, Solo state, lock state, and isolation/hidden filters.
- During drag, no hover preview is expected (this is by design).

## 7) Recommended Daily Workflow (No Guessing)

1. Start in SEL (Q), frame your working area (F).
2. In Scene Tree, confirm target layer is visible and unlocked.
3. Choose operation tool from toolbar (FSEL/FMOV/CLIP/PAINT/BCR).
4. Use Inspector for operation parameters.
5. Keep Status bar visible for immediate feedback.
6. If behavior seems wrong, clear hidden/isolation and re-check layer lock.

## 8) High-Value Menus You Will Use Often

File:
- New/Open/Save/Save As
- Export OBJ, MAP, GLTF/GLB

Edit:
- Undo/Redo (Ctrl+Z undo, Ctrl+Y redo, Ctrl+Shift+Z redo)
- Duplicate Selected
- Align to Primary (multi-select): align edges/centers on X/Y/Z to the primary selected entity
- Geometry Snap: Drop to Surface + axis-constrained snap to nearest hit geometry (X/Y/Z)
- Hide/Isolate/Clear Hidden
- CSG preview modes (Subtract/Union/Intersect/XOR)
- Run Validation (F9)
- Snap All to Grid

Validation panel navigation:
- Click an issue row to select and frame the referenced entity.
- **Up / Down** — move keyboard focus through the issue list (repeating, panel must have focus).
- **Enter** — jump to (select + frame) the currently focused issue entity.
- A status line at the bottom of the panel shows the available keys.
- Scene-wide issues remain non-targeted (they do not jump to a single entity).

Add:
- Primitive library (box, wedge, prism, pyramid, cylinder, cone, sphere, torus)
- Insert Prefab
- Prefab Browser toggle

View:
- Grid/Wire/Skybox/Fog/Shadows/Bloom
- Build BSP / BSP Stats
- Script Console / Trigger Debug Overlay
- Orthographic Edit Mode
- Property Inspector
- Panel visibility toggles

## 9) Align To Primary (Multi-Selection)

When you have at least 2 entities selected, you can align all secondary entities to the first selected one (the primary).

Where to use it:
- Edit -> Align to Primary
- Inspector -> Align to Primary section (appears when 2+ entities are selected)

Available alignments:
- X axis: Left / Center / Right
- Y axis: Bottom / Center / Top
- Z axis: Front / Center / Back

Center-align shortcuts:
- Ctrl+Shift+1: align centers on X
- Ctrl+Shift+2: align centers on Y
- Ctrl+Shift+3: align centers on Z

Behavior notes:
- Primary entity is the first selected entity and is not moved.
- The operation creates one undoable batch move command.
- If the primary has invalid bounds, alignment is canceled with a status message.

## 10) Material Delete Safety

When deleting a material from Material Editor:
- A confirmation dialog appears before deletion.
- The editor performs a lightweight scene usage scan.
- If the material is still referenced, the dialog warns with:
  - number of faces using the material
  - number of brush entities containing those faces

This helps avoid accidental deletion of in-use materials.

Additional safety:
- Delete is disabled until you type the exact material name in the confirmation dialog.

Material file workflows:
- Texture fields now support Browse buttons (image file picker).
- Import/Export path now supports Browse Import... and Browse Export... buttons.
- Texture fields now show quick thumbnail previews for assigned textures.
- You can drag textures from the "Texture Assets" list in Material Editor onto texture fields.

Material reference replacement workflow:
- Rename: if the old material is used by brush faces, references are automatically updated to the new material name.
- Delete: if the material is referenced, the confirmation dialog can replace references with another material before deletion.

Scene persistence:
- Material library data is now saved and loaded with `.forge` scenes.
- Layer/group editor metadata (entity layer/group assignments, layer visibility/lock/tint, solo layer, group filter) now round-trips with `.forge` scenes.
- Serializer now records and reads `forge_version` with compatibility parsing notes for future schema evolution.

## 11) Snap Capabilities (Current)

What exists now:
- Grid snapping toggle + grid size selector (toolbar).
- Snap All to Grid command (Edit menu).
- Grid-aware transforms and creation workflows (for example nudges and box-create sizing).
- Drop to Surface (Edit -> Geometry Snap, Ctrl+Shift+G): casts downward and moves selected entities to first supporting surface.
- Axis-constrained geometry snap (Edit -> Geometry Snap): snaps selection to nearest hit geometry along one axis.
  - Ctrl+Alt+1: X axis
  - Ctrl+Alt+2: Y axis
  - Ctrl+Alt+3: Z axis

Current practical limitation:
- Geometry snap uses lightweight ray sampling from selection bounds, so very irregular concave arrangements may still need a manual nudge/alignment pass.

Orthographic edit mode shortcut:
- Ctrl+Alt+O toggles the orthographic quad editing layout on and off.

Practical workflow today for indoor blockouts:
- Keep Snap enabled with a consistent grid size.
- Build shell pieces on-grid.
- Use Align to Primary plus Geometry Snap to place wall/roof/floor pieces quickly.
- Run Snap All to Grid periodically to eliminate drift.

## 12) Quick Troubleshooting Checklist

If a tool does nothing:
- Confirm correct active tool in toolbar
- Confirm entity/face is visible in current filters
- Confirm layer is not locked
- Confirm play mode is not interfering with intended edit flow
- Watch status bar messages after each click

If Paint hover does not appear:
- Confirm tool is PAINT
- Confirm mouse is inside viewport
- Confirm you are not currently dragging paint
- Confirm target entity is visible and unlocked

## 13) 60-Second Manual QA Checklist (Release Smoke)

Goal:
- Confirm editor startup and the three recent UX features work end-to-end in under 1 minute.

Setup (5 seconds):
- [ ] Open any small `.forge` map with at least one editable entity that has custom properties.

Checklist (target: ~60 seconds total):
- [ ] Startup sanity (5-10s): Launch editor; pass if viewport renders and no immediate crash occurs.
- [ ] Redo alternate hotkey (10-15s): Move one entity, press Ctrl+Z then Ctrl+Shift+Z; pass if moved state is restored and status feedback appears.
- [ ] Visibility hotkeys (10-15s): Select entity/ies, press Alt+H then Alt+Shift+H; pass if entities hide then reappear.
- [ ] Visibility dropdown (10-15s): Use toolbar Visibility -> Toggle Selected; pass if selected entities flip hidden state while selection remains intact.
- [ ] Property Inspector edit flow (20-25s): Open View -> Property Inspector, edit one property, press Enter (or Apply); pass if value persists after reselect.
- [ ] Undo/redo for property edit (10s): After applying a Property Inspector edit, press Ctrl+Z then Ctrl+Shift+Z; pass if property value reverts then restores.

Quick fail cues:
- [ ] Hotkey does nothing while viewport has focus.
- [ ] Property popup opens but Apply does not change value.
- [ ] Hidden entities do not restore after Alt+Shift+H.

Release recommendation:
- If any step fails, do not tag release until reproduced once and logged with map name + exact key sequence.

---

If you want, the next pass can add screenshots and callouts so this becomes a true click-by-click onboarding sheet.
