#pragma once

#include "Shader.hpp"
#include "GPUMesh.hpp"
#include "Camera.hpp"
#include <forge/scene.hpp>
#include <glm/mat4x4.hpp>

namespace forge::gfx {

// ─── RenderFrame ─────────────────────────────────────────────────────────────

/// All per-frame global data needed for rendering.
/// Filled by the viewer from Camera + Scene lighting settings.
struct RenderFrame {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;

    glm::vec3 sunDirection  = { -0.5f, -1.f, -0.5f };  ///< world space, toward light
    glm::vec3 sunColor      = {  1.f,  0.95f, 0.8f };
    float     sunIntensity  = 1.2f;
    glm::vec3 ambientColor  = { 0.06f, 0.06f, 0.08f };

    bool wireframe = false;
};

// ─── DrawCall ────────────────────────────────────────────────────────────────

/// One draw call: a mesh with a model transform and flat albedo colour.
struct DrawCall {
    const GPUMesh* mesh        = nullptr;
    glm::mat4      modelMatrix = glm::mat4(1.f);
    glm::vec3      albedo      = { 0.7f, 0.7f, 0.72f }; ///< fallback flat colour
};

// ─── Renderer ────────────────────────────────────────────────────────────────

/// Simple single-pass forward renderer.
///
/// Usage:
///   renderer.init();
///   // each frame:
///   renderer.beginFrame(frame);
///   for (auto& dc : drawCalls) renderer.submit(dc);
///   renderer.endFrame();
///   // at shutdown:
///   renderer.shutdown();
class Renderer {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Compile shaders and set up GL state.
    /// Must be called after a GL context exists.
    [[nodiscard]] bool init() noexcept;

    /// Free GPU resources.
    void shutdown() noexcept;

    // ── Per-frame ─────────────────────────────────────────────────────────────

    /// Clear colour/depth, bind shader, upload frame globals.
    void beginFrame(const RenderFrame& frame) noexcept;

    /// Queue a draw call.
    void submit(const DrawCall& dc) noexcept;

    /// Flush all submitted draw calls and reset state.
    void endFrame() noexcept;

    // ── Stats ─────────────────────────────────────────────────────────────────

    [[nodiscard]] uint32_t drawCallCount()  const noexcept { return drawCallCount_; }
    [[nodiscard]] uint32_t triangleCount()  const noexcept { return triangleCount_; }

private:
    Shader   shader_;
    RenderFrame frame_;

    uint32_t drawCallCount_ = 0;
    uint32_t triangleCount_ = 0;

    bool initialised_ = false;
};

} // namespace forge::gfx
