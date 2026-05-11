#pragma once

#include "Shader.hpp"
#include <forge/geo/Math.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <cstdint>

namespace forge::gfx {

// ─── ShadowMap ────────────────────────────────────────────────────────────────

/// A single-cascade directional shadow map.
///
/// Two-pass rendering:
///   1. shadowPass: render scene depth into a depth-only FBO from the sun's POV.
///   2. Main pass: bind the depth texture + light-space matrix; fragment shader
///      samples it with PCF for smooth soft shadows.
///
/// Resolution: 2048 × 2048 by default (configurable at init time).
/// Filtering:  3×3 PCF kernel using GL_COMPARE_REF_TO_TEXTURE sampler.
/// Bias:       NdotL-dependent bias in the fragment shader (no depth-bias offset
///             at the driver level to keep things portable).
class ShadowMap {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Create the depth FBO and compile the depth-only shader.
    /// @param resolution  Width = Height of the shadow map texture.
    [[nodiscard]] bool init(int resolution = 2048) noexcept;

    void shutdown() noexcept;

    [[nodiscard]] bool valid() const noexcept { return depthFbo_ != 0; }

    // ── Light space matrix ─────────────────────────────────────────────────────

    /// Compute a light-space VP matrix that encloses the whole scene.
    /// Call once per frame (or when sun direction / scene changes).
    ///
    /// @param sunDir      Normalised direction FROM surface TOWARD sun.
    /// @param sceneBounds AABB of all scene geometry.
    [[nodiscard]] glm::mat4 computeLightVP(
        glm::vec3        sunDir,
        const geo::AABB& sceneBounds) const noexcept;

    void setLightVP(const glm::mat4& m) noexcept { lightVP_ = m; }

    [[nodiscard]] const glm::mat4& lightVP() const noexcept { return lightVP_; }

    // ── Shadow pass ───────────────────────────────────────────────────────────

    /// Bind depth FBO, set viewport, clear depth.
    void beginPass() const noexcept;

    /// Submit a mesh into the depth-only pass.
    /// Only requires the model matrix — shader uses u_lightMVP = lightVP * model.
    void submitDepth(uint32_t vao, uint32_t indexCount,
                     const glm::mat4& model) const noexcept;

    /// Restore default framebuffer.
    void endPass() const noexcept;

    // ── Bind for main pass ────────────────────────────────────────────────────

    /// Bind depth texture to `textureUnit`.  Activate BEFORE renderer.beginFrame().
    void bindDepthTexture(int textureUnit = 1) const noexcept;

    [[nodiscard]] uint32_t depthTexture() const noexcept { return depthTex_; }
    [[nodiscard]] int      resolution()   const noexcept { return res_; }

    // ── Depth shader access ───────────────────────────────────────────────────

    [[nodiscard]] const Shader& depthShader() const noexcept { return depthShader_; }

private:
    int      res_      = 2048;
    uint32_t depthFbo_ = 0;
    uint32_t depthTex_ = 0;

    Shader   depthShader_;
    glm::mat4 lightVP_ = glm::mat4(1.f);
};

} // namespace forge::gfx
