#pragma once

#include "Brush.hpp"

namespace forge::geo {

/// Create an axis-aligned box brush.
/// @param mins  Minimum corner (bottom-left-back).
/// @param maxs  Maximum corner (top-right-front).
[[nodiscard]] Brush makeBox(glm::dvec3 mins, glm::dvec3 maxs) noexcept;

/// Create a wedge (right-triangular prism) from an AABB.
/// The diagonal cut runs from the front-bottom edge to the back-top edge on
/// the XZ plane. Useful for ramps and sloped architectural surfaces.
[[nodiscard]] Brush makeWedge(glm::dvec3 mins, glm::dvec3 maxs) noexcept;

/// Create a regular N-sided prism (cylinder approximation) standing upright
/// along the Y axis.
/// @param center  Centre of the base face (bottom).
/// @param radius  Circumscribed radius of each end cap.
/// @param height  Height along the +Y axis.
/// @param sides   Number of sides. Clamped to minimum 3.
[[nodiscard]] Brush makePrism(
    glm::dvec3 center,
    double     radius,
    double     height,
    int        sides) noexcept;

/// Create a regular N-sided pyramid.
/// @param baseCenter  Centre of the base face.
/// @param radius      Circumscribed radius of the base.
/// @param height      Height of the apex above the base along +Y.
/// @param sides       Number of base sides. Clamped to minimum 3.
[[nodiscard]] Brush makePyramid(
    glm::dvec3 baseCenter,
    double     radius,
    double     height,
    int        sides) noexcept;

} // namespace forge::geo
