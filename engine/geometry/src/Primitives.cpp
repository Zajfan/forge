#include "forge/geo/Primitives.hpp"

#include <cmath>
#include <numbers>

namespace forge::geo {

// ─── makeBox ─────────────────────────────────────────────────────────────────

/// Create a box brush from axis-aligned min/max corners.
/// The 6 face planes point outward (positive normal = outward).
Brush makeBox(glm::dvec3 mins, glm::dvec3 maxs) noexcept {
    Brush b;
    b.id = "box";
    b.faces = {
        // +X face (right)
        { Plane::fromNormalPoint({ 1.0, 0.0, 0.0}, {maxs.x, 0.0,   0.0  }) },
        // -X face (left)
        { Plane::fromNormalPoint({-1.0, 0.0, 0.0}, {mins.x, 0.0,   0.0  }) },
        // +Y face (top / up)
        { Plane::fromNormalPoint({ 0.0, 1.0, 0.0}, {0.0,   maxs.y, 0.0  }) },
        // -Y face (bottom / down)
        { Plane::fromNormalPoint({ 0.0,-1.0, 0.0}, {0.0,   mins.y, 0.0  }) },
        // +Z face (front)
        { Plane::fromNormalPoint({ 0.0, 0.0, 1.0}, {0.0,   0.0,   maxs.z}) },
        // -Z face (back)
        { Plane::fromNormalPoint({ 0.0, 0.0,-1.0}, {0.0,   0.0,   mins.z}) },
    };
    return b;
}

// ─── makeWedge ───────────────────────────────────────────────────────────────

/// Create a wedge (right-triangular prism) from an AABB.
/// The wedge cuts the box diagonally on the XZ plane, keeping the
/// front-bottom / back-top portion. Useful for ramps and sloped surfaces.
Brush makeWedge(glm::dvec3 mins, glm::dvec3 maxs) noexcept {
    Brush b;
    b.id = "wedge";

    // Five faces: bottom, left, right, back, and the diagonal slope
    const glm::dvec3 frontBottom = { mins.x, mins.y, maxs.z };
    const glm::dvec3 backTop     = { mins.x, maxs.y, mins.z };
    const glm::dvec3 backTopR    = { maxs.x, maxs.y, mins.z };

    b.faces = {
        { Plane::fromNormalPoint({-1.0, 0.0, 0.0}, {mins.x, 0.0, 0.0}) }, // -X
        { Plane::fromNormalPoint({ 1.0, 0.0, 0.0}, {maxs.x, 0.0, 0.0}) }, // +X
        { Plane::fromNormalPoint({ 0.0,-1.0, 0.0}, {0.0, mins.y, 0.0}) }, // -Y (bottom)
        { Plane::fromNormalPoint({ 0.0, 0.0,-1.0}, {0.0, 0.0, mins.z}) }, // -Z (back)
        // Slope: defined by three points on the diagonal face
        { Plane::fromPoints(frontBottom,
                            frontBottom + glm::dvec3{1.0, 0.0, 0.0},
                            backTop) },
    };
    return b;
}

// ─── makePrism ───────────────────────────────────────────────────────────────

/// Create a regular N-sided prism (cylinder approximation).
/// @param center   Centre of the prism's base.
/// @param radius   Circumscribed radius.
/// @param height   Height along the Y axis.
/// @param sides    Number of sides (minimum 3).
Brush makePrism(glm::dvec3 center, double radius, double height, int sides) noexcept {
    if (sides < 3) sides = 3;

    Brush b;
    b.id = "prism";

    const double step = 2.0 * std::numbers::pi / static_cast<double>(sides);

    // Top and bottom caps
    const glm::dvec3 topCenter    = center + glm::dvec3{0.0, height, 0.0};
    b.faces.push_back({ Plane::fromNormalPoint({ 0.0, 1.0, 0.0}, topCenter) });
    b.faces.push_back({ Plane::fromNormalPoint({ 0.0,-1.0, 0.0}, center   ) });

    // Side faces — each defined by a point on the cylinder wall and an
    // outward-pointing normal in the XZ plane.
    for (int i = 0; i < sides; ++i) {
        const double angle  = step * static_cast<double>(i) + step * 0.5;
        const double nx     = std::cos(angle);
        const double nz     = std::sin(angle);
        const glm::dvec3 pointOnFace = center + glm::dvec3{nx * radius, 0.0, nz * radius};
        b.faces.push_back({ Plane::fromNormalPoint({nx, 0.0, nz}, pointOnFace) });
    }

    return b;
}

// ─── makePyramid ─────────────────────────────────────────────────────────────

/// Create a regular N-sided pyramid.
/// @param baseCenter  Centre of the base (Y = 0 relative).
/// @param radius      Base circumscribed radius.
/// @param height      Height of the apex above the base.
/// @param sides       Number of base sides (minimum 3).
Brush makePyramid(glm::dvec3 baseCenter, double radius, double height, int sides) noexcept {
    if (sides < 3) sides = 3;

    Brush b;
    b.id = "pyramid";

    const double step = 2.0 * std::numbers::pi / static_cast<double>(sides);
    const glm::dvec3 apex = baseCenter + glm::dvec3{0.0, height, 0.0};

    // Base face (pointing downward)
    b.faces.push_back({ Plane::fromNormalPoint({0.0, -1.0, 0.0}, baseCenter) });

    // Side faces — defined by two adjacent base vertices and the apex
    for (int i = 0; i < sides; ++i) {
        const double a0 = step * static_cast<double>(i);
        const double a1 = step * static_cast<double>(i + 1);

        const glm::dvec3 v0 = baseCenter + glm::dvec3{std::cos(a0) * radius, 0.0, std::sin(a0) * radius};
        const glm::dvec3 v1 = baseCenter + glm::dvec3{std::cos(a1) * radius, 0.0, std::sin(a1) * radius};

        // CCW winding viewed from outside: apex, v1, v0
        b.faces.push_back({ Plane::fromPoints(apex, v1, v0) });
    }

    return b;
}

} // namespace forge::geo
