# FORGE — Architecture Document
**C++23 Native Desktop — Brush Geometry Kernel & Game Engine**
*v0.2 — C++23 Native Revision*

---

## Vision

A unified environment where the geometry you model is the geometry the engine runs.
No roundtrip exports. No disconnect between editor and runtime. Build a crate. Build a dungeon. Press play.
The brush is the art asset, the physics collider, and the BSP primitive — simultaneously.

TrenchBroom's geometry model × Blender's old Game Engine ambition × native C++23 execution.

---

## Core Philosophy

### 1. The Brush is the Atom
All solid geometry is a **brush** — a convex solid defined entirely by a set of half-spaces (planes).
Vertices and edges are computed, never stored. This is not a limitation — it makes CSG trivial,
collision exact, and numerical stability manageable.

### 2. Exact Where It Matters
Plane-side tests use **exact arithmetic predicates** (via CGAL's filtered kernels) at the geometry
kernel boundary. Everything else uses `float` or `double` as appropriate. You get robustness without
paying for exact arithmetic everywhere.

### 3. One Codebase, Two Modes
The same C++ modules power both the **Editor** and the **Runtime**. The editor is the runtime with
tool systems attached. Flip between edit and play without relaunching.

### 4. No Hidden Magic
No reflection framework, no script VM (optional later), no over-engineered ECS. Plain C++ structs,
explicit ownership via `std::unique_ptr` / `std::shared_ptr`, and a simple event bus. Readable code
that a solo developer can hold in their head.

---

## High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                        FORGE EDITOR                              │
│            Dear ImGui UI + Docking + Custom Widgets              │
│            Viewport (bgfx / OpenGL 4.6 render target)           │
│            Tool System (Select / Face / Vertex / CSG / Paint)   │
└─────────────────────────────┬────────────────────────────────────┘
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│                        FORGE ENGINE                              │
│                                                                  │
│  ┌──────────────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │  Geometry        │  │  Scene Graph  │  │  Game Runtime    │  │
│  │  Kernel          │◄─│  (ECS-lite)   │─►│  (loop/input/    │  │
│  │  forge::geo      │  │  forge::scene │  │   scripting)     │  │
│  └────────┬─────────┘  └───────────────┘  └──────────────────┘  │
│           │                                                       │
│  ┌────────▼─────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │  Mesh Builder    │  │  Renderer     │  │  Physics         │  │
│  │  forge::build    │─►│  forge::gfx   │  │  forge::phys     │  │
│  │  brush→VAO/VBO   │  │  bgfx backend │  │  Jolt Physics    │  │
│  └──────────────────┘  └───────────────┘  └──────────────────┘  │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   Export System forge::export             │  │
│  │         GLTF 2.0  │  OBJ+MTL  │  Quake MAP  │  STL       │  │
│  └───────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## Tech Stack

| Layer            | Library / Technology         | Rationale                                              |
|------------------|------------------------------|--------------------------------------------------------|
| Language         | C++23                        | Modules, `std::expected`, ranges, concepts, `std::flat_map` |
| Build            | CMake 3.28+ + vcpkg          | Industry standard, manifest mode for deps              |
| Math             | GLM 1.0                      | Header-only, GLSL-compatible API, SIMD paths           |
| Geometry robustness | CGAL (Exact Predicates Inexact Constructions kernel) | Exact plane-side tests, no flip artifacts |
| Renderer         | bgfx                         | Cross-platform (OpenGL/Vulkan/Metal), one API          |
| Physics          | Jolt Physics                 | Modern, deterministic, native ConvexHull shapes        |
| Editor UI        | Dear ImGui + ImGuizmo        | Purpose-built for tools, immediate mode, no overhead   |
| Windowing/Input  | SDL3                         | Cross-platform, gamepad support, clean C API           |
| Audio            | miniaudio                    | Single-header, zero deps, spatial audio                |
| Serialization    | nlohmann/json + custom binary| JSON for scenes/assets, binary for fast load           |
| Testing          | Catch2 v3                    | Header-light, BDD-style geometry test cases            |
| Packaging        | CPack + AppImage (Linux)     | Distributable desktop binary                           |

### C++23 Features Used

- **Modules** — `import forge.geometry;` instead of header hell
- **`std::expected<T, E>`** — error handling in geometry ops without exceptions
- **Ranges & Views** — iterate face polygons, filter entities cleanly
- **`std::flat_map`** — cache-friendly entity component storage
- **Deducing `this`** — fluent builder patterns in scene graph
- **`std::mdspan`** — vertex buffer views over raw GPU memory
- **Structured bindings everywhere** — readable geometry decomposition

---

## Module 1: Geometry Kernel — `forge::geo`

The heart of the system. Zero dependencies on renderer or physics. Fully unit-testable in isolation.

### Fundamental Types

```cpp
// Vec3 is GLM's glm::dvec3 inside the kernel (double precision)
// Exported as glm::vec3 (float) to the renderer

namespace forge::geo {

struct Plane {
    glm::dvec3 normal;   // unit length
    double     distance; // dot(normal, point_on_plane)

    // Signed distance from plane to point
    // > 0 = front (outside), < 0 = back (inside), 0 = on plane
    [[nodiscard]] double eval(glm::dvec3 p) const noexcept {
        return glm::dot(normal, p) - distance;
    }

    [[nodiscard]] Plane flipped() const noexcept {
        return { -normal, -distance };
    }
};

// PointSide: which side of a plane a point is on
// Uses CGAL exact predicate at the kernel boundary
enum class Side : int8_t { Front = 1, On = 0, Back = -1 };
Side classifyPoint(const Plane& plane, glm::dvec3 point) noexcept;

struct BrushFace {
    Plane        plane;
    std::string  materialId;
    glm::vec2    uvOffset  = {0.f, 0.f};
    glm::vec2    uvScale   = {1.f, 1.f};
    float        uvRotation = 0.f;
};

// A convex solid — the intersection of N half-spaces
// Minimum valid brush: 4 faces (tetrahedron)
struct Brush {
    std::string              id;
    std::vector<BrushFace>   faces;

    // Computed on demand, cached until invalidated
    mutable std::optional<std::vector<glm::dvec3>> cachedVertices;

    [[nodiscard]] bool isValid()   const noexcept; // convex + closed + non-degenerate
    [[nodiscard]] AABB bounds()    const noexcept;
    void invalidateCache()               noexcept { cachedVertices.reset(); }
};

} // namespace forge::geo
```

### Vertex Computation — 3-Plane Intersection

For every triple of face planes (P_i, P_j, P_k), solve the linear system:

```
| n_i.x  n_i.y  n_i.z | | x |   | d_i |
| n_j.x  n_j.y  n_j.z | | y | = | d_j |
| n_k.x  n_k.y  n_k.z | | z |   | d_k |
```

A solution is a valid brush vertex only if it lies on the inside (Back or On) of every other face plane.

```cpp
namespace forge::geo {

std::optional<glm::dvec3>
intersectThreePlanes(const Plane& a, const Plane& b, const Plane& c) noexcept;

// Returns deduplicated vertex list — O(F^3) but F is small (4–32 faces)
std::vector<glm::dvec3>
computeBrushVertices(const Brush& brush) noexcept;

} // namespace forge::geo
```

### Face Polygon Computation — Sutherland-Hodgman Clipping

Each face polygon starts as a large quad (±65536 units) on its own plane,
then gets clipped by every other face plane:

```cpp
// Returns the convex polygon for face[faceIdx] clipped to the brush volume
// Result is in CCW winding order viewed from outside the brush
std::vector<glm::dvec3>
computeFacePolygon(const Brush& brush, std::size_t faceIdx) noexcept;

// Clip a convex polygon by a single plane — returns the part on the back side
std::vector<glm::dvec3>
clipPolygonByPlane(std::span<const glm::dvec3> poly, const Plane& plane) noexcept;
```

### CSG Operations

All CSG operates at the plane level. No mesh boolean operations — that's the whole point.

```
CSG Subtract (world - cutter):

  For each brush B in cutter:
    currentFragments = { world brush }
    For each face F of B:
      newFragments = {}
      For each fragment in currentFragments:
        [front, back] = splitBrushByPlane(fragment, F.plane)
        keep front (outside the cutter)
        if back exists: carry it to next face iteration
      currentFragments = newFragments
    result += currentFragments
```

```cpp
namespace forge::geo {

struct CSGResult {
    std::vector<Brush> fragments; // what remains after the operation
};

// Split a brush by a plane — returns [front_brush, back_brush]
// Either may be empty if brush is entirely on one side
std::pair<std::optional<Brush>, std::optional<Brush>>
splitBrushByPlane(const Brush& brush, const Plane& plane) noexcept;

// Subtract cutter brushes from subject brush
// Returns the resulting convex fragments (may be 1..N brushes)
CSGResult csgSubtract(const Brush& subject, std::span<const Brush> cutters) noexcept;

// Hollow: turn a solid brush into a shell of N face-brushes with given thickness
std::vector<Brush> hollowBrush(const Brush& brush, double wallThickness) noexcept;

} // namespace forge::geo
```

### Brush Validation

```cpp
struct ValidationResult {
    bool valid;
    std::vector<std::string> errors; // empty if valid
};

// Checks:
//   - At least 4 faces
//   - All face planes distinct and non-parallel
//   - At least 4 non-coplanar vertices computed
//   - All vertices inside all planes (convexity)
//   - No zero-area faces
//   - Volume > epsilon
ValidationResult validateBrush(const Brush& brush) noexcept;
```

### Primitive Constructors

```cpp
namespace forge::geo::primitives {

Brush makeBox(glm::dvec3 mins, glm::dvec3 maxs) noexcept;
Brush makePrism(glm::dvec3 base, double radius, double height, int sides) noexcept;
Brush makeWedge(glm::dvec3 mins, glm::dvec3 maxs) noexcept;
Brush makePyramid(glm::dvec3 base, glm::dvec3 apex, int sides) noexcept;
Brush makeSphere(glm::dvec3 center, double radius, int subdivisions) noexcept; // approx

} // namespace forge::geo::primitives
```

---

## Module 2: Mesh Builder — `forge::build`

Converts brush data into GPU-ready vertex buffers. The bridge between geometry and renderer.
This module *does* depend on the renderer (for buffer handle types) but not vice versa.

### Pipeline

```
Brush (planes)
  ├─ computeFacePolygons()      — Sutherland-Hodgman per face
  ├─ triangulateFaces()         — fan triangulation (convex → trivial)
  ├─ computePlanarUVs()         — project along dominant axis per face
  ├─ buildInterleavedBuffer()   — [pos:3f | normal:3f | uv:2f | matIdx:1u]
  └─ uploadToGPU()              — bgfx::createVertexBuffer / createIndexBuffer
```

### Incremental Rebuild

When one face of a brush changes, only that brush's buffers are rebuilt.
Each brush owns its own `bgfx::VertexBufferHandle` + `bgfx::IndexBufferHandle`.

```cpp
namespace forge::build {

struct BrushMesh {
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  ibh = BGFX_INVALID_HANDLE;
    uint32_t                 indexCount = 0;
    AABB                     bounds;
};

// Build or rebuild mesh for a single brush
BrushMesh buildBrushMesh(const geo::Brush& brush);

// Merge all brush meshes in a scene entity into one draw call (for static geo)
BrushMesh mergeBrushMeshes(std::span<const geo::Brush> brushes);

} // namespace forge::build
```

---

## Module 3: Scene Graph — `forge::scene`

Intentionally simple. No reflection. No archetype ECS megaframework.
Entities are IDs. Components are `std::variant`-dispatched structs.

### Entity Types

```cpp
namespace forge::scene {

struct Transform {
    glm::dvec3 translation = {0, 0, 0};
    glm::dquat rotation    = glm::identity<glm::dquat>();
    glm::dvec3 scale       = {1, 1, 1};

    [[nodiscard]] glm::dmat4 matrix() const noexcept;
};

// Solid world geometry — one or more brushes
struct BrushEntity {
    std::string              name;
    Transform                transform;
    std::vector<geo::Brush>  brushes;
    bool                     solid    = true;  // physics collider
    bool                     visible  = true;
    // Cached render data — rebuilt on brush change
    std::optional<build::BrushMesh> mesh;
};

// Point entity — lights, triggers, spawn points, game objects
struct PointEntity {
    std::string                          name;
    std::string                          classname; // "light", "player_start", etc.
    Transform                            transform;
    std::unordered_map<std::string, std::variant<float, int, bool, std::string>> properties;
};

// Authored mesh asset (imported GLTF, OBJ, etc.)
struct MeshEntity {
    std::string    name;
    Transform      transform;
    std::string    assetPath;
    // Loaded geometry lives in asset manager, referenced here
};

using Entity = std::variant<BrushEntity, PointEntity, MeshEntity>;
using EntityId = uint64_t;

struct Scene {
    std::string                              name;
    std::flat_map<EntityId, Entity>          entities;
    EntityId                                 nextId = 1;

    // Scene-wide settings
    glm::vec3   gravity        = {0.f, -9.81f, 0.f};
    glm::vec4   ambientLight   = {0.1f, 0.1f, 0.1f, 1.f};
    glm::vec3   fogColor       = {0.5f, 0.5f, 0.5f};
    float       fogDensity     = 0.f;

    EntityId addEntity(Entity e) noexcept;
    void     removeEntity(EntityId id) noexcept;
    Entity*  getEntity(EntityId id) noexcept;
};

} // namespace forge::scene
```

---

## Module 4: Renderer — `forge::gfx`

bgfx abstraction layer. Submits draw calls from scene data. One render pass for opaque geometry,
one for transparent, one for the editor overlay (grid, selection highlights, gizmos).

```
Frame:
  beginFrame()
  submitSkybox()
  for each visible BrushEntity:   submit mesh VBO/IBO + material uniforms
  for each visible MeshEntity:    submit asset geometry
  submitEditorOverlay()           grid, AABB highlights, face normals (debug)
  submitImGui()
  endFrame() → bgfx::frame()
```

Materials are simple PBR:
- Albedo texture (or flat color)
- Normal map (optional)
- Roughness / Metallic (scalar or map)
- Emissive (optional)

---

## Module 5: Physics — `forge::phys`

Jolt Physics wrapper. Brush geometry maps exactly to Jolt's `ConvexHullShapeSettings`.

```cpp
namespace forge::phys {

// Build a Jolt ConvexHullShape from a brush's computed vertices
JPH::Ref<JPH::Shape> shapeFromBrush(const geo::Brush& brush);

// Build a compound shape from multiple brushes (for a BrushEntity)
JPH::Ref<JPH::Shape> compoundShapeFromBrushes(std::span<const geo::Brush> brushes);

// PhysicsWorld wraps JPH::PhysicsSystem
struct PhysicsWorld {
    void step(float dt) noexcept;
    void addStaticBody(EntityId id, JPH::Ref<JPH::Shape> shape, glm::mat4 transform);
    void addDynamicBody(EntityId id, JPH::Ref<JPH::Shape> shape, glm::mat4 transform, float mass);
    void removeBody(EntityId id);

    // Raycast — used by editor selection and game logic
    struct RayHit { EntityId entity; glm::vec3 point; glm::vec3 normal; float t; };
    std::optional<RayHit> raycast(glm::vec3 origin, glm::vec3 dir, float maxDist) noexcept;
};

} // namespace forge::phys
```

---

## Module 6: Editor — `forge::editor`

Dear ImGui application running on top of the engine. The editor state is a plain struct.
All editing operations go through a **Command** interface for undo/redo.

### Tool System

```cpp
namespace forge::editor {

struct ToolContext {
    scene::Scene&    scene;
    phys::PhysicsWorld& physics;
    gfx::Renderer&   renderer;
    Selection&       selection;
    CommandStack&    commands;
    Camera&          camera;
};

struct Tool {
    virtual ~Tool() = default;
    virtual void onMouseDown (ToolContext&, MouseEvent)  {}
    virtual void onMouseMove (ToolContext&, MouseEvent)  {}
    virtual void onMouseUp   (ToolContext&, MouseEvent)  {}
    virtual void onKeyDown   (ToolContext&, KeyEvent)    {}
    virtual void onDraw      (ToolContext&)              {} // ImGuizmo overlays
    virtual const char* name() const noexcept = 0;
};

} // namespace forge::editor
```

### Tools

| Tool | Shortcut | Description |
|---|---|---|
| Select | `Q` | Click/box select entities and faces |
| Move | `W` | Translate via ImGuizmo gizmo |
| Rotate | `E` | Rotate via ImGuizmo gizmo |
| Scale | `R` | Scale brush by moving faces along normals |
| Face | `F` | Select individual faces, adjust plane distance |
| Vertex | `V` | Drag vertices (recomputes surrounding planes) |
| Clip | `C` | Cut a brush with an arbitrary plane |
| CSG Subtract | `X` | Carve selection into other brushes |
| Hollow | `H` | Shell a solid brush with given wall thickness |
| Paint | `P` | Apply materials to faces |

### Command Stack (Undo/Redo)

```cpp
struct Command {
    virtual ~Command() = default;
    virtual void execute(scene::Scene&) = 0;
    virtual void undo(scene::Scene&)    = 0;
    virtual std::string describe() const = 0;
};

// Example
struct MoveFaceCommand : Command {
    scene::EntityId entityId;
    std::size_t     brushIdx;
    std::size_t     faceIdx;
    double          oldDistance;
    double          newDistance;

    void execute(scene::Scene& s) override { setFaceDistance(s, newDistance); }
    void undo   (scene::Scene& s) override { setFaceDistance(s, oldDistance); }
};
```

---

## Module 7: Export System — `forge::export_`

All exporters work from computed mesh data (vertices/polygons), not raw planes.
They run on a snapshot of the scene — non-destructive, callable at any time.

### Exporters

| Format | Use Case |
|---|---|
| **GLTF 2.0 / GLB** | Godot, Unity, Unreal, Blender — primary handoff format |
| **OBJ + MTL** | Maximum legacy compatibility |
| **Quake MAP** | TrenchBroom interchange, BSP compilers (qbsp) |
| **STL** | 3D printing |
| **Forge JSON** | Native scene serialization (save/load) |

### GLTF Pipeline

```
Scene snapshot
  BrushEntities  → merge brush meshes per material → GLTF Mesh nodes
  MeshEntities   → passthrough asset geometry → GLTF Mesh nodes
  PointEntities  → GLTF Empty nodes + extras{classname, properties}
  Materials      → GLTF PBR material descriptors
  → tinygltf serializer → .glb binary output
```

### Quake MAP Export

Writes the native plane-based format directly from `Brush::faces` —
no mesh conversion needed. Round-trips perfectly.

```
// brush
{
( 64 0 0 ) ( 64 64 0 ) ( 64 0 64 ) textureName 0 0 0 1 1
( 0 0 0 ) ( 0 0 64 ) ( 0 64 0 ) textureName 0 0 0 1 1
...
}
```

---

## Repository Structure

```
forge/
├── CMakeLists.txt
├── vcpkg.json                     # manifest: glm, cgal, sdl3, catch2, nlohmann-json
├── cmake/
│   └── modules/                   # FindBGFX.cmake, FindJolt.cmake, etc.
│
├── engine/
│   ├── geometry/                  # forge::geo — no renderer dep
│   │   ├── include/forge/geo/
│   │   │   ├── Plane.hpp
│   │   │   ├── Brush.hpp
│   │   │   ├── CSG.hpp
│   │   │   └── Primitives.hpp
│   │   └── src/
│   │       ├── Brush.cpp
│   │       ├── CSG.cpp
│   │       └── Primitives.cpp
│   │
│   ├── scene/                     # forge::scene
│   ├── build/                     # forge::build — brush→GPU
│   ├── gfx/                       # forge::gfx — bgfx wrapper
│   ├── physics/                   # forge::phys — Jolt wrapper
│   └── export/                    # forge::export_
│
├── editor/                        # forge::editor — ImGui app
│   ├── include/forge/editor/
│   └── src/
│       ├── main.cpp
│       ├── EditorApp.cpp
│       ├── Viewport.cpp
│       ├── tools/
│       │   ├── SelectTool.cpp
│       │   ├── FaceTool.cpp
│       │   └── CSGTool.cpp
│       └── panels/
│           ├── SceneTreePanel.cpp
│           └── PropertiesPanel.cpp
│
├── runtime/                       # standalone game runner (no editor)
│   └── src/main.cpp
│
├── tests/
│   ├── geo/
│   │   ├── test_plane.cpp
│   │   ├── test_brush_vertices.cpp
│   │   ├── test_csg_subtract.cpp
│   │   └── test_primitives.cpp
│   └── CMakeLists.txt
│
└── assets/
    ├── shaders/                   # bgfx shader source (.sc files)
    └── textures/
```

---

## Build Instructions (Linux)

```bash
# Prerequisites
sudo apt install cmake ninja-build libsdl3-dev

# Clone with submodules (bgfx, imgui, Jolt as submodules or via vcpkg)
git clone --recurse-submodules https://github.com/The-No-Hands-company/forge
cd forge

# Configure
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --parallel

# Run editor
./build/editor/forge-editor

# Run tests
ctest --test-dir build --output-on-failure
```

---

## Data Flow: Edit → Play

```
User drags a face in viewport
  → FaceTool captures mouse delta
  → Casts ray through viewport → hits face
  → MoveFaceCommand{entity, brush, face, oldDist, newDist}
     → command.execute(scene)
        → brush.faces[i].plane.distance += delta
        → brush.invalidateCache()
        → build::buildBrushMesh(brush)         ← GPU buffer rebuild
        → phys::world.updateCollider(entityId) ← Jolt shape rebuild
  → Frame renders updated geometry

User presses Play (Ctrl+P)
  → Editor freezes tool input
  → All static BrushEntities → Jolt static bodies (already built)
  → Dynamic PointEntities → Jolt dynamic bodies
  → Player spawns at 'player_start' transform
  → Game loop runs: Input → Scripts → Physics → Transform sync → Render
  → Ctrl+P again → restore editor state, scene unchanged
```

---

## Phase 0 Deliverable — Geometry Kernel

**Goal:** A standalone C++ library with zero renderer dependency, fully tested.

**Deliverables:**
- [ ] `Vec3`, `Plane`, `AABB` types + GLM integration
- [ ] `Brush` struct with face list
- [ ] `computeBrushVertices()` — 3-plane intersection, inside-all-planes filter
- [ ] `computeFacePolygon()` — Sutherland-Hodgman clip per face
- [ ] `validateBrush()` — convex + closed + non-degenerate checks
- [ ] `splitBrushByPlane()` — the fundamental CSG primitive
- [ ] `csgSubtract()` — built on top of split
- [ ] `hollowBrush()` — convenience CSG operation
- [ ] `primitives::makeBox/Wedge/Prism/Pyramid()`
- [ ] Catch2 test suite covering all of the above

**Not in Phase 0:** renderer, physics, editor, UI — nothing.
Pure geometry math. Verifiable with unit tests alone.

---

*FORGE — C++23 Native Architecture Document — End*
