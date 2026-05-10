#include "forge/gfx/GridRenderer.hpp"
#include "Shaders.hpp"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace forge::gfx {

bool GridRenderer::init() noexcept {
    auto result = Shader::compile(shaders::kGridVert, shaders::kGridFrag);
    if (!result) {
        std::cerr << "[forge::gfx] Grid shader compile error:\n" << result.error() << '\n';
        return false;
    }
    shader_ = std::move(*result);

    // Empty VAO — required by Core Profile even though we use gl_VertexID
    glGenVertexArrays(1, &emptyVao_);
    return true;
}

void GridRenderer::shutdown() noexcept {
    if (emptyVao_) { glDeleteVertexArrays(1, &emptyVao_); emptyVao_ = 0; }
    shader_ = {};
}

void GridRenderer::draw(
    const glm::mat4& view,
    const glm::mat4& proj,
    float            nearZ,
    float            farZ) const noexcept
{
    if (!valid()) return;

    // The grid is semi-transparent — enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); // don't write to depth so opaque geometry wins

    shader_.bind();

    const glm::mat4 invVP = glm::inverse(proj * view);
    shader_.setMat4("u_invVP", invVP);
    shader_.setMat4("u_proj",  proj);
    shader_.setMat4("u_view",  view);
    shader_.setFloat("u_nearZ", nearZ);
    shader_.setFloat("u_farZ",  farZ);

    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3); // fullscreen triangle from gl_VertexID
    glBindVertexArray(0);

    shader_.unbind();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace forge::gfx
