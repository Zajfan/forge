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

// ─── CSG Intersect ────────────────────────────────────────────────────────────

/// Compute the intersection of two brushes.
///
/// Returns the region that is inside BOTH brushes. This is equivalent to:
/// (subject − (subject − cutter)) but more direct: apply all cutter faces
/// to cut the subject from the back side.
///
/// @returns 0–N convex brush fragments representing the intersection.
///          Returns {} if the brushes do not overlap.
[[nodiscard]] std::vector<Brush>
csgIntersect(const Brush& subject, const Brush& cutter) noexcept;

// ─── CSG Union ────────────────────────────────────────────────────────────────

/// Compute the union of two brushes.
///
/// Returns all fragments that are in either brush. This operation may produce
/// a larger number of fragments than the inputs because the result is a
/// collection of convex brushes that may not perfectly fit together (i.e.,
/// the union is represented as the collection of overlapping convex pieces,
/// not a single concave brush).
///
/// @returns 1+ convex brush fragments covering (subject ∪ cutter).
///          Returns { subject, cutter } if they don't overlap or touch.
///          May return fewer fragments if brushes are touching or overlapping
///          in a way that produces a convex result.
[[nodiscard]] std::vector<Brush>
csgUnion(const Brush& subject, const Brush& cutter) noexcept;

// ─── CSG XOR ──────────────────────────────────────────────────────────────────

/// Compute the symmetric difference of two brushes.
///
/// Returns the regions that are in subject OR cutter, but NOT in both.
/// Equivalent to: (subject − cutter) ∪ (cutter − subject)
///
/// @returns 0–N convex brush fragments covering the symmetric difference.
///          Returns {} if brushes are identical.
///          Returns { subject, cutter } if they don't overlap.
[[nodiscard]] std::vector<Brush>
csgXor(const Brush& subject, const Brush& cutter) noexcept;

} // namespace forge::geo
