#include "forge/gfx/ShadowMap.hpp"
#include "Shaders.hpp"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace forge::gfx {

// ─── Init ─────────────────────────────────────────────────────────────────────

bool ShadowMap::init(int resolution) noexcept {
    res_ = resolution;

    // ── Depth texture (comparison sampler) ────────────────────────────────────
    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 res_, res_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,    GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,    GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,        GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,        GL_CLAMP_TO_BORDER);
    constexpr GLfloat border[] = {1.f,1.f,1.f,1.f}; // outside shadow = lit
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    // Hardware PCF — texture() on sampler2DShadow does 2×2 bilinear PCF
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Depth-only FBO ────────────────────────────────────────────────────────
    glGenFramebuffers(1, &depthFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, depthFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depthTex_, 0);
    glDrawBuffer(GL_NONE); // no colour attachment
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ShadowMap] FBO incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Depth-only shader ─────────────────────────────────────────────────────
    auto result = Shader::compile(shaders::kShadowVert, shaders::kShadowFrag);
    if (!result) {
        std::cerr << "[ShadowMap] Shader error: " << result.error() << '\n';
        return false;
    }
    depthShader_ = std::move(*result);

    return true;
}

void ShadowMap::shutdown() noexcept {
    if (depthFbo_) { glDeleteFramebuffers(1, &depthFbo_); depthFbo_ = 0; }
    if (depthTex_) { glDeleteTextures(1, &depthTex_);     depthTex_ = 0; }
    depthShader_ = {};
}

// ─── Light space matrix ───────────────────────────────────────────────────────

glm::mat4 ShadowMap::computeLightVP(
    glm::vec3        sunDir,
    const geo::AABB& bounds) const noexcept
{
    sunDir = glm::normalize(sunDir);

    // Scene centre and radius
    const glm::vec3 centre = bounds.isValid()
        ? glm::vec3(bounds.center())
        : glm::vec3{0.f};
    const float radius = bounds.isValid()
        ? glm::length(glm::vec3(bounds.extents())) * 0.5f + 64.f
        : 512.f;

    // Camera sits behind the scene along the light direction
    const glm::vec3 eye = centre + sunDir * (radius + 128.f);

    // Up vector must not be parallel to sunDir
    glm::vec3 up = { 0.f, 1.f, 0.f };
    if (std::abs(glm::dot(sunDir, up)) > 0.98f) up = { 0.f, 0.f, -1.f };

    const glm::mat4 view = glm::lookAt(eye, centre, up);
    const glm::mat4 proj = glm::ortho(
        -radius, radius, -radius, radius,
        0.f, (radius + 128.f) * 2.f + 256.f);

    return proj * view;
}

// ─── Shadow pass ─────────────────────────────────────────────────────────────

void ShadowMap::beginPass() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, depthFbo_);
    glViewport(0, 0, res_, res_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_FRONT); // front-face culling reduces peter-panning
    depthShader_.bind();
}

void ShadowMap::submitDepth(uint32_t vao, uint32_t indexCount,
                             const glm::mat4& model) const noexcept
{
    const glm::mat4 mvp = lightVP_ * model;
    depthShader_.setMat4("u_lightMVP", mvp);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ShadowMap::endPass() const noexcept {
    depthShader_.unbind();
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ─── Bind for main pass ───────────────────────────────────────────────────────

void ShadowMap::bindDepthTexture(int unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
}

} // namespace forge::gfx
