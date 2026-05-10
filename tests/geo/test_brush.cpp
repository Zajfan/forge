#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "forge/geo/Brush.hpp"

using namespace forge::geo;
using Catch::Approx;

// ─── Test helpers ─────────────────────────────────────────────────────────────

/// Build a simple axis-aligned box brush from mins/maxs.
/// Normals point outward — matches the convention used by the kernel.
static Brush makeBox(glm::dvec3 mins, glm::dvec3 maxs, std::string id = "box") {
    Brush b;
    b.id = std::move(id);
    b.faces = {
        { Plane::fromNormalPoint({ 1, 0, 0}, {maxs.x, 0,      0     }) }, // +X
        { Plane::fromNormalPoint({-1, 0, 0}, {mins.x, 0,      0     }) }, // -X
        { Plane::fromNormalPoint({ 0, 1, 0}, {0,      maxs.y, 0     }) }, // +Y
        { Plane::fromNormalPoint({ 0,-1, 0}, {0,      mins.y, 0     }) }, // -Y
        { Plane::fromNormalPoint({ 0, 0, 1}, {0,      0,      maxs.z}) }, // +Z
        { Plane::fromNormalPoint({ 0, 0,-1}, {0,      0,      mins.z}) }, // -Z
    };
    return b;
}

// ─── Vertex computation ───────────────────────────────────────────────────────

TEST_CASE("Unit box has exactly 8 vertices", "[brush][vertices]") {
    const Brush box = makeBox({0,0,0}, {1,1,1});
    CHECK(box.vertices().size() == 8);
}

TEST_CASE("Box vertices are all inside or on all face planes", "[brush][vertices]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    for (const auto& v : box.vertices()) {
        for (const auto& face : box.faces) {
            const Side s = classifyPoint(face.plane, v);
            CHECK(s != Side::Front); // no vertex should be outside any face
        }
    }
}

TEST_CASE("Vertex cache is populated on first access and reused", "[brush][cache]") {
    Brush box = makeBox({0,0,0}, {1,1,1});
    CHECK_FALSE(box.cachedVertices.has_value());
    const auto& v1 = box.vertices();
    CHECK(box.cachedVertices.has_value());
    const auto& v2 = box.vertices(); // second call hits cache
    CHECK(&v1 == &v2);               // same underlying storage
}

TEST_CASE("invalidate() clears vertex and polygon caches", "[brush][cache]") {
    Brush box = makeBox({0,0,0}, {1,1,1});
    (void)box.vertices();
    CHECK(box.cachedVertices.has_value());
    box.invalidate();
    CHECK_FALSE(box.cachedVertices.has_value());
    CHECK_FALSE(box.cachedFacePolygons.has_value());
}

// ─── Face polygons ────────────────────────────────────────────────────────────

TEST_CASE("Unit box has 6 face polygons, each a quad (4 verts)", "[brush][polygons]") {
    const Brush box = makeBox({0,0,0}, {1,1,1});
    const auto& polys = box.allFacePolygons();
    REQUIRE(polys.size() == 6);
    for (const auto& poly : polys) {
        CHECK(poly.size() == 4);
    }
}

TEST_CASE("Face polygon vertices lie on the face plane", "[brush][polygons]") {
    const Brush box = makeBox({0,0,0}, {32,32,32});
    const auto& polys = box.allFacePolygons();
    for (std::size_t f = 0; f < box.faces.size(); ++f) {
        for (const auto& v : polys[f]) {
            const double dist = box.faces[f].plane.eval(v);
            CHECK(std::abs(dist) < 1e-3); // vertex is on the face plane
        }
    }
}

TEST_CASE("Face polygon vertices are inside all other face planes", "[brush][polygons]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    const auto& polys = box.allFacePolygons();
    for (std::size_t f = 0; f < box.faces.size(); ++f) {
        for (const auto& v : polys[f]) {
            for (std::size_t g = 0; g < box.faces.size(); ++g) {
                if (g == f) continue;
                CHECK(classifyPoint(box.faces[g].plane, v) != Side::Front);
            }
        }
    }
}

// ─── Bounds ───────────────────────────────────────────────────────────────────

TEST_CASE("Box bounds match construction mins/maxs", "[brush][bounds]") {
    const Brush box = makeBox({-10, -20, -30}, {40, 50, 60});
    const AABB b = box.bounds();
    CHECK(b.mins.x == Approx(-10.0).margin(1e-3));
    CHECK(b.mins.y == Approx(-20.0).margin(1e-3));
    CHECK(b.mins.z == Approx(-30.0).margin(1e-3));
    CHECK(b.maxs.x == Approx( 40.0).margin(1e-3));
    CHECK(b.maxs.y == Approx( 50.0).margin(1e-3));
    CHECK(b.maxs.z == Approx( 60.0).margin(1e-3));
}

// ─── Polygon clip ─────────────────────────────────────────────────────────────

TEST_CASE("clipPolygonByPlane — clip a quad, keep top half", "[brush][clip]") {
    // A unit square in XY, clip it at y=0.5 keeping the bottom (back) side
    std::vector<glm::dvec3> quad = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}
    };
    // Plane: normal +Y, distance 0.5 → front = y > 0.5, back = y < 0.5
    const Plane clip = Plane::fromNormalPoint({0,1,0}, {0, 0.5, 0});
    const auto result = clipPolygonByPlane(quad, clip);

    // Should produce a rectangle with 4 vertices, all at y ≤ 0.5
    REQUIRE(result.size() == 4);
    for (const auto& v : result) {
        CHECK(v.y <= 0.5 + 1e-6);
    }
}

TEST_CASE("clipPolygonByPlane — fully inside returns unchanged", "[brush][clip]") {
    std::vector<glm::dvec3> quad = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}
    };
    // Clip plane far away — all polygon verts are on the back (inside) side
    const Plane clip = Plane::fromNormalPoint({0,1,0}, {0, 100, 0});
    const auto result = clipPolygonByPlane(quad, clip);
    CHECK(result.size() == 4);
}

TEST_CASE("clipPolygonByPlane — fully outside returns empty", "[brush][clip]") {
    std::vector<glm::dvec3> quad = {
        {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}
    };
    // Clip plane cuts below y=0 — entire polygon is on the front (outside) side
    const Plane clip = Plane::fromNormalPoint({0,1,0}, {0, -10, 0});
    const auto result = clipPolygonByPlane(quad, clip);
    CHECK(result.empty());
}

// ─── Validation ───────────────────────────────────────────────────────────────

TEST_CASE("Valid box brush passes validation", "[brush][validate]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    const auto v = box.validate();
    INFO("Errors: " << (v.errors.empty() ? "none" : v.errors.front()));
    CHECK(v.valid);
    CHECK(v.errors.empty());
}

TEST_CASE("Brush with < 4 faces fails validation", "[brush][validate]") {
    Brush b;
    b.id = "bad";
    b.faces = {
        { Plane::fromNormalPoint({1,0,0}, {0,0,0}) },
        { Plane::fromNormalPoint({0,1,0}, {0,0,0}) },
        { Plane::fromNormalPoint({0,0,1}, {0,0,0}) },
    };
    const auto v = b.validate();
    CHECK_FALSE(v.valid);
    CHECK_FALSE(v.errors.empty());
}

TEST_CASE("Brush with duplicate face planes fails validation", "[brush][validate]") {
    Brush b = makeBox({0,0,0}, {64,64,64});
    // Replace the -X face with a duplicate of the +X face
    b.faces[1] = b.faces[0];
    b.invalidate();
    const auto v = b.validate();
    CHECK_FALSE(v.valid);
}
