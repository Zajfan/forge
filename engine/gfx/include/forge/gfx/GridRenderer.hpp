#pragma once

#include "Shader.hpp"
#include <glm/mat4x4.hpp>

namespace forge::gfx {

/// Renders an infinite anti-aliased grid in the XZ plane.
///
/// Uses the fullscreen-triangle trick (3 vertices from gl_VertexID, no VBO).
/// Depth is written correctly via gl_FragDepth so the grid sits behind geometry.
/// Two grid scales are composited: 64-unit minor and 512-unit major.
/// X and Z axes are highlighted in red and blue respectively.
class GridRenderer {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Compile shader + create empty VAO. Must be called with a GL context.
    [[nodiscard]] bool init() noexcept;

    void shutdown() noexcept;

    // ── Draw ──────────────────────────────────────────────────────────────────

    /// Draw the grid for the current frame's camera.
    /// Call AFTER the opaque geometry pass (additive blend over the scene).
    void draw(
        const glm::mat4& view,
        const glm::mat4& proj,
        float            nearZ,
        float            farZ) const noexcept;

    [[nodiscard]] bool valid() const noexcept { return shader_.valid(); }

private:
    Shader   shader_;
    uint32_t emptyVao_ = 0;  ///< Empty VAO required by GL Core Profile
};

} // namespace forge::gfx
