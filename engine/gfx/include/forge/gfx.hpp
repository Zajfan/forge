#pragma once

// ─── forge::gfx — renderer umbrella header ───────────────────────────────────
//
//   #include <forge/gfx.hpp>
//
//   forge::gfx::Window, WindowConfig, InputState, MouseState, KeyState
//   forge::gfx::Shader
//   forge::gfx::GPUMesh, GPUEntityMesh
//   forge::gfx::OrbitCamera
//   forge::gfx::Renderer, RenderFrame, DrawCall
//   forge::gfx::Framebuffer
//   forge::gfx::GridRenderer

#include "forge/gfx/Window.hpp"
#include "forge/gfx/Shader.hpp"
#include "forge/gfx/GPUMesh.hpp"
#include "forge/gfx/Camera.hpp"
#include "forge/gfx/Renderer.hpp"
#include "forge/gfx/Framebuffer.hpp"
#include "forge/gfx/GridRenderer.hpp"
#include "forge/gfx/TextureCache.hpp"
#include "forge/gfx/SkyboxRenderer.hpp"
#include "forge/gfx/OrthoCamera.hpp"
#include "forge/gfx/ShadowMap.hpp"
#include "forge/gfx/MeshAssetCache.hpp"
#include "forge/gfx/BloomRenderer.hpp"
