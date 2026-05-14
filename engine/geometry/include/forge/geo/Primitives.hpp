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

/// Create a cylinder (N-sided prism with many sides for smooth appearance).
/// @param center      Centre of the base face (bottom).
/// @param radius      Radius of the cylinder.
/// @param height      Height along the +Y axis.
/// @param sides       Number of sides (default 32 for smooth appearance). Clamped to minimum 3.
[[nodiscard]] Brush makeCylinder(
    glm::dvec3 center,
    double     radius,
    double     height,
    int        sides = 32) noexcept;

/// Create a cone (N-sided pyramid with many sides for smooth appearance).
/// @param baseCenter  Centre of the base face.
/// @param radius      Base radius of the cone.
/// @param height      Height from base to apex along +Y.
/// @param sides       Number of base sides (default 32 for smooth appearance). Clamped to minimum 3.
[[nodiscard]] Brush makeCone(
    glm::dvec3 baseCenter,
    double     radius,
    double     height,
    int        sides = 32) noexcept;

/// Create a UV-sphere (tessellated sphere using latitude/longitude segments).
/// @param center              Centre of the sphere.
/// @param radius              Radius of the sphere.
/// @param latitudeSegments    Number of latitude divisions (rings). Clamped to minimum 3.
/// @param longitudeSegments   Number of longitude divisions (slices). Clamped to minimum 3.
[[nodiscard]] Brush makeSphere(
    glm::dvec3 center,
    double     radius,
    int        latitudeSegments = 16,
    int        longitudeSegments = 32) noexcept;

/// Create a torus (donut shape) lying flat on the XZ plane.
/// @param center              Centre of the torus.
/// @param majorRadius         Radius from center to tube center.
/// @param minorRadius         Radius of the tube itself.
/// @param majorSegments       Number of segments around the major circle. Clamped to minimum 3.
/// @param minorSegments       Number of segments around the minor circle. Clamped to minimum 3.
[[nodiscard]] Brush makeTorus(
    glm::dvec3 center,
    double     majorRadius,
    double     minorRadius,
    int        majorSegments = 24,
    int        minorSegments = 16) noexcept;

} // namespace forge::geo
