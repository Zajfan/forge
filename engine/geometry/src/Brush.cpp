#include "forge/geo/Brush.hpp"

#include <algorithm>
#include <format>
#include <ranges>

namespace forge::geo {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static bool verticesEqual(glm::dvec3 a, glm::dvec3 b) noexcept {
    return glm::length(a - b) < kVertexEpsilon;
}

// ─── Vertex computation ───────────────────────────────────────────────────────

std::vector<glm::dvec3>
computeBrushVertices(const Brush& brush) noexcept {
    std::vector<glm::dvec3> result;
    const auto& faces = brush.faces;
    const std::size_t n = faces.size();

    // Test every triple of face planes for a common point.
    // O(F^3) — fine since brushes have 4–32 faces in practice.
    for (std::size_t i = 0; i < n - 2; ++i) {
        for (std::size_t j = i + 1; j < n - 1; ++j) {
            for (std::size_t k = j + 1; k < n; ++k) {

                auto v = intersectThreePlanes(
                    faces[i].plane, faces[j].plane, faces[k].plane);

                if (!v) continue; // planes are parallel — no intersection

                // Valid brush vertex: must be on the Back (inside) or On side
                // of every face plane. A Front classification means the point
                // is outside the brush volume — discard it.
                bool inside = true;
                for (std::size_t f = 0; f < n && inside; ++f) {
                    if (classifyPoint(faces[f].plane, *v) == Side::Front)
                        inside = false;
                }
                if (!inside) continue;

                // Deduplicate — multiple triples can produce the same corner
                const bool duplicate = std::ranges::any_of(result,
                    [&](const glm::dvec3& existing) {
                        return verticesEqual(existing, *v);
                    });

                if (!duplicate)
                    result.push_back(*v);
            }
        }
    }

    return result;
}

// ─── Sutherland-Hodgman clip ─────────────────────────────────────────────────

std::vector<glm::dvec3>
clipPolygonByPlane(std::span<const glm::dvec3> poly, const Plane& plane) noexcept {
    if (poly.empty()) return {};

    std::vector<glm::dvec3> out;
    out.reserve(poly.size() + 1);

    for (std::size_t i = 0; i < poly.size(); ++i) {
        const glm::dvec3& curr = poly[i];
        const glm::dvec3& next = poly[(i + 1) % poly.size()];

        const Side sCurr = classifyPoint(plane, curr);
        const Side sNext = classifyPoint(plane, next);

        // Keep curr if it is inside (Back) or on the plane.
        if (sCurr != Side::Front)
            out.push_back(curr);

        // If the edge straddles the plane, emit the crossing point.
        //   Front→Back  or  Back→Front
        const bool crosses =
            (sCurr == Side::Front && sNext == Side::Back) ||
            (sCurr == Side::Back  && sNext == Side::Front);

        if (crosses) {
            if (auto xp = intersectSegment(plane, curr, next))
                out.push_back(*xp);
        }
    }

    return out;
}

// ─── Face polygon computation ─────────────────────────────────────────────────

std::vector<glm::dvec3>
computeFacePolygon(const Brush& brush, std::size_t faceIdx) noexcept {
    const Plane& facePlane = brush.faces[faceIdx].plane;
    const glm::dvec3& n   = facePlane.normal;

    // Build an orthonormal basis (u, v) on the face plane.
    // Choose a helper vector that is not parallel to n.
    glm::dvec3 helper;
    if (std::abs(n.x) < 0.9)
        helper = { 1.0, 0.0, 0.0 };
    else
        helper = { 0.0, 1.0, 0.0 };

    const glm::dvec3 u = glm::normalize(glm::cross(n, helper));
    const glm::dvec3 v = glm::cross(n, u); // already unit length

    // A point on the plane surface.
    const glm::dvec3 origin = n * facePlane.distance;

    // Start with a very large quad centred on the face, in CCW winding
    // viewed from outside (from the front of facePlane).
    std::vector<glm::dvec3> poly = {
        origin - u * kHullSize - v * kHullSize,
        origin + u * kHullSize - v * kHullSize,
        origin + u * kHullSize + v * kHullSize,
        origin - u * kHullSize + v * kHullSize,
    };

    // Clip the quad against every other face plane.
    // We keep the BACK side (interior of the brush) each time.
    // clipPolygonByPlane keeps the Back side, so we pass the plane as-is
    // (each face plane's back side is the brush interior).
    for (std::size_t f = 0; f < brush.faces.size(); ++f) {
        if (f == faceIdx) continue;

        // The other face's plane points outward. Its back side = brush interior.
        poly = clipPolygonByPlane(poly, brush.faces[f].plane);

        if (poly.size() < 3)
            return {}; // face fully clipped — degenerate brush
    }

    return poly;
}

// ─── Brush member implementations ────────────────────────────────────────────

void Brush::invalidate() noexcept {
    cachedVertices.reset();
    cachedFacePolygons.reset();
    cachedBounds.reset();
}

const std::vector<glm::dvec3>& Brush::vertices() const noexcept {
    if (!cachedVertices)
        cachedVertices = computeBrushVertices(*this);
    return *cachedVertices;
}

const std::vector<std::vector<glm::dvec3>>&
Brush::allFacePolygons() const noexcept {
    if (!cachedFacePolygons) {
        auto& polys = cachedFacePolygons.emplace();
        polys.reserve(faces.size());
        for (std::size_t i = 0; i < faces.size(); ++i)
            polys.push_back(computeFacePolygon(*this, i));
    }
    return *cachedFacePolygons;
}

const std::vector<glm::dvec3>&
Brush::facePolygon(std::size_t faceIdx) const noexcept {
    return allFacePolygons()[faceIdx];
}

AABB Brush::bounds() const noexcept {
    if (!cachedBounds) {
        AABB box;
        for (const auto& v : vertices())
            box.expand(v);
        cachedBounds = box;
    }
    return *cachedBounds;
}

BrushValidation Brush::validate() const noexcept {
    BrushValidation result;

    // ── Structural checks ─────────────────────────────────────────────────────

    if (faces.size() < 4) {
        result.errors.push_back(std::format(
            "Brush '{}' has {} faces — minimum is 4 (tetrahedron).",
            id, faces.size()));
        return result; // can't proceed without enough faces
    }

    // ── Vertex checks ─────────────────────────────────────────────────────────

    const auto& verts = vertices();

    if (verts.size() < 4) {
        result.errors.push_back(std::format(
            "Brush '{}' produced {} vertices — likely degenerate or "
            "coincident face planes.", id, verts.size()));
    }

    // ── Face polygon checks ───────────────────────────────────────────────────

    const auto& polys = allFacePolygons();

    for (std::size_t i = 0; i < polys.size(); ++i) {
        if (polys[i].size() < 3) {
            result.errors.push_back(std::format(
                "Brush '{}' face {} has a degenerate polygon ({} verts after "
                "clipping).", id, i, polys[i].size()));
        }
    }

    // ── Parallel / duplicate face plane checks ────────────────────────────────

    for (std::size_t i = 0; i < faces.size() - 1; ++i) {
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
            if (faces[i].plane.isParallelTo(faces[j].plane)) {
                // Same direction AND same distance → duplicate plane
                const bool sameDist =
                    std::abs(faces[i].plane.distance - faces[j].plane.distance)
                    < kPlaneEpsilon;
                if (sameDist) {
                    result.errors.push_back(std::format(
                        "Brush '{}' faces {} and {} are duplicate planes.", id, i, j));
                }
                // Opposite direction AND same |distance| → zero-volume slab
                const bool opposite =
                    glm::dot(faces[i].plane.normal, faces[j].plane.normal) < -0.9
                    && std::abs(faces[i].plane.distance + faces[j].plane.distance)
                       < kPlaneEpsilon;
                if (opposite) {
                    result.errors.push_back(std::format(
                        "Brush '{}' faces {} and {} form a zero-thickness slab.", id, i, j));
                }
            }
        }
    }

    result.valid = result.errors.empty();
    return result;
}

} // namespace forge::geo
