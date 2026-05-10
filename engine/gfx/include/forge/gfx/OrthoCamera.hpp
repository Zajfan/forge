#pragma once

#include <forge/geo/Math.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string_view>

namespace forge::gfx {

/// An orthographic camera for the axis-aligned 2-D editorial viewports.
///
/// Three pre-set directions cover the standard level-editor quad layout:
///   Top   — looking down (-Y).  X goes right, -Z goes up on screen.
///   Front — looking in  (+Z).   X goes right,  Y goes up on screen.
///   Right — looking in  (-X).  -Z goes right,  Y goes up on screen.
///
/// Zoom is in world-units-per-pixel: zoom=1 → 1 WU per screen pixel.
/// Default zoom=0.5 → 2 WU per pixel (shows more of the scene).
struct OrthoCamera {
    enum class Dir { Top, Front, Right };

    Dir       dir    = Dir::Top;
    glm::vec3 target = { 0.f, 0.f, 0.f };  ///< Centre of the view in world space
    float     zoom   = 0.5f;               ///< World units per screen pixel

    // ── Matrices ──────────────────────────────────────────────────────────────

    [[nodiscard]] glm::mat4 viewMatrix()  const noexcept;

    /// @param pixW  Viewport width  in pixels.
    /// @param pixH  Viewport height in pixels.
    [[nodiscard]] glm::mat4 projMatrix(float pixW, float pixH) const noexcept;

    [[nodiscard]] glm::mat4 vpMatrix(float pixW, float pixH) const noexcept {
        return projMatrix(pixW, pixH) * viewMatrix();
    }

    // ── Input ─────────────────────────────────────────────────────────────────

    /// Pan the target (mouse drag).  dx/dy in screen pixels.
    void pan(float dx, float dy) noexcept;

    /// Zoom in / out (scroll wheel delta).
    void zoomBy(float delta, float sensitivity = 0.1f) noexcept;

    // ── Helpers ───────────────────────────────────────────────────────────────

    [[nodiscard]] std::string_view label() const noexcept {
        switch (dir) {
        case Dir::Top:   return "Top";
        case Dir::Front: return "Front";
        case Dir::Right: return "Right";
        }
        return "Ortho";
    }
};

} // namespace forge::gfx
