#pragma once

#include "Brush.hpp"
#include <span>
#include <vector>

namespace forge::geo {

// ─── Split ────────────────────────────────────────────────────────────────────

/// Split a convex brush into two halves along an arbitrary plane.
///
/// @param brush         The brush to split.
/// @param cuttingPlane  The splitting plane. Its normal points toward the FRONT
///                      (outside / positive half-space).
///
/// @returns A pair {front, back}.
///   front → brush fragment on the positive side of the plane (or nullopt if
///           the brush lies entirely on the back/negative side).
///   back  → brush fragment on the negative side of the plane (or nullopt if
///           the brush lies entirely on the front/positive side).
///
/// Implementation: splitting a brush by a plane is just adding one new face.
///   front brush = original faces + cuttingPlane.flipped()  (outward for front piece)
///   back  brush = original faces + cuttingPlane             (outward for back piece)
[[nodiscard]] std::pair<std::optional<Brush>, std::optional<Brush>>
splitBrushByPlane(const Brush& brush, const Plane& cuttingPlane) noexcept;

// ─── CSG Subtract ────────────────────────────────────────────────────────────

/// Subtract a single cutter brush from a subject brush.
///
/// Carves the volume of @p cutter out of @p subject and returns the surviving
/// convex fragments. The subject is not modified.
///
/// @returns 0–N convex brush fragments that together represent (subject − cutter).
///          Returns { subject } unchanged if the brushes do not overlap.
///          Returns {} if the subject is entirely inside the cutter.
[[nodiscard]] std::vector<Brush>
csgSubtract(const Brush& subject, const Brush& cutter) noexcept;

/// Subtract multiple cutter brushes from a subject brush, applied sequentially.
[[nodiscard]] std::vector<Brush>
csgSubtract(const Brush& subject, std::span<const Brush> cutters) noexcept;

// ─── Hollow ───────────────────────────────────────────────────────────────────

/// Convert a solid brush into a hollow shell by creating one wall brush per face.
///
/// Each wall brush covers the material of one face with the given thickness.
/// The result is N brushes (one per face of the input) that together form a
/// hollow version of the input shape — like cutting the inside out of a box to
/// leave only the walls.
///
/// @param brush          The source solid brush.
/// @param wallThickness  Wall thickness in world units. Must be > 0 and less
///                       than the brush's minimum dimension.
///
/// @returns One brush per valid face wall. Degenerate walls (thickness ≥ brush
///          extent on that axis) are silently omitted.
[[nodiscard]] std::vector<Brush>
hollowBrush(const Brush& brush, double wallThickness) noexcept;

} // namespace forge::geo
