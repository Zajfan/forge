#include "forge/geo/Plane.hpp"

#include <cmath>

namespace forge::geo {

// ─── Plane construction ───────────────────────────────────────────────────────

Plane Plane::fromPoints(glm::dvec3 a, glm::dvec3 b, glm::dvec3 c) noexcept {
    // CCW winding → normal faces the viewer (outward convention)
    const glm::dvec3 ab = b - a;
    const glm::dvec3 ac = c - a;
    const glm::dvec3 n  = glm::normalize(glm::cross(ab, ac));
    return { n, glm::dot(n, a) };
}

Plane Plane::fromNormalPoint(glm::dvec3 normal, glm::dvec3 pointOnPlane) noexcept {
    const glm::dvec3 n = glm::normalize(normal);
    return { n, glm::dot(n, pointOnPlane) };
}

// ─── Classification ───────────────────────────────────────────────────────────

Side classifyPoint(const Plane& plane, glm::dvec3 point, double epsilon) noexcept {
    const double d = plane.eval(point);
    if (d >  epsilon) return Side::Front;
    if (d < -epsilon) return Side::Back;
    return Side::On;
}

// ─── Intersection ─────────────────────────────────────────────────────────────

std::optional<glm::dvec3>
intersectSegment(const Plane& plane, glm::dvec3 a, glm::dvec3 b) noexcept {
    const double da    = plane.eval(a);
    const double db    = plane.eval(b);
    const double denom = da - db;

    // Segment is (nearly) parallel to the plane
    if (std::abs(denom) < 1e-10) return std::nullopt;

    const double t = da / denom;

    // Intersection is outside the segment endpoints
    if (t < 0.0 || t > 1.0) return std::nullopt;

    return a + t * (b - a);
}

std::optional<glm::dvec3>
intersectThreePlanes(const Plane& p0, const Plane& p1, const Plane& p2) noexcept {
    // We solve the 3×3 linear system using Cramer's rule.
    //
    //   n0 · x = d0
    //   n1 · x = d1
    //   n2 · x = d2
    //
    // det = n0 · (n1 × n2)
    // If |det| is too small the planes are nearly parallel — no unique solution.
    //
    // Solution:
    //   x = (d0*(n1×n2) + d1*(n2×n0) + d2*(n0×n1)) / det

    const glm::dvec3 n1_cross_n2 = glm::cross(p1.normal, p2.normal);
    const double     det         = glm::dot(p0.normal, n1_cross_n2);

    if (std::abs(det) < 1e-10) return std::nullopt;

    const glm::dvec3 n2_cross_n0 = glm::cross(p2.normal, p0.normal);
    const glm::dvec3 n0_cross_n1 = glm::cross(p0.normal, p1.normal);

    return (  p0.distance * n1_cross_n2
            + p1.distance * n2_cross_n0
            + p2.distance * n0_cross_n1  ) / det;
}

} // namespace forge::geo
