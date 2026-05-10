#pragma once

#include "Math.hpp"
#include "Plane.hpp"

namespace forge::geo {

// ─── BrushFace ────────────────────────────────────────────────────────────────

/// One face of a brush — a plane with material and UV mapping data.
///
/// The polygon for this face (the actual vertex list) is NOT stored here.
/// It is computed on demand by the parent Brush and cached there.
/// This keeps the source-of-truth minimal: just the plane equation and material.
struct BrushFace {
    Plane       plane;
    std::string materialId = "default";

    // Planar UV mapping parameters (applied when building render mesh)
    glm::vec2   uvOffset   = { 0.f, 0.f };
    glm::vec2   uvScale    = { 1.f, 1.f };
    float       uvRotation = 0.f;           ///< Degrees.
};

// ─── BrushValidation ─────────────────────────────────────────────────────────

/// Result of Brush::validate(). Check `valid` first; inspect `errors` on failure.
struct BrushValidation {
    bool                     valid = false;
    std::vector<std::string> errors;
};

// ─── Brush ────────────────────────────────────────────────────────────────────

/// A convex solid defined entirely by the intersection of N half-spaces.
///
/// ## What is stored
///   - A list of BrushFace (plane + material). That is all.
///
/// ## What is computed on demand and cached
///   - Vertex positions (intersection of face-plane triples).
///   - Per-face convex polygons (Sutherland-Hodgman clip of each face against
///     all other face planes).
///   - AABB.
///
/// ## Invariants
///   - Every face plane's normal points OUTWARD (away from the brush interior).
///   - At least 4 faces (tetrahedron minimum).
///   - All computed vertices lie inside (or on) every face plane.
///
/// ## Cache invalidation
///   Call invalidate() after modifying any face. The cache is rebuilt lazily
///   on the next call to vertices(), facePolygon(), or bounds().
struct Brush {
    std::string            id;
    std::vector<BrushFace> faces;

    // ── Computed / cached data ────────────────────────────────────────────────
    // mutable so const query methods can populate the cache.

    /// All brush vertices (deduplicated, in no guaranteed order).
    mutable std::optional<std::vector<glm::dvec3>>                  cachedVertices;

    /// Per-face convex polygon — cachedFacePolygons[i] corresponds to faces[i].
    mutable std::optional<std::vector<std::vector<glm::dvec3>>>     cachedFacePolygons;

    // ── Query interface ───────────────────────────────────────────────────────

    /// All brush vertices. Computed and cached on first call after invalidation.
    [[nodiscard]] const std::vector<glm::dvec3>&
    vertices() const noexcept;

    /// Convex polygon for a single face. Computed with all face polygons together.
    /// @param faceIdx  Index into faces[].
    [[nodiscard]] const std::vector<glm::dvec3>&
    facePolygon(std::size_t faceIdx) const noexcept;

    /// All face polygons. Index i corresponds to faces[i].
    [[nodiscard]] const std::vector<std::vector<glm::dvec3>>&
    allFacePolygons() const noexcept;

    /// Tight axis-aligned bounding box around all vertices.
    [[nodiscard]] AABB bounds() const noexcept;

    /// Validate brush geometry. Returns a BrushValidation with any errors found.
    /// A valid brush is convex, closed, and non-degenerate.
    [[nodiscard]] BrushValidation validate() const noexcept;

    /// Mark all cached data dirty. Must be called after modifying any face.
    void invalidate() noexcept;
};

// ─── Free functions (internal — prefer Brush member calls) ───────────────────

/// Compute all vertices of a brush from its face planes.
/// A point is a vertex if it is the intersection of exactly 3 planes AND lies
/// on the inside (Back or On) of every other plane.
[[nodiscard]] std::vector<glm::dvec3>
computeBrushVertices(const Brush& brush) noexcept;

/// Clip a convex polygon by a plane using the Sutherland-Hodgman algorithm.
///
/// @param poly   Input polygon vertices in CCW order.
/// @param plane  Clipping plane. Vertices on the BACK side are kept.
/// @returns      Clipped polygon, or empty if fully clipped away.
[[nodiscard]] std::vector<glm::dvec3>
clipPolygonByPlane(
    std::span<const glm::dvec3> poly,
    const Plane&                plane) noexcept;

/// Compute the convex polygon for one face of a brush.
///
/// Starts with a large quad on the face's plane and clips it against every
/// other face plane (keeping the back/interior side each time).
///
/// @param brush    The brush containing the face.
/// @param faceIdx  Which face to compute.
/// @returns        Polygon in CCW winding viewed from outside the brush,
///                 or empty if the face is degenerate.
[[nodiscard]] std::vector<glm::dvec3>
computeFacePolygon(const Brush& brush, std::size_t faceIdx) noexcept;

} // namespace forge::geo
