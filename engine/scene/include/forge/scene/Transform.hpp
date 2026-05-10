#pragma once

#include <forge/geo/Math.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace forge::scene {

/// A Transform-Rotate-Scale (TRS) spatial transform stored in double precision.
///
/// Composed as:  M = T * R * S
///
/// World-space position = translation
/// World-space orientation = rotation quaternion
/// World-space scale = scale (non-uniform supported)
///
/// Brushes store their geometry in LOCAL space.
/// Apply this transform to get WORLD space positions for rendering/export/physics.
struct Transform {
    glm::dvec3 translation = { 0.0, 0.0, 0.0 };
    glm::dquat rotation    = glm::identity<glm::dquat>();
    glm::dvec3 scale       = { 1.0, 1.0, 1.0 };

    // ── Matrix ────────────────────────────────────────────────────────────────

    /// Build the 4×4 TRS matrix (double precision).
    [[nodiscard]] glm::dmat4 matrix() const noexcept;

    /// Build the inverse of the TRS matrix.
    [[nodiscard]] glm::dmat4 inverseMatrix() const noexcept;

    /// Normal transform matrix — inverse-transpose of the upper-left 3×3.
    /// Use this to transform surface normals (handles non-uniform scale).
    [[nodiscard]] glm::dmat3 normalMatrix() const noexcept;

    // ── Point / vector transform ──────────────────────────────────────────────

    /// Transform a point (affected by translation, rotation, and scale).
    [[nodiscard]] glm::dvec3 transformPoint(glm::dvec3 p) const noexcept;

    /// Transform a direction vector (not affected by translation; affected by
    /// rotation and scale).
    [[nodiscard]] glm::dvec3 transformVector(glm::dvec3 v) const noexcept;

    /// Transform a surface normal (not affected by translation; uses the
    /// inverse-transpose to handle non-uniform scale correctly).
    [[nodiscard]] glm::dvec3 transformNormal(glm::dvec3 n) const noexcept;

    // ── Fluent builders ───────────────────────────────────────────────────────

    [[nodiscard]] static Transform fromTranslation(glm::dvec3 t) noexcept {
        Transform tf; tf.translation = t; return tf;
    }

    [[nodiscard]] static Transform fromRotation(double angleDeg, glm::dvec3 axis) noexcept {
        Transform tf;
        tf.rotation = glm::angleAxis(glm::radians(angleDeg), glm::normalize(axis));
        return tf;
    }

    [[nodiscard]] static Transform identity() noexcept { return {}; }

    // ── Comparison ────────────────────────────────────────────────────────────

    [[nodiscard]] bool isIdentity() const noexcept {
        return translation == glm::dvec3{0,0,0}
            && rotation    == glm::identity<glm::dquat>()
            && scale       == glm::dvec3{1,1,1};
    }
};

} // namespace forge::scene
