#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "forge/geo/Plane.hpp"

using namespace forge::geo;
using Catch::Approx;

// ─── Plane construction ───────────────────────────────────────────────────────

TEST_CASE("Plane::fromPoints — XY plane", "[plane][construction]") {
    const Plane p = Plane::fromPoints({0,0,0}, {1,0,0}, {0,1,0});
    CHECK(p.normal.x   == Approx(0.0).margin(1e-9));
    CHECK(p.normal.y   == Approx(0.0).margin(1e-9));
    CHECK(p.normal.z   == Approx(1.0).margin(1e-9));
    CHECK(p.distance   == Approx(0.0).margin(1e-9));
}

TEST_CASE("Plane::fromNormalPoint — horizontal at y=10", "[plane][construction]") {
    const Plane p = Plane::fromNormalPoint({0,1,0}, {0,10,0});
    CHECK(p.normal.y  == Approx(1.0).margin(1e-9));
    CHECK(p.distance  == Approx(10.0).margin(1e-9));
}

TEST_CASE("Plane::fromNormalPoint normalises input normal", "[plane][construction]") {
    const Plane p = Plane::fromNormalPoint({0,2,0}, {0,5,0}); // non-unit normal
    const double len = glm::length(p.normal);
    CHECK(len == Approx(1.0).margin(1e-9));
}

// ─── eval / signed distance ───────────────────────────────────────────────────

TEST_CASE("Plane::eval returns correct signed distance", "[plane][eval]") {
    const Plane p = Plane::fromPoints({0,0,0}, {1,0,0}, {0,1,0}); // XY, normal +Z
    CHECK(p.eval({0, 0,  5}) == Approx( 5.0));
    CHECK(p.eval({0, 0, -3}) == Approx(-3.0));
    CHECK(p.eval({0, 0,  0}) == Approx( 0.0));
    CHECK(p.eval({99,99, 7}) == Approx( 7.0)); // x,y irrelevant for Z-normal plane
}

// ─── flipped ─────────────────────────────────────────────────────────────────

TEST_CASE("Plane::flipped inverts normal and distance", "[plane][flip]") {
    const Plane p  = Plane::fromNormalPoint({0,1,0}, {0,5,0});
    const Plane pf = p.flipped();
    CHECK(pf.normal.y == Approx(-1.0).margin(1e-9));
    CHECK(pf.distance == Approx(-5.0).margin(1e-9));
    // A point above the original plane is now behind the flipped plane
    CHECK(classifyPoint(pf, {0,10,0}) == Side::Back);
}

// ─── classifyPoint ────────────────────────────────────────────────────────────

TEST_CASE("classifyPoint — horizontal plane at y=5", "[plane][classify]") {
    const Plane p = Plane::fromNormalPoint({0,1,0}, {0,5,0});
    CHECK(classifyPoint(p, {0, 10, 0}) == Side::Front); // above
    CHECK(classifyPoint(p, {0,  0, 0}) == Side::Back);  // below
    CHECK(classifyPoint(p, {0,  5, 0}) == Side::On);    // exactly on
}

TEST_CASE("classifyPoint — diagonal plane", "[plane][classify]") {
    // Plane through origin with normal (1,1,0)/√2 — 45° in XY
    const Plane p = Plane::fromNormalPoint(
        glm::normalize(glm::dvec3{1,1,0}),
        {0,0,0});
    CHECK(classifyPoint(p, {10, 10, 0}) == Side::Front);
    CHECK(classifyPoint(p, {-5, -5, 0}) == Side::Back);
}

TEST_CASE("classifyPoint respects custom epsilon", "[plane][classify]") {
    const Plane p = Plane::fromNormalPoint({0,1,0}, {0,0,0});
    // Point 0.01 above — Front with tight eps, On with loose eps
    CHECK(classifyPoint(p, {0, 0.01, 0}, 1e-6)  == Side::Front);
    CHECK(classifyPoint(p, {0, 0.01, 0}, 0.1)   == Side::On);
}

// ─── intersectSegment ─────────────────────────────────────────────────────────

TEST_CASE("intersectSegment — segment crossing XY plane", "[plane][intersect]") {
    const Plane p = Plane::fromNormalPoint({0,0,1}, {0,0,0}); // XY plane
    const auto hit = intersectSegment(p, {0,0,-1}, {0,0,1});
    REQUIRE(hit.has_value());
    CHECK(hit->z == Approx(0.0).margin(1e-9));
}

TEST_CASE("intersectSegment — segment parallel to plane returns nullopt", "[plane][intersect]") {
    const Plane p  = Plane::fromNormalPoint({0,1,0}, {0,5,0});
    const auto hit = intersectSegment(p, {0,0,0}, {1,0,0}); // horizontal segment
    CHECK_FALSE(hit.has_value());
}

TEST_CASE("intersectSegment — segment that misses plane returns nullopt", "[plane][intersect]") {
    const Plane p  = Plane::fromNormalPoint({0,1,0}, {0,5,0}); // y=5
    const auto hit = intersectSegment(p, {0,0,0}, {0,3,0}); // ends at y=3, before plane
    CHECK_FALSE(hit.has_value());
}

// ─── intersectThreePlanes ─────────────────────────────────────────────────────

TEST_CASE("intersectThreePlanes — axis-aligned planes meeting at a point", "[plane][intersect]") {
    const Plane px = Plane::fromNormalPoint({1,0,0}, {2,0,0});
    const Plane py = Plane::fromNormalPoint({0,1,0}, {0,3,0});
    const Plane pz = Plane::fromNormalPoint({0,0,1}, {0,0,4});
    const auto v = intersectThreePlanes(px, py, pz);
    REQUIRE(v.has_value());
    CHECK(v->x == Approx(2.0).margin(1e-6));
    CHECK(v->y == Approx(3.0).margin(1e-6));
    CHECK(v->z == Approx(4.0).margin(1e-6));
}

TEST_CASE("intersectThreePlanes — two parallel planes returns nullopt", "[plane][intersect]") {
    const Plane p0 = Plane::fromNormalPoint({0,1,0}, {0,0,0});
    const Plane p1 = Plane::fromNormalPoint({0,1,0}, {0,1,0}); // parallel to p0
    const Plane p2 = Plane::fromNormalPoint({1,0,0}, {0,0,0});
    CHECK_FALSE(intersectThreePlanes(p0, p1, p2).has_value());
}

TEST_CASE("intersectThreePlanes — general oblique planes", "[plane][intersect]") {
    // Three planes: x+y+z=6, x-y=0, z=2
    // Solution: x=y, z=2, x+x+2=6 → x=2, y=2, z=2
    const Plane p0 = Plane::fromNormalPoint(glm::normalize(glm::dvec3{1,1,1}), {2,2,2});
    const Plane p1 = Plane::fromNormalPoint(glm::normalize(glm::dvec3{1,-1,0}), {0,0,0});
    const Plane p2 = Plane::fromNormalPoint({0,0,1}, {0,0,2});
    const auto v = intersectThreePlanes(p0, p1, p2);
    REQUIRE(v.has_value());
    CHECK(v->x == Approx(2.0).margin(1e-5));
    CHECK(v->y == Approx(2.0).margin(1e-5));
    CHECK(v->z == Approx(2.0).margin(1e-5));
}
