#include "forge/gfx/SkyboxRenderer.hpp"
#include "Shaders.hpp"

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace forge::gfx {

bool SkyboxRenderer::init() noexcept {
    auto result = Shader::compile(shaders::kSkyboxVert, shaders::kSkyboxFrag);
    if (!result) {
        std::cerr << "[forge::gfx] Skybox shader error:\n" << result.error() << '\n';
        return false;
    }
    shader_ = std::move(*result);
    glGenVertexArrays(1, &emptyVao_);
    return true;
}

void SkyboxRenderer::shutdown() noexcept {
    if (emptyVao_) { glDeleteVertexArrays(1, &emptyVao_); emptyVao_ = 0; }
    shader_ = {};
}

void SkyboxRenderer::draw(const glm::mat4& invVP) const noexcept {
    if (!valid()) return;

    // Sky renders at maximum depth, before geometry
    glDisable(GL_DEPTH_TEST);

    shader_.bind();
    shader_.setMat4("u_invVP",      invVP);
    shader_.setVec3("u_skyZenith",  zenith);
    shader_.setVec3("u_skyHorizon", horizon);
    shader_.setVec3("u_skyGround",  ground);

    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    shader_.unbind();

    glEnable(GL_DEPTH_TEST);
}

} // namespace forge::gfx
