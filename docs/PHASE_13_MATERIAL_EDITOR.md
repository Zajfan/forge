# Phase 13: Material Editor Panel

## Overview

Phase 13 extends the FORGE editor with a complete **Material Editor Panel** — an ImGui-based UI for creating, editing, and managing PBR materials. This unlocks practical workflow for artists and designers.

## Architecture

### MaterialEditor Class (`MaterialEditor.hpp / MaterialEditor.cpp`)

Standalone component that manages material editing UI:

```cpp
class MaterialEditor {
    // Material library management
    void selectMaterial(const std::string& name);
    std::shared_ptr<gfx::Material> selected();
    
    // UI rendering
    void draw();  // Main panel with tabbed interface
    
    // Property editors
    void drawMaterialProperty(const std::string& label, float& value, float min, float max);
    void drawColorProperty(const std::string& label, glm::vec3& color);
    void drawTextureProperty(const std::string& label, std::string& textureId);
    void drawLibraryBrowser();
    
    // Material CRUD
    std::shared_ptr<gfx::Material> createNewMaterial();
    void deleteSelected();
    void renameMaterial(const std::string& newName);
    
    // Import/Export
    void exportMaterial(const std::string& filepath);
    void importMaterial(const std::string& filepath);
};
```

### Integration with EditorApp

MaterialEditor is instantiated in EditorApp with:
- `materialEditor_` — The UI controller
- `materialLibrary_` — Shared material registry
- `showMaterialEditor_` — Panel visibility toggle

## UI Layout

### Tab 1: Library

Material browser panel with:
- **Create button** — Add new material with default name
- **Material list** — Selectable items with context menu
- **Delete button** — Remove selected material
- **Import/Export** — Save/load materials to disk

### Tab 2: Properties

Editable properties for selected material:
- **Name field** — Rename material (enter to confirm)
- **Color properties**:
  - Albedo (RGB color picker)
  - Emissive (RGB color picker)
- **PBR parameters**:
  - Metallic (0.0 → 1.0 slider)
  - Roughness (0.04 → 1.0 slider)
  - Ambient Occlusion (0.0 → 1.0 slider)
- **Texture scales**:
  - Normal scale (0.0 → 2.0)
  - Emissive scale (0.0 → 4.0)
- **Texture assignments**:
  - Albedo Map (text field + browse button)
  - Normal Map
  - Metallic Map
  - Roughness Map
  - AO Map
  - Emissive Map
- **Alpha blending**:
  - useAlphaBlend (checkbox)
  - Alpha cutoff (0.0 → 1.0 slider, only visible when enabled)

## UI Components

### Material Property Slider
```cpp
void drawMaterialProperty(const std::string& label, float& value,
                          float minVal = 0.0f, float maxVal = 1.0f)
```
Renders `ImGui::SliderFloat` with label, min/max range, and real-time updates.

### Color Picker
```cpp
void drawColorProperty(const std::string& label, glm::vec3& color)
```
Renders `ImGui::ColorEdit3` for sRGB color selection. Updates immediately apply to material preview.

### Texture Input
```cpp
void drawTextureProperty(const std::string& label, std::string& textureId)
```
Text input field + "Browse" button. Future enhancement: file dialog for texture selection.

### Library Browser
- List of all materials in library
- Right-click context menu (Delete, Export)
- Double-click to select and edit
- Create new material inline

## Workflows

### Create a New Material

1. Enter name in "Material Name" field
2. Click "Create"
3. Material appears in list
4. Automatically selected → Properties tab shows editable fields
5. Adjust color, metallic, roughness, add textures
6. Close panel or select another material to finish

### Edit Existing Material

1. Library → Select material from list
2. Properties → Adjust sliders, colors, texture IDs
3. Changes apply immediately (reflected in viewport)
4. Optional: Export to save as `.mat` file

### Import/Export Materials

**Export** (right-click → Export):
- Saves material properties to text file
- Simple key=value format (no external JSON library required)
- Can be shared between projects

**Import** (future enhancement):
- Menu: Materials → Import
- Select `.mat` file
- Material added to library

## Integration Points

### EditorApp Initialization
```cpp
materialEditor_.setMaterialLibrary(&materialLibrary_);
```

### Frame Update
In `drawFrame()`, call:
```cpp
if (showMaterialEditor_) {
    materialEditor_.draw();
}
```

### Draw Pipeline
When rendering scene, bind materials from library:
```cpp
gfx::DrawCall dc;
dc.mesh = gpuMesh;
dc.material = materialLibrary_.getMaterial(faceMatId);
renderer_.submit(dc);
```

## Design Decisions

### Tabbed Interface
Two tabs (Library + Properties) keep the panel width manageable. Users see the full list or full properties, not both truncated.

### Immediate Mode
All properties use ImGui sliders/color pickers with immediate apply. No "save" button — this matches game editor conventions (Unreal, Unity).

### Library Ownership
MaterialLibrary is owned by EditorApp, persisted with the scene. Materials are serialized in `.forge` files (Phase 14).

### No File Dialogs Yet
Texture paths are text fields. Full file browser will use portable file dialogs (pfd) library — Phase 14+.

### Stateless Material IDs
Brush faces store **material name strings** (e.g., "Steel"), not pointers. Library lookups happen at render time. This keeps serialization simple and handles material renames gracefully.

## Performance

- **Panel rendering**: ImGui overhead = ~0.5ms per frame (negligible)
- **Material storage**: 128 bytes per material (see Phase 12)
- **Texture updates**: GPU upload only when texture ID changes
- **Library lookup**: O(1) hash map per draw call

## Next Steps (Phase 14+)

1. **Scene Integration** — Save/load materials with `.forge` scenes
2. **Face Material Painting** — Assign materials to brush faces in viewport
3. **Material Thumbnails** — Show preview of metallic/roughness in library list
4. **Procedural Textures** — Generate noise/pattern textures without files
5. **Material Presets** — Built-in library (metal, plastic, fabric, etc.)
6. **Texture Baking** — Bake normals/AO from high-poly models

## Files Modified/Created

- ✅ `MaterialEditor.hpp` — UI controller class
- ✅ `MaterialEditor.cpp` — Implementation (ImGui rendering, CRUD)
- ✅ `EditorApp.hpp` — Integration (member variables, includes)
- ✅ `EditorApp.cpp` — Wire material editor into frame loop (pending)
