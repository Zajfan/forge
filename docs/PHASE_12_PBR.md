# Phase 12: Advanced Materials & PBR (Physically Based Rendering)

## Overview

Phase 12 implements a complete **Physically Based Rendering (PBR)** material system with support for metallic-roughness workflows, normal maps, ambient occlusion, and emissive textures. This is a major visual quality upgrade from the previous Blinn-Phong shader.

## Key Components

### 1. Material System (`Material.hpp / Material.cpp`)

A comprehensive PBR material definition with:
- **Color Properties**: Albedo, emissive
- **PBR Parameters**: 
  - Metallic (0 = dielectric, 1 = metal)
  - Roughness (0 = mirror, 1 = rough)
  - Ambient Occlusion (per-pixel)
- **Texture IDs**: Albedo, normal, roughness, metallic, AO, emissive
- **Texture Intensity Controls**: Normal scale, emissive scale
- **Alpha Blending**: Optional alpha transparency with cutoff threshold
- **JSON Serialization**: Full round-trip support for saving/loading

### 2. MaterialLibrary

A simple registry for managing named materials:
```cpp
auto lib = MaterialLibrary();
auto mat = lib.getOrCreateMaterial("brick/wall");
mat->metallic = 0.0f;
mat->roughness = 0.8f;
```

### 3. PBR Shader (`Shaders.hpp`)

Complete **Cook-Torrance BRDF** implementation in GLSL:
- **Fresnel-Schlick** approximation for angle-dependent reflectance
- **GGX/Trowbridge-Reitz** normal distribution function
- **Schlick-Beckmann** geometry shadowing
- **Normal mapping** with tangent-space perturbation
- **Metallic/Roughness** texture support
- **Ambient Occlusion** integration
- **Emissive mapping** for glow effects
- **Point light PBR** evaluation (up to 8 lights)
- **Directional light** with shadows
- **Exponential fog** and Reinhard tonemapping

### 4. Renderer Updates (`Renderer.hpp / Renderer.cpp`)

- **Dual shader system**: PBR shader (default) + standard Blinn-Phong fallback
- **Material binding**: Automatic texture cache lookup and GL binding
- **Interleaved texture units**: 6 texture units per draw call for full PBR
- **Dynamic shader switching**: `setPBREnabled(bool)` toggles between rendering modes
- **Material-driven rendering**: `DrawCall` now carries a `std::shared_ptr<Material>` instead of albedo + textureId

### 5. TextureCache Integration

Materials reference textures by ID (e.g., `"brick/wall"` → `textures/brick/wall.png`). The TextureCache automatically:
- Resolves IDs to file paths
- Caches loaded textures
- Provides fallback checkerboard for missing textures
- Supports transparent binding in the renderer

### 6. Material Tests (`test_material.cpp`)

Complete test coverage:
- Material initialization and defaults
- JSON serialization round-trip
- MaterialLibrary management
- Property persistence

## Design Decisions

### Cook-Torrance BRDF

The Cook-Torrance model is the industry standard for PBR because it produces physically plausible results across all viewing angles and roughness values. The implementation includes:

1. **Fresnel Effect**: Metals have constant reflectance; dielectrics reflect more at grazing angles (Schlick approximation)
2. **Microfacet Distribution**: GGX produces realistic roughness falloff (sharper highlights for polished surfaces)
3. **Geometry Shadowing**: Prevents over-bright highlights by accounting for self-shadowing of microfacets

### Normal Mapping

Normal perturbation uses tangent-space normals with TBN matrix reconstruction:
- Vertex provides base normal + implicit tangent
- Normal map sampled and decoded from [0,1] → [-1,1]
- `normalScale` parameter tunes intensity
- Branchless path (no special cases for missing maps)

### Metallic/Roughness Workflow

F0 (Fresnel base reflectance) is derived from metalness:
- **Dielectric** (metalness=0): F0 = 0.04 (4% reflectance, typical for plastic)
- **Metal** (metalness=1): F0 = albedo (highly reflective)
- Linear interpolation smooths the transition

Roughness modulates both:
- Specular intensity (rougher = dimmer highlights)
- Specular spread (rougher = wider highlights)

### Texture Binding Strategy

Each material can have up to 6 textures bound to texture units 0-5 at draw time:
- Albedo (sRGB, gamma-corrected)
- Normal (tangent-space)
- Metallic (grayscale, packed)
- Roughness (grayscale, packed)
- AO (grayscale, multiplied)
- Emissive (sRGB)

Missing textures are handled gracefully:
- Albedo missing → uniform color (0.7, 0.7, 0.7)
- Normal missing → perturb to base normal only
- Metallic/Roughness missing → use uniform value

### JSON Format

Materials serialize as a flat key-value map for simplicity:
```json
{
  "name": "Steel",
  "albedoColor.r": "0.8",
  "albedoColor.g": "0.8",
  "metallic": "1.0",
  "roughness": "0.2"
}
```

This integrates seamlessly with the existing scene serialization (Phase 6).

## Usage Example

```cpp
// Create a material
auto steel = std::make_shared<forge::gfx::Material>("Steel");
steel->metallic = 1.0f;
steel->roughness = 0.2f;
steel->albedoColor = {0.5f, 0.5f, 0.6f};

// Reference textures
steel->normalTextureId = "metal/steel_normal";
steel->roughnessTextureId = "metal/steel_roughness";

// Submit for rendering
forge::gfx::DrawCall dc;
dc.mesh = gpuMesh;
dc.modelMatrix = transform;
dc.material = steel;

renderer.submit(dc);
```

## Integration Points

- **Scene Serialization (Phase 6)**: Materials should be extended to load/save with scenes
- **Editor (Phase 3+)**: Material browser and properties panel should display PBR parameters
- **Export (Phase 1)**: GLTF export should pack metallic/roughness into a single texture for compatibility
- **Asset Import (Phase 10)**: Mesh import should detect and load associated PBR textures

## Performance Characteristics

- **Shader Complexity**: Cook-Torrance adds ~2x fragment shader cost vs Blinn-Phong
- **Texture Bandwidth**: 6 textures × 32-bit = 192 bits/pixel max (typical: 2-3 textures in use)
- **Memory**: Material struct = ~128 bytes; negligible overhead
- **Fallback Path**: Standard Blinn-Phong shader available for older hardware (glProgramPipeline could select at runtime)

## Next Steps

1. **Material Editor Panel (Phase 13)**: ImGui UI for editing PBR parameters and assigning textures
2. **Material Import**: Support importing FBX/GLTF materials with automatic PBR texture detection
3. **Texture Baking**: Tools to bake normal maps and AO from high-poly models
4. **Shader Variants**: Anisotropic reflections, clearcoat (car paint), fabric (cloth BRDF)
5. **Post-Processing**: Environment mapping (IBL) for realistic ambient reflections
