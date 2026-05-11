#pragma once

#include <forge/geo/Math.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace forge::editor {

// ─── Grid size presets ────────────────────────────────────────────────────────

inline constexpr std::array<float, 11> kGridSizes = {
    1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f, 128.f, 256.f, 512.f, 1024.f
};

[[nodiscard]] inline std::string gridLabel(float size) noexcept {
    if (size < 1.f) return std::format("{:.2f}u", size);
    return std::format("{}u", static_cast<int>(size));
}

// ─── Snap functions ───────────────────────────────────────────────────────────

/// Snap a scalar to the nearest grid multiple.
[[nodiscard]] inline float snapF(float v, float g) noexcept {
    if (g < 1e-6f) return v;
    return std::round(v / g) * g;
}

[[nodiscard]] inline double snapD(double v, double g) noexcept {
    if (g < 1e-10) return v;
    return std::round(v / g) * g;
}

/// Snap all three components of a vec3.
[[nodiscard]] inline glm::vec3 snapVec3(glm::vec3 v, float g) noexcept {
    return { snapF(v.x, g), snapF(v.y, g), snapF(v.z, g) };
}

[[nodiscard]] inline glm::dvec3 snapDVec3(glm::dvec3 v, double g) noexcept {
    return { snapD(v.x, g), snapD(v.y, g), snapD(v.z, g) };
}

/// Snap just the component that is most aligned with a face normal
/// (used for snapping plane distances — face moves along its own axis).
[[nodiscard]] inline double snapAlongNormal(
    double distance, glm::dvec3 normal, double gridSize) noexcept
{
    // The world-space position along the dominant normal axis is approximately
    // `distance` (since plane distance = dot(normal, point) and normal is unit).
    // We snap the distance directly; this keeps the plane axis-aligned.
    return snapD(distance, gridSize);
}

} // namespace forge::editor
