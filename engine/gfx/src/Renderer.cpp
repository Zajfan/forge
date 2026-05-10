#include "forge/gfx/Renderer.hpp"
#include "Shaders.hpp"

#include <GL/glew.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <format>
#include <algorithm>
#include <iostream>

namespace forge::gfx {

// ─── Init / shutdown ──────────────────────────────────────────────────────────

bool Renderer::init() noexcept {
    auto result = Shader::compile(shaders::kBrushVert, shaders::kBrushFrag);
    if (!result) {
        std::cerr << "[forge::gfx] Brush shader compile error:\n" << result.error() << '\n';
        return false;
    }
    shader_       = std::move(*result);
    initialised_  = true;

    // Fixed GL state
    glClearColor(0.08f, 0.08f, 0.10f, 1.f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    return true;
}

void Renderer::shutdown() noexcept {
    shader_      = {};
    initialised_ = false;
}

// ─── Per-frame ────────────────────────────────────────────────────────────────

void Renderer::beginFrame(const RenderFrame& frame) noexcept {
    frame_ = frame;
    drawCallCount_ = 0;
    triangleCount_ = 0;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Wireframe mode
    glPolygonMode(GL_FRONT_AND_BACK, frame.wireframe ? GL_LINE : GL_FILL);

    shader_.bind();

    // Upload frame-global uniforms (lights, camera)
    shader_.setVec3 ("u_sunDirection",  glm::normalize(frame.sunDirection));
    shader_.setVec3 ("u_sunColor",      frame.sunColor);
    shader_.setFloat("u_sunIntensity",  frame.sunIntensity);
    shader_.setVec3 ("u_ambientColor",  frame.ambientColor);
    shader_.setVec3 ("u_cameraPos",     frame.cameraPos);
    shader_.setBool ("u_wireframe",     frame.wireframe);

    // Point lights
    const int nLights = static_cast<int>(
        std::min(frame.pointLights.size(), static_cast<std::size_t>(kMaxPointLights)));
    shader_.setInt("u_numPointLights", nLights);
    for (int i = 0; i < nLights; ++i) {
        const auto& pl = frame.pointLights[i];
        shader_.setVec3 (std::format("u_plPos[{}]",       i).c_str(), pl.position);
        shader_.setVec3 (std::format("u_plColor[{}]",     i).c_str(), pl.color);
        shader_.setFloat(std::format("u_plIntensity[{}]", i).c_str(), pl.intensity);
        shader_.setFloat(std::format("u_plRadius[{}]",    i).c_str(), pl.radius);
    }
}

void Renderer::submit(const DrawCall& dc) noexcept {
    if (!dc.mesh || !dc.mesh->valid()) return;

    const glm::mat4 mvp          = frame_.proj * frame_.view * dc.modelMatrix;
    const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(dc.modelMatrix)));

    shader_.setMat4("u_mvp",          mvp);
    shader_.setMat4("u_model",        dc.modelMatrix);
    shader_.setMat3("u_normalMatrix", normalMatrix);
    shader_.setVec3("u_albedo",       dc.albedo);
    shader_.setFloat("u_roughness",   0.7f);

    dc.mesh->draw();

    ++drawCallCount_;
    triangleCount_ += dc.mesh->indexCount() / 3;
}

void Renderer::endFrame() noexcept {
    // Reset polygon mode in case wireframe was on
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    shader_.unbind();
}

} // namespace forge::gfx
