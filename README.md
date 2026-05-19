# FORGE

A brush geometry kernel and game engine for native desktop.

Plane-based convex solid geometry (Quake/TrenchBroom model) as the foundation for a unified modelling and game development environment.

## Current State

| Phase | Module | Status |
|-------|--------|--------|
| 0     | `forge::geometry` — brush kernel, CSG, primitives | ✅ Complete |
| 0.5   | CSG — `splitBrushByPlane`, `csgSubtract`, `hollowBrush` | ✅ Complete |
| 1     | `forge::scene` — scene graph, entities, transforms | ✅ Complete |
| 1     | `forge::export_` — OBJ + Quake MAP exporters | ✅ Complete |
| 1     | `forge-cli` — CLI demo / export tool | ✅ Complete |
| 2     | `forge::build` — brush → CPU mesh (MeshData) | ✅ Complete |
| 2     | `forge::gfx` — SDL3 + OpenGL 4.6 + Camera + Renderer | ✅ Complete |
| 2     | `forge-viewer` — 3D scene viewer with ImGui overlay | ✅ Complete |

## Building

### Prerequisites

```bash
# Ubuntu 24.04
sudo apt install cmake ninja-build git curl zip unzip tar \
                 pkg-config libgl1-mesa-dev

# vcpkg
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
```

### Configure & Build

```bash
git clone https://github.com/Zajfan/forge
cd forge

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build build --parallel
```

vcpkg will automatically install: `glm`, `catch2`, `nlohmann-json`, `sdl3`, `glew`, `imgui[sdl3-binding,opengl3-binding]`

### Run the viewer

```bash
./build/bin/forge-viewer
```

Mouse controls: **Left-drag** orbit · **Right-drag** pan · **Scroll** zoom · **F1** wireframe · **ESC** quit

### Run the CLI exporter

```bash
./build/bin/forge-cli ./output
# Produces output/forge_test_room.obj and output/forge_test_room.map
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## Architecture

See [`docs/FORGE_ARCHITECTURE_CPP23.md`](docs/FORGE_ARCHITECTURE_CPP23.md).
Editor usage guide: [`docs/EDITOR_TOOL_FIELD_GUIDE.md`](docs/EDITOR_TOOL_FIELD_GUIDE.md).

```
forge/
├── engine/
│   ├── geometry/   forge::geo    — brush kernel, CSG, primitives
│   ├── scene/      forge::scene  — entities, transforms, scene graph
│   ├── export/     forge::export_ — OBJ, Quake MAP exporters
│   ├── build/      forge::build  — brush → CPU MeshData pipeline
│   └── gfx/        forge::gfx   — SDL3 window, OpenGL renderer, camera
├── apps/
│   ├── forge-cli/                — CLI export tool
│   └── forge-viewer/             — 3D viewer
├── tests/
└── docs/
```

## License

TBD
