# Forge Project File Schema

**Current Version:** 0.2.0  
**File Extension:** `.forge`  
**File Format:** JSON (UTF-8, pretty-printed)

---

## Version History

### 0.1.0 (Legacy)
**Supported:** Read-only (backward compatible)  
**Content:**
- Scene entities (brushes, points, meshes)
- Lighting and fog settings
- Entity properties
- **No material library** — materials are embedded inline per face
- **No editor metadata** — no layers, groups, visibility settings

**Wire Format:**
```json
{
  "forge_version": "0.1.0",
  "name": "Level Name",
  "author": "Author Name",
  "entities": { /* ... */ },
  "lighting": { /* ... */ },
  "fog": { /* ... */ }
}
```

### 0.2.0 (Current)
**Supported:** Read/write (full support)  
**Content:**
- Scene entities (brushes, points, meshes) — same as 0.1.0
- Material library (deduplicated, shared by all entities)
- Editor metadata (layers, groups, visibility, locks, tints)
- Lighting and fog settings
- **New:** Face references use `materialId` pointing to the library
- **New:** All entity metadata preserved (layers, groups, user-defined flags)

**Wire Format:**
```json
{
  "forge_version": "0.2.0",
  "name": "Level Name",
  "author": "Author Name",
  "entities": { /* ... */ },
  "materials": { /* ... */ },
  "editor": { /* ... */ },
  "lighting": { /* ... */ },
  "fog": { /* ... */ }
}
```

---

## Top-Level Structure

### Root Object
```json
{
  "forge_version": "0.2.0",
  "name": "string",
  "author": "string",
  "entities": { "<id>": { ... }, ... },
  "materials": { "<id>": { ... }, ... },
  "editor": { ... },
  "lighting": { ... },
  "fog": { ... }
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `forge_version` | string | Yes | Semver format (e.g., "0.2.0") |
| `name` | string | Yes | Level name |
| `author` | string | No | Author name (defaults to "") |
| `entities` | object | Yes | Map of EntityId → Entity object |
| `materials` | object | No | Map of MaterialId → Material object |
| `editor` | object | No | Editor metadata (layers, groups, visibility) |
| `lighting` | object | No | Lighting configuration |
| `fog` | object | No | Fog configuration |

---

## Entities

### Entity ID Format
Entity IDs are unique numeric identifiers assigned by the scene at creation time. Serialized as strings in JSON.

```json
"42": { /* entity data */ }
```

### Entity Object (Base)
```json
{
  "type": "brush" | "point" | "mesh",
  "name": "string",
  "classname": "string",
  "transform": { ... },
  "properties": { ... },
  ...
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `type` | string | Yes | "brush", "point", or "mesh" |
| `name` | string | No | Entity display name |
| `classname` | string | Yes | Entity class (e.g., "world_spawn", "light") |
| `transform` | object | Yes | Position, rotation, scale |
| `properties` | object | Yes | Entity-specific key-value pairs |
| `solid` | bool | No | Brush only; whether entity collides |
| `visible` | bool | No | Brush only; whether entity renders |
| `brushes` | array | No | Brush only; array of brush objects |

### Brush Entity
```json
{
  "type": "brush",
  "name": "BuildingWall",
  "classname": "func_wall",
  "solid": true,
  "visible": true,
  "transform": { ... },
  "properties": { ... },
  "brushes": [ { ... }, ... ]
}
```

### Point Entity
```json
{
  "type": "point",
  "name": "LightSource",
  "classname": "light",
  "transform": { ... },
  "properties": {
    "brightness": { "t": "f", "v": 100.0 },
    "color": { "t": "v3", "v": [1.0, 1.0, 1.0] }
  }
}
```

### Mesh Entity
```json
{
  "type": "mesh",
  "name": "Model",
  "classname": "model_prop",
  "transform": { ... },
  "properties": {
    "model": { "t": "s", "v": "models/mymodel.glb" }
  }
}
```

### Transform Object
```json
{
  "translation": [x, y, z],
  "rotation": [x, y, z, w],
  "scale": [x, y, z]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `translation` | [number, number, number] | 3D position (double precision) |
| `rotation` | [number, number, number, number] | Quaternion (x, y, z, w) in double precision |
| `scale` | [number, number, number] | 3D scale vector (double precision) |

### Properties Object
```json
{
  "prop_name": {
    "t": "s" | "i" | "f" | "b" | "v3",
    "v": value
  },
  ...
}
```

| Type Code | Value Type | Example |
|-----------|-----------|---------|
| `"s"` | string | `{ "t": "s", "v": "texture_name" }` |
| `"i"` | integer | `{ "t": "i", "v": 42 }` |
| `"f"` | float | `{ "t": "f", "v": 3.14 }` |
| `"b"` | boolean | `{ "t": "b", "v": true }` |
| `"v3"` | vec3 | `{ "t": "v3", "v": [1.0, 2.0, 3.0] }` |

---

## Brushes

### Brush Object
```json
{
  "id": "brush_0",
  "faces": [ { ... }, ... ]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | string | Unique identifier within the entity |
| `faces` | array | Array of face objects |

### Face Object
```json
{
  "plane": {
    "normal": [x, y, z],
    "distance": number
  },
  "materialId": "mat_0",
  "uvOffset": [u, v],
  "uvScale": [u, v],
  "uvRotation": degrees
}
```

| Field | Type | Notes |
|-------|------|-------|
| `plane` | object | Half-plane definition (normal + distance) |
| `materialId` | string | Reference to material library (0.2.0+) or inline material ID |
| `uvOffset` | [number, number] | Texture coordinate offset |
| `uvScale` | [number, number] | Texture coordinate scale |
| `uvRotation` | number | Rotation in degrees |

---

## Material Library

### Materials Object
```json
{
  "mat_0": { ... },
  "mat_1": { ... }
}
```

### Material Entry
```json
{
  "name": "string",
  "baseColor": [r, g, b, a],
  "metallicFactor": 0.0 - 1.0,
  "roughnessFactor": 0.0 - 1.0,
  "textures": {
    "baseColor": "texture_id_0",
    "normal": "texture_id_1",
    "metallic": "texture_id_2",
    "roughness": "texture_id_3"
  }
}
```

| Field | Type | Notes |
|-------|------|-------|
| `name` | string | Material name |
| `baseColor` | [number, number, number, number] | RGBA color (0.0–1.0) |
| `metallicFactor` | number | Metallic amount (0.0–1.0) |
| `roughnessFactor` | number | Roughness amount (0.0–1.0) |
| `textures` | object | Optional texture references by slot |

---

## Editor Metadata

### Editor Object
```json
{
  "entityLayers": { "42": "layer_name", ... },
  "entityGroups": { "42": "group_name", ... },
  "layerVisibility": { "layer_name": true, ... },
  "layerLocked": { "layer_name": false, ... },
  "layerTint": { "layer_name": [r, g, b], ... },
  "soloLayer": "layer_name",
  "groupFilter": "group_name"
}
```

| Field | Type | Notes |
|-------|------|-------|
| `entityLayers` | object | Map of EntityId → layer name |
| `entityGroups` | object | Map of EntityId → group name |
| `layerVisibility` | object | Map of layer name → visibility (bool) |
| `layerLocked` | object | Map of layer name → locked (bool) |
| `layerTint` | object | Map of layer name → RGB tint |
| `soloLayer` | string | Active solo layer (if any) |
| `groupFilter` | string | Active group filter (if any) |

---

## Lighting

### Lighting Object
```json
{
  "ambientColor": [r, g, b],
  "sunDirection": [x, y, z],
  "sunColor": [r, g, b],
  "sunIntensity": 1.0,
  "sunShadowBias": 0.0005
}
```

---

## Fog

### Fog Object
```json
{
  "density": 0.01,
  "color": [r, g, b]
}
```

---

## Compatibility Rules

### Backward Compatibility (0.1.0 → 0.2.0)
- Files without `materials` or `editor` sections are loaded with:
  - Empty material library (materials synthesized from inline face colors)
  - Empty editor metadata (all entities in default layer)
- Scene-level parsing is identical; unknown fields are silently ignored

### Forward Compatibility (0.2.0 → future)
- Unknown fields at any level are silently ignored
- Unknown fields inside nested objects (properties, materials, editor metadata) are preserved but ignored
- Version mismatch triggers a `schemaNote` in the loaded `ProjectData` but does not fail the load
- Entity IDs remain valid across versions

### Migration Path
Future schema versions will:
1. Increment the minor version (e.g., 0.3.0) for additive changes
2. Increment the major version (e.g., 1.0.0) for breaking changes
3. Specify migration rules in this document
4. Provide migration tools in the editor if needed

---

## Example: Complete 0.2.0 Project

```json
{
  "forge_version": "0.2.0",
  "name": "TestLevel",
  "author": "Demo",
  "entities": {
    "0": {
      "type": "brush",
      "name": "Floor",
      "classname": "world_brush",
      "solid": true,
      "visible": true,
      "transform": {
        "translation": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "properties": {
        "textureName": { "t": "s", "v": "textures/floor" }
      },
      "brushes": [
        {
          "id": "brush_0",
          "faces": [
            {
              "plane": { "normal": [0, 0, 1], "distance": 0 },
              "materialId": "mat_0",
              "uvOffset": [0, 0],
              "uvScale": [1, 1],
              "uvRotation": 0
            }
          ]
        }
      ]
    },
    "1": {
      "type": "point",
      "name": "PlayerSpawn",
      "classname": "spawn_point",
      "transform": {
        "translation": [64, 64, 32],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "properties": {
        "team": { "t": "i", "v": 0 }
      }
    }
  },
  "materials": {
    "mat_0": {
      "name": "FloorMaterial",
      "baseColor": [0.8, 0.8, 0.8, 1.0],
      "metallicFactor": 0.0,
      "roughnessFactor": 0.7,
      "textures": {}
    }
  },
  "editor": {
    "entityLayers": { "0": "geo", "1": "spawns" },
    "entityGroups": {},
    "layerVisibility": { "geo": true, "spawns": true },
    "layerLocked": { "geo": false, "spawns": false },
    "layerTint": { "geo": [1, 1, 1], "spawns": [0.5, 1, 0.5] },
    "soloLayer": "",
    "groupFilter": ""
  },
  "lighting": {
    "ambientColor": [0.3, 0.3, 0.3],
    "sunDirection": [0.707, 0.707, 0],
    "sunColor": [1, 1, 0.95],
    "sunIntensity": 1.2,
    "sunShadowBias": 0.0005
  },
  "fog": {
    "density": 0.0,
    "color": [0.5, 0.5, 0.5]
  }
}
```

---

## Validation Notes

When loading a project file:
1. **Version check:** If `forge_version` does not match current (0.2.0), emit a compatibility warning (not a failure)
2. **Unknown fields:** Silently ignore fields not in this schema
3. **Invalid entity IDs:** If an ID is corrupted (non-numeric), skip the entity and emit a warning
4. **Missing required fields:** If a required field is missing, use a sensible default and emit a warning
5. **Type mismatches:** If a field has the wrong type (e.g., string where number expected), attempt type coercion; if impossible, use default and warn

---

## Future Versions

### 0.3.0 (Planned)
- **Mesh support:** Full per-vertex storage of UVs, normals, tangents
- **Constraints:** Entity soft-body and hinge constraints for dynamic simulation
- **Terrain:** Heightfield terrain chunk support

### 1.0.0 (Planned)
- **Breaking change:** Rename all fields to use `snake_case` throughout
- **Material overhaul:** Support for material layering and blending modes
- **Texture atlasing:** Built-in support for texture atlases to optimize draw calls

---

## Reference: Version Comparison Helpers

The serializer provides helpers in `forge/serial/SceneSerializer.hpp`:

```cpp
// Parse a semver version string
std::tuple<int, int, int> parseVersion(std::string_view version) noexcept;

// Compare two semver versions
// Returns: negative if a < b, 0 if a == b, positive if a > b
int compareVersions(std::string_view a, std::string_view b) noexcept;
```

**Usage:**
```cpp
auto [major, minor, patch] = parseVersion("0.2.0");
int cmp = compareVersions("0.2.0", "0.1.0");  // returns positive (0.2.0 > 0.1.0)
```
