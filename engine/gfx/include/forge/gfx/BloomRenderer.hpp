#pragma once

#include "Shader.hpp"
#include "Framebuffer.hpp"
#include <cstdint>

namespace forge::gfx {

// ─── BloomRenderer ────────────────────────────────────────────────────────────

/// Screen-space bloom post-process.
///
/// Pipeline (called after the main scene pass):
///   1. Bright-pass  — extract pixels above `threshold` into a half-res FBO.
///   2. Blur (H + V) — separable 9-tap Gaussian, iterated `passes` times.
///   3. Composite    — additively blend the blurred result onto the scene FBO.
///
/// The scene Framebuffer is modified in-place by the composite step.
/// The caller should display the scene FBO as normal after bloom.
///
/// All three FBOs are internal and resized automatically to match the scene.
class BloomRenderer {
public:
    // ── Tuning ────────────────────────────────────────────────────────────────

    float threshold  = 0.80f;  ///< Luminance threshold for the bright-pass
    float intensity  = 1.00f;  ///< Bloom additive intensity multiplier
    int   passes     = 3;      ///< Blur iterations (more = softer, wider bloom)

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool init() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] bool valid() const noexcept { return brightPass_.valid(); }

    // ── Apply ─────────────────────────────────────────────────────────────────

    /// Apply bloom to the scene and return the composited result texture ID.
    ///
    /// Pipeline: bright-pass (half-res) → Gaussian blur × passes → composite (full-res)
    /// The returned texture is valid until the next call to apply().
    ///
    /// @param sceneFbo  Source scene (not modified).
    /// @param width/height  Full scene resolution.
    /// @returns GL texture ID of composited result (full-res RGBA8).
    [[nodiscard]] uint32_t apply(const Framebuffer& sceneFbo,
                                  int width, int height) noexcept;

private:
    Shader      brightPass_;    ///< Bright-pass shader
    Shader      blurShader_;    ///< Separable Gaussian blur
    Shader      composite_;     ///< Additive blend

    Framebuffer brightFbo_;     ///< Half-res bright-pass result
    Framebuffer pingFbo_;       ///< Ping-pong blur buffer A (half-res)
    Framebuffer pongFbo_;       ///< Ping-pong blur buffer B (half-res)
    Framebuffer resultFbo_;     ///< Full-res composited output

    uint32_t emptyVao_ = 0;     ///< Empty VAO for fullscreen triangle

    void ensureFbos(int w, int h) noexcept;
    void runBrightPass(uint32_t sceneTex) noexcept;
    void runBlur() noexcept;
    void runComposite(uint32_t sceneTex) noexcept;
};

} // namespace forge::gfx
