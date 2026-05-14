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

// ─── makeCylinder ────────────────────────────────────────────────────────────

/// Create a cylinder (N-sided prism) — wrapper around makePrism with default high side count.
Brush makeCylinder(glm::dvec3 center, double radius, double height, int sides) noexcept {
    return makePrism(center, radius, height, sides);
}

// ─── makeCone ────────────────────────────────────────────────────────────────

/// Create a cone (N-sided pyramid) — wrapper around makePyramid.
Brush makeCone(glm::dvec3 baseCenter, double radius, double height, int sides) noexcept {
    return makePyramid(baseCenter, radius, height, sides);
}

// ─── makeSphere ──────────────────────────────────────────────────────────────

/// Create a UV-sphere by tessellating with latitude/longitude segments.
/// Uses an octahedron-based approach with faces defined by three-point plane definitions.
Brush makeSphere(glm::dvec3 center, double radius, int latSegs, int lonSegs) noexcept {
    if (latSegs < 3) latSegs = 3;
    if (lonSegs < 3) lonSegs = 3;

    Brush b;
    b.id = "sphere";

    // Generate latitude ring vertices
    // lat 0 = south pole, lat = latSegs = north pole
    std::vector<std::vector<glm::dvec3>> rings(latSegs + 1);

    for (int lat = 0; lat <= latSegs; ++lat) {
        const double latAngle = (std::numbers::pi / static_cast<double>(latSegs)) * lat - std::numbers::pi / 2.0;
        const double latRadius = radius * std::cos(latAngle);
        const double y = radius * std::sin(latAngle);

        for (int lon = 0; lon < lonSegs; ++lon) {
            const double lonAngle = (2.0 * std::numbers::pi / static_cast<double>(lonSegs)) * lon;
            const double x = latRadius * std::cos(lonAngle);
            const double z = latRadius * std::sin(lonAngle);
            rings[lat].push_back(center + glm::dvec3{x, y, z});
        }
    }

    // Generate quads (as pairs of triangles) from latitude rings
    for (int lat = 0; lat < latSegs; ++lat) {
        for (int lon = 0; lon < lonSegs; ++lon) {
            const int lon_next = (lon + 1) % lonSegs;

            const glm::dvec3 v0 = rings[lat][lon];
            const glm::dvec3 v1 = rings[lat][lon_next];
            const glm::dvec3 v2 = rings[lat + 1][lon_next];
            const glm::dvec3 v3 = rings[lat + 1][lon];

            // Two triangles per quad (CCW from outside)
            b.faces.push_back({ Plane::fromPoints(v0, v1, v2) });
            b.faces.push_back({ Plane::fromPoints(v0, v2, v3) });
        }
    }

    return b;
}

// ─── makeTorus ───────────────────────────────────────────────────────────────

/// Create a torus (donut) by revolving a circle around a vertical axis.
/// The torus lies flat on the XZ plane with Y pointing up.
Brush makeTorus(glm::dvec3 center, double majorRadius, double minorRadius, int majSegs, int minSegs) noexcept {
    if (majSegs < 3) majSegs = 3;
    if (minSegs < 3) minSegs = 3;

    Brush b;
    b.id = "torus";

    // Generate cross-section circles at each major angle
    std::vector<std::vector<glm::dvec3>> circles(majSegs);

    for (int maj = 0; maj < majSegs; ++maj) {
        const double majAngle = (2.0 * std::numbers::pi / static_cast<double>(majSegs)) * maj;
        const double majCos = std::cos(majAngle);
        const double majSin = std::sin(majAngle);

        // Center of this cross-section circle
        const glm::dvec3 circleCenter = center + glm::dvec3{majCos * majorRadius, 0.0, majSin * majorRadius};

        for (int min = 0; min < minSegs; ++min) {
            const double minAngle = (2.0 * std::numbers::pi / static_cast<double>(minSegs)) * min;
            const double minCos = std::cos(minAngle);
            const double minSin = std::sin(minAngle);

            // Point on the torus surface
            const double px = majCos * (majorRadius + minorRadius * minCos);
            const double py = minorRadius * minSin;
            const double pz = majSin * (majorRadius + minorRadius * minCos);

            circles[maj].push_back(center + glm::dvec3{px, py, pz});
        }
    }

    // Generate quads from the circle grid
    for (int maj = 0; maj < majSegs; ++maj) {
        const int maj_next = (maj + 1) % majSegs;

        for (int min = 0; min < minSegs; ++min) {
            const int min_next = (min + 1) % minSegs;

            const glm::dvec3 v0 = circles[maj][min];
            const glm::dvec3 v1 = circles[maj][min_next];
            const glm::dvec3 v2 = circles[maj_next][min_next];
            const glm::dvec3 v3 = circles[maj_next][min];

            // Two triangles per quad (CCW from outside)
            b.faces.push_back({ Plane::fromPoints(v0, v1, v2) });
            b.faces.push_back({ Plane::fromPoints(v0, v2, v3) });
        }
    }

    return b;
}

} // namespace forge::geo
