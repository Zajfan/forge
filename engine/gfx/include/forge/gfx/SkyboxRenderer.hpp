#pragma once

#include "Shader.hpp"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace forge::gfx {

/// Renders a procedural sky gradient before the scene geometry.
///
/// Uses the fullscreen-triangle trick (gl_VertexID, no VBO) and writes
/// gl_FragDepth = 1.0 so all scene geometry appears in front.
///
/// Two configurable tones:
///   zenith  — colour at the top of the sky (directly overhead).
///   horizon — colour at the horizon (eye level).
///   ground  — colour below the horizon (visible when looking down).
class SkyboxRenderer {
public:
    glm::vec3 zenith  = { 0.10f, 0.25f, 0.60f };
    glm::vec3 horizon = { 0.55f, 0.68f, 0.80f };
    glm::vec3 ground  = { 0.18f, 0.15f, 0.12f };

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool init() noexcept;
    void shutdown() noexcept;

    // ── Draw ──────────────────────────────────────────────────────────────────

    /// Draw the sky behind all geometry.
    /// Call BEFORE the opaque geometry pass (depth test is disabled for this draw).
    void draw(const glm::mat4& invVP) const noexcept;

    [[nodiscard]] bool valid() const noexcept { return shader_.valid(); }

private:
    Shader   shader_;
    uint32_t emptyVao_ = 0;
};

} // namespace forge::gfx
