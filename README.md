# FORGE

A brush geometry kernel and game engine for native desktop.

Plane-based convex solid geometry (Quake/TrenchBroom model) as the foundation
for a unified modelling and game development environment.

## Phase 0 — Geometry Kernel

The current milestone is a standalone, renderer-free geometry library:

- **Plane** — half-space definition, signed distance, classification
- **Brush** — convex solid from plane intersections, face polygon computation
- **CSG** — split, subtract, hollow *(Phase 0.5)*
- **Primitives** — box, wedge, prism, pyramid

## Building

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt install cmake ninja-build git curl zip unzip tar pkg-config

# vcpkg (if not already installed)
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
```

### Configure & Build

```bash
git clone https://github.com/The-No-Hands-company/forge
cd forge

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build build --parallel
```

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```

## Architecture

See [`docs/FORGE_ARCHITECTURE_CPP23.md`](docs/FORGE_ARCHITECTURE_CPP23.md) for the full design document.

## Project Structure

```
forge/
├── engine/
│   └── geometry/          # forge::geo — brush kernel (no renderer dep)
│       ├── include/
│       └── src/
├── tests/
│   └── geo/               # Catch2 tests
└── CMakeLists.txt
```

## License

TBD
