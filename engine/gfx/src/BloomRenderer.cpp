#include "forge/gfx/BloomRenderer.hpp"
#include "Shaders.hpp"

#include <GL/glew.h>
#include <glm/vec2.hpp>
#include <algorithm>
#include <iostream>

namespace forge::gfx {

// ─── Init / shutdown ─────────────────────────────────────────────────────────

bool BloomRenderer::init() noexcept {
    auto bp = Shader::compile(shaders::kFullscreenVert, shaders::kBrightPassFrag);
    if (!bp) { std::cerr << "[Bloom] Bright-pass: " << bp.error() << '\n'; return false; }
    brightPass_ = std::move(*bp);

    auto bl = Shader::compile(shaders::kFullscreenVert, shaders::kGaussianBlurFrag);
    if (!bl) { std::cerr << "[Bloom] Blur: " << bl.error() << '\n'; return false; }
    blurShader_ = std::move(*bl);

    auto co = Shader::compile(shaders::kFullscreenVert, shaders::kBloomCompositeFrag);
    if (!co) { std::cerr << "[Bloom] Composite: " << co.error() << '\n'; return false; }
    composite_ = std::move(*co);

    glGenVertexArrays(1, &emptyVao_);
    return true;
}

void BloomRenderer::shutdown() noexcept {
    if (emptyVao_) { glDeleteVertexArrays(1, &emptyVao_); emptyVao_ = 0; }
    brightFbo_.destroy();
    pingFbo_.destroy();
    pongFbo_.destroy();
    resultFbo_.destroy();
    brightPass_ = {}; blurShader_ = {}; composite_ = {};
}

// ─── FBO management ───────────────────────────────────────────────────────────

void BloomRenderer::ensureFbos(int w, int h) noexcept {
    // Blur buffers run at half resolution (performance + softer look)
    const int hw = std::max(1, w / 2);
    const int hh = std::max(1, h / 2);
    brightFbo_.resize(hw, hh);
    pingFbo_  .resize(hw, hh);
    pongFbo_  .resize(hw, hh);
    // Result is full resolution
    resultFbo_.resize(w, h);
}

static void fullscreenTri(uint32_t vao) noexcept {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// ─── Bright-pass ─────────────────────────────────────────────────────────────

void BloomRenderer::runBrightPass(uint32_t sceneTex) noexcept {
    brightFbo_.bind();
    glDisable(GL_DEPTH_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    brightPass_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    brightPass_.setInt  ("u_scene",     0);
    brightPass_.setFloat("u_threshold", threshold);
    fullscreenTri(emptyVao_);
    brightPass_.unbind();
    brightFbo_.unbind();
}

// ─── Gaussian blur (ping-pong at half-res) ────────────────────────────────────

void BloomRenderer::runBlur() noexcept {
    uint32_t srcTex = brightFbo_.colorTexture;

    blurShader_.bind();
    for (int i = 0; i < passes; ++i) {
        // Horizontal: src → ping
        pingFbo_.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        blurShader_.setInt ("u_tex", 0);
        blurShader_.setVec2("u_dir", glm::vec2(1.f, 0.f));
        fullscreenTri(emptyVao_);
        pingFbo_.unbind();

        // Vertical: ping → pong
        pongFbo_.bind();
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pingFbo_.colorTexture);
        blurShader_.setInt ("u_tex", 0);
        blurShader_.setVec2("u_dir", glm::vec2(0.f, 1.f));
        fullscreenTri(emptyVao_);
        pongFbo_.unbind();

        srcTex = pongFbo_.colorTexture;
    }
    blurShader_.unbind();
}

// ─── Composite (full-res, scene + upscaled bloom) ────────────────────────────

void BloomRenderer::runComposite(uint32_t sceneTex, uint32_t bloomTex) noexcept {
    // Draw into resultFbo_ (full-res); GL bilinearly upscales bloomTex
    resultFbo_.bind();
    glClear(GL_COLOR_BUFFER_BIT);

    composite_.bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    composite_.setInt("u_scene", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex);        // half-res → upscaled automatically
    composite_.setInt  ("u_bloom",     1);
    composite_.setFloat("u_intensity", intensity);

    fullscreenTri(emptyVao_);
    composite_.unbind();
    resultFbo_.unbind();
}

// ─── apply ────────────────────────────────────────────────────────────────────

uint32_t BloomRenderer::apply(const Framebuffer& sceneFbo,
                               int width, int height) noexcept {
    if (!valid()) return sceneFbo.colorTexture; // pass-through

    glDisable(GL_DEPTH_TEST);
    ensureFbos(width, height);

    runBrightPass(sceneFbo.colorTexture);
    runBlur();
    runComposite(sceneFbo.colorTexture, pongFbo_.colorTexture);

    glEnable(GL_DEPTH_TEST);
    return resultFbo_.colorTexture; // caller displays this
}

} // namespace forge::gfx
