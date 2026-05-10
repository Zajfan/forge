#pragma once

#include "Shader.hpp"
#include "GPUMesh.hpp"
#include "Camera.hpp"
#include <forge/scene.hpp>
#include <glm/mat4x4.hpp>
#include <vector>

namespace forge::gfx {

// ─── PointLight ──────────────────────────────────────────────────────────────

/// A dynamic point light submitted per-frame.
/// The renderer supports up to kMaxPointLights lights per frame.
struct PointLight {
    glm::vec3 position  = {};
    glm::vec3 color     = { 1.f, 1.f, 1.f };
    float     intensity = 300.f;    ///< Brightness (game units²)
    float     radius    = 512.f;    ///< Hard cutoff radius (game units)
};

inline constexpr int kMaxPointLights = 8;

// ─── RenderFrame ─────────────────────────────────────────────────────────────

struct RenderFrame {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;

    // Directional (sun) light
    glm::vec3 sunDirection = { -0.5f, -1.f, -0.5f };
    glm::vec3 sunColor     = {  1.f,  0.95f, 0.8f  };
    float     sunIntensity = 1.2f;
    glm::vec3 ambientColor = { 0.06f, 0.06f, 0.08f };

    // Dynamic point lights (up to kMaxPointLights)
    std::vector<PointLight> pointLights;

    bool wireframe = false;
};

// ─── DrawCall ────────────────────────────────────────────────────────────────

struct DrawCall {
    const GPUMesh* mesh        = nullptr;
    glm::mat4      modelMatrix = glm::mat4(1.f);
    glm::vec3      albedo      = { 0.7f, 0.7f, 0.72f };
};

// ─── Renderer ────────────────────────────────────────────────────────────────

class Renderer {
public:
    [[nodiscard]] bool init()     noexcept;
    void               shutdown() noexcept;

    void beginFrame(const RenderFrame& frame) noexcept;
    void submit    (const DrawCall&    dc)    noexcept;
    void endFrame  ()                         noexcept;

    [[nodiscard]] uint32_t drawCallCount() const noexcept { return drawCallCount_; }
    [[nodiscard]] uint32_t triangleCount() const noexcept { return triangleCount_; }

private:
    Shader      shader_;
    RenderFrame frame_;

    uint32_t drawCallCount_ = 0;
    uint32_t triangleCount_ = 0;
    bool     initialised_   = false;
};

} // namespace forge::gfx
