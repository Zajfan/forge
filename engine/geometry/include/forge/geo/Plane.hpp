#pragma once

#include "Math.hpp"

namespace forge::geo {

// ─── Side ────────────────────────────────────────────────────────────────────

/// Which side of a plane a point lies on.
/// Front = outside the brush (positive half-space).
/// Back  = inside the brush (negative half-space).
/// On    = on the plane surface (within kPlaneEpsilon).
enum class Side : int8_t {
    Front =  1,
    On    =  0,
    Back  = -1,
};

// ─── Plane ───────────────────────────────────────────────────────────────────

/// An oriented plane in 3D space defined by a unit normal and a signed distance
/// from the world origin.
///
/// Implicit equation:  dot(normal, P) = distance
/// Signed distance:    dot(normal, P) - distance
///   > 0  →  P is in front of (outside) the plane
///   < 0  →  P is behind (inside) the plane
///   = 0  →  P is on the plane
///
/// All computation inside the geometry kernel uses double precision.
/// The renderer receives float copies.
struct Plane {
    glm::dvec3 normal   = { 0.0, 0.0, 1.0 }; ///< Must be unit length.
    double     distance = 0.0;                ///< Signed offset from origin.

    // ── Construction ─────────────────────────────────────────────────────────

    /// Construct a plane from three non-collinear points.
    /// Winding is CCW when viewed from the outside (front) — same convention
    /// as the Quake .map format.
    [[nodiscard]] static Plane fromPoints(
        glm::dvec3 a, glm::dvec3 b, glm::dvec3 c) noexcept;

    /// Construct a plane from a (possibly non-unit) normal and a point that
    /// lies on the plane.
    [[nodiscard]] static Plane fromNormalPoint(
        glm::dvec3 normal, glm::dvec3 pointOnPlane) noexcept;

    // ── Queries ───────────────────────────────────────────────────────────────

    /// Signed distance from this plane to point p.
    /// Positive → front/outside, negative → back/inside.
    [[nodiscard]] constexpr double eval(glm::dvec3 p) const noexcept {
        return glm::dot(normal, p) - distance;
    }

    /// Return a plane with its orientation flipped (normal and distance negated).
    [[nodiscard]] constexpr Plane flipped() const noexcept {
        return { -normal, -distance };
    }

    /// Project a point onto this plane (closest point on plane).
    [[nodiscard]] constexpr glm::dvec3 project(glm::dvec3 p) const noexcept {
        return p - eval(p) * normal;
    }

    /// True if this plane's normal is (nearly) parallel to another plane's normal.
    [[nodiscard]] bool isParallelTo(const Plane& other, double eps = 1e-6) const noexcept {
        return std::abs(std::abs(glm::dot(normal, other.normal)) - 1.0) < eps;
    }
};

// ─── Free functions ───────────────────────────────────────────────────────────

/// Classify which side of @p plane the point @p point lies on.
/// Points within kPlaneEpsilon of the surface return Side::On.
[[nodiscard]] Side classifyPoint(
    const Plane& plane,
    glm::dvec3   point,
    double       epsilon = kPlaneEpsilon) noexcept;

/// Intersect a line segment (a → b) with a plane.
/// Returns the intersection point, or nullopt if the segment is parallel to
/// the plane or does not cross it.
[[nodiscard]] std::optional<glm::dvec3> intersectSegment(
    const Plane& plane,
    glm::dvec3   a,
    glm::dvec3   b) noexcept;

/// Intersect three planes and return their common point.
/// Returns nullopt if any two planes are parallel (degenerate configuration).
///
/// Solved via Cramer's rule on the 3×3 system:
///   [n0]   [d0]
///   [n1] x = [d1]
///   [n2]   [d2]
[[nodiscard]] std::optional<glm::dvec3> intersectThreePlanes(
    const Plane& p0,
    const Plane& p1,
    const Plane& p2) noexcept;

} // namespace forge::geo
