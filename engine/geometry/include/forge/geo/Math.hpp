#pragma once

// ─── GLM ─────────────────────────────────────────────────────────────────────
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/string_cast.hpp>

// ─── STL ─────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace forge::geo {

// ─── Epsilon constants ────────────────────────────────────────────────────────

/// Points within this distance of each other are considered identical.
inline constexpr double kVertexEpsilon = 1e-4;

/// Points within this distance of a plane are considered ON the plane.
/// Sized for integer-grid world coordinates (units = game units, not metres).
inline constexpr double kPlaneEpsilon  = 1e-4;

/// Half-extent of the initial clipping quad used to build face polygons.
/// Must be larger than any map coordinate you'll ever use.
/// 2^17 = 131 072 — comfortably covers any Quake-scale map.
inline constexpr double kHullSize = 131072.0;

// ─── AABB ────────────────────────────────────────────────────────────────────

/// Axis-aligned bounding box in double precision world space.
struct AABB {
    glm::dvec3 mins = {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()
    };
    glm::dvec3 maxs = {
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()
    };

    /// True when at least one point has been added (mins ≤ maxs on all axes).
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z;
    }

    [[nodiscard]] constexpr glm::dvec3 center() const noexcept {
        return (mins + maxs) * 0.5;
    }

    [[nodiscard]] constexpr glm::dvec3 extents() const noexcept {
        return maxs - mins;
    }

    /// Expand to include a point.
    void expand(glm::dvec3 p) noexcept {
        mins = glm::min(mins, p);
        maxs = glm::max(maxs, p);
    }

    /// Expand to include another AABB.
    void expand(const AABB& other) noexcept {
        expand(other.mins);
        expand(other.maxs);
    }

    /// True if point p is inside or on the boundary of this box.
    [[nodiscard]] bool contains(glm::dvec3 p) const noexcept {
        return p.x >= mins.x && p.x <= maxs.x
            && p.y >= mins.y && p.y <= maxs.y
            && p.z >= mins.z && p.z <= maxs.z;
    }

    /// True if this box overlaps with another.
    [[nodiscard]] bool overlaps(const AABB& other) const noexcept {
        return mins.x <= other.maxs.x && maxs.x >= other.mins.x
            && mins.y <= other.maxs.y && maxs.y >= other.mins.y
            && mins.z <= other.maxs.z && maxs.z >= other.mins.z;
    }
};

} // namespace forge::geo
