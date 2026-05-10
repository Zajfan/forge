#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "forge/geo/CSG.hpp"
#include "forge/geo/Primitives.hpp"

using namespace forge::geo;
using Catch::Approx;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static double totalVolume(const std::vector<Brush>& brushes) {
    // Approximate volume via AABB sum — good enough for axis-aligned test cases.
    double v = 0.0;
    for (const auto& b : brushes) {
        const auto ext = b.bounds().extents();
        v += ext.x * ext.y * ext.z;
    }
    return v;
}

// ─── splitBrushByPlane ───────────────────────────────────────────────────────

TEST_CASE("splitBrushByPlane — cuts a box into two valid halves", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    const Plane midX = Plane::fromNormalPoint({1,0,0}, {32,0,0});

    auto [front, back] = splitBrushByPlane(box, midX);

    REQUIRE(front.has_value());
    REQUIRE(back.has_value());
    CHECK(front->validate().valid);
    CHECK(back->validate().valid);
}

TEST_CASE("splitBrushByPlane — front half is bounded at x=32", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    const Plane midX = Plane::fromNormalPoint({1,0,0}, {32,0,0});
    auto [front, back] = splitBrushByPlane(box, midX);

    // Front piece: x > 32
    REQUIRE(front.has_value());
    const AABB fb = front->bounds();
    CHECK(fb.mins.x == Approx(32.0).margin(1e-3));
    CHECK(fb.maxs.x == Approx(64.0).margin(1e-3));
}

TEST_CASE("splitBrushByPlane — back half is bounded at x=32", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    const Plane midX = Plane::fromNormalPoint({1,0,0}, {32,0,0});
    auto [front, back] = splitBrushByPlane(box, midX);

    // Back piece: x < 32
    REQUIRE(back.has_value());
    const AABB bb = back->bounds();
    CHECK(bb.mins.x == Approx(0.0).margin(1e-3));
    CHECK(bb.maxs.x == Approx(32.0).margin(1e-3));
}

TEST_CASE("splitBrushByPlane — plane outside brush returns {box, nullopt}", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    // Plane at x=200, far in front of the box — box entirely behind
    const Plane far = Plane::fromNormalPoint({1,0,0}, {200,0,0});
    auto [front, back] = splitBrushByPlane(box, far);
    CHECK_FALSE(front.has_value());
    REQUIRE(back.has_value());
    CHECK(back->validate().valid);
}

TEST_CASE("splitBrushByPlane — plane behind brush returns {nullopt, box}", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,64,64});
    // Plane at x=-100, entirely behind the box — box entirely in front
    const Plane behind = Plane::fromNormalPoint({1,0,0}, {-100,0,0});
    auto [front, back] = splitBrushByPlane(box, behind);
    REQUIRE(front.has_value());
    CHECK_FALSE(back.has_value());
    CHECK(front->validate().valid);
}

TEST_CASE("splitBrushByPlane — both halves together cover original AABB", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,32,64});
    const Plane midY = Plane::fromNormalPoint({0,1,0}, {0,16,0});
    auto [front, back] = splitBrushByPlane(box, midY);

    REQUIRE(front.has_value());
    REQUIRE(back.has_value());

    AABB combined;
    combined.expand(front->bounds());
    combined.expand(back->bounds());

    const AABB orig = box.bounds();
    CHECK(combined.mins.x == Approx(orig.mins.x).margin(1e-3));
    CHECK(combined.mins.y == Approx(orig.mins.y).margin(1e-3));
    CHECK(combined.maxs.x == Approx(orig.maxs.x).margin(1e-3));
    CHECK(combined.maxs.y == Approx(orig.maxs.y).margin(1e-3));
}

TEST_CASE("splitBrushByPlane — resulting brushes have one more face", "[csg][split]") {
    const Brush box = makeBox({0,0,0}, {64,64,64}); // 6 faces
    const Plane cut = Plane::fromNormalPoint({1,0,0}, {32,0,0});
    auto [front, back] = splitBrushByPlane(box, cut);

    REQUIRE(front.has_value());
    REQUIRE(back.has_value());
    CHECK(front->faces.size() == 7); // 6 original + 1 cut face
    CHECK(back->faces.size()  == 7);
}

// ─── csgSubtract ─────────────────────────────────────────────────────────────

TEST_CASE("csgSubtract — non-overlapping brushes return subject unchanged", "[csg][subtract]") {
    const Brush subject = makeBox({0,0,0}, {64,64,64});
    const Brush cutter  = makeBox({200,200,200}, {264,264,264});

    const auto result = csgSubtract(subject, cutter);

    REQUIRE(result.size() == 1);
    CHECK(result[0].validate().valid);
    // Bounds should be unchanged
    const AABB rb = result[0].bounds();
    CHECK(rb.mins.x == Approx(0.0).margin(1e-3));
    CHECK(rb.maxs.x == Approx(64.0).margin(1e-3));
}

TEST_CASE("csgSubtract — cutter larger than subject returns empty", "[csg][subtract]") {
    const Brush subject = makeBox({0,0,0},     {64,64,64});
    const Brush cutter  = makeBox({-10,-10,-10}, {74,74,74}); // fully encloses subject

    const auto result = csgSubtract(subject, cutter);
    CHECK(result.empty());
}

TEST_CASE("csgSubtract — corner carve produces valid fragments", "[csg][subtract]") {
    // Carve a 32x32x32 corner out of a 64x64x64 box
    const Brush subject = makeBox({0,0,0},   {64,64,64});
    const Brush cutter  = makeBox({0,0,0},   {32,32,32});

    const auto result = csgSubtract(subject, cutter);

    CHECK_FALSE(result.empty());
    for (const auto& b : result) {
        const auto v = b.validate();
        INFO("Fragment error: " << (v.errors.empty() ? "none" : v.errors.front()));
        CHECK(v.valid);
    }
}

TEST_CASE("csgSubtract — corner carve fragments stay inside original AABB", "[csg][subtract]") {
    const Brush subject = makeBox({0,0,0}, {64,64,64});
    const Brush cutter  = makeBox({0,0,0}, {32,32,32});
    const auto result   = csgSubtract(subject, cutter);

    const AABB orig = subject.bounds();
    for (const auto& b : result) {
        for (const auto& v : b.vertices()) {
            // Every vertex of every fragment must be inside (or on) the original box
            CHECK(v.x >= orig.mins.x - 1e-3);
            CHECK(v.y >= orig.mins.y - 1e-3);
            CHECK(v.z >= orig.mins.z - 1e-3);
            CHECK(v.x <= orig.maxs.x + 1e-3);
            CHECK(v.y <= orig.maxs.y + 1e-3);
            CHECK(v.z <= orig.maxs.z + 1e-3);
        }
    }
}

TEST_CASE("csgSubtract — no fragment overlaps the cutter volume", "[csg][subtract]") {
    const Brush subject = makeBox({0,0,0}, {64,64,64});
    const Brush cutter  = makeBox({16,16,16}, {48,48,48}); // centred carve
    const auto result   = csgSubtract(subject, cutter);

    const AABB cutterBounds = cutter.bounds();
    for (const auto& b : result) {
        // Fragment AABB must not be fully inside the cutter
        // (they may touch its faces, but not be contained)
        const AABB fb = b.bounds();
        const bool insideCutter =
            fb.mins.x >= cutterBounds.mins.x - 1e-3 &&
            fb.mins.y >= cutterBounds.mins.y - 1e-3 &&
            fb.mins.z >= cutterBounds.mins.z - 1e-3 &&
            fb.maxs.x <= cutterBounds.maxs.x + 1e-3 &&
            fb.maxs.y <= cutterBounds.maxs.y + 1e-3 &&
            fb.maxs.z <= cutterBounds.maxs.z + 1e-3;
        CHECK_FALSE(insideCutter);
    }
}

TEST_CASE("csgSubtract — multiple cutters applied sequentially", "[csg][subtract]") {
    const Brush subject  = makeBox({0,0,0}, {64,64,64});
    const Brush cutter1  = makeBox({0,0,0},   {32,32,32}); // one corner
    const Brush cutter2  = makeBox({32,0,0},  {64,32,32}); // adjacent corner

    const std::vector<Brush> cutters = { cutter1, cutter2 };
    const auto result = csgSubtract(subject, std::span<const Brush>{cutters});

    CHECK_FALSE(result.empty());
    for (const auto& b : result) {
        CHECK(b.validate().valid);
    }
}

// ─── hollowBrush ─────────────────────────────────────────────────────────────

TEST_CASE("hollowBrush — box produces 6 wall brushes", "[csg][hollow]") {
    const Brush box   = makeBox({0,0,0}, {64,64,64});
    const auto  walls = hollowBrush(box, 8.0);
    CHECK(walls.size() == 6);
}

TEST_CASE("hollowBrush — all wall brushes are valid", "[csg][hollow]") {
    const Brush box   = makeBox({0,0,0}, {64,64,64});
    const auto  walls = hollowBrush(box, 8.0);
    for (const auto& wall : walls) {
        const auto v = wall.validate();
        INFO("Wall error: " << (v.errors.empty() ? "none" : v.errors.front()));
        CHECK(v.valid);
    }
}

TEST_CASE("hollowBrush — each wall has N+1 faces (original + inner)", "[csg][hollow]") {
    const Brush box   = makeBox({0,0,0}, {64,64,64}); // 6 faces
    const auto  walls = hollowBrush(box, 8.0);
    for (const auto& wall : walls) {
        CHECK(wall.faces.size() == 7); // 6 original + 1 inset
    }
}

TEST_CASE("hollowBrush — all wall vertices are inside original AABB", "[csg][hollow]") {
    const Brush box   = makeBox({0,0,0}, {64,64,64});
    const AABB  orig  = box.bounds();
    const auto  walls = hollowBrush(box, 8.0);

    for (const auto& wall : walls) {
        for (const auto& v : wall.vertices()) {
            CHECK(v.x >= orig.mins.x - 1e-3);
            CHECK(v.y >= orig.mins.y - 1e-3);
            CHECK(v.z >= orig.mins.z - 1e-3);
            CHECK(v.x <= orig.maxs.x + 1e-3);
            CHECK(v.y <= orig.maxs.y + 1e-3);
            CHECK(v.z <= orig.maxs.z + 1e-3);
        }
    }
}

TEST_CASE("hollowBrush — excessive thickness produces fewer/no walls", "[csg][hollow]") {
    const Brush box       = makeBox({0,0,0}, {64,64,64});
    const auto  thinWalls = hollowBrush(box, 8.0);  // valid
    const auto  thickWalls= hollowBrush(box, 200.0); // wall thickness > brush extent

    // Thin walls should exist, thick walls may collapse to degenerate (omitted)
    CHECK(thinWalls.size() == 6);
    CHECK(thickWalls.size() <= 6); // degenerate walls dropped
}

// ─── Primitives ───────────────────────────────────────────────────────────────

TEST_CASE("makeBox — validates and has correct vertex count", "[primitives]") {
    const Brush b = makeBox({-32,-32,-32}, {32,32,32});
    CHECK(b.validate().valid);
    CHECK(b.vertices().size() == 8);
}

TEST_CASE("makeWedge — validates and has 5 faces", "[primitives]") {
    const Brush b = makeWedge({0,0,0}, {64,64,64});
    CHECK(b.validate().valid);
    CHECK(b.faces.size() == 5);
}

TEST_CASE("makePrism — 6-sided validates", "[primitives]") {
    const Brush b = makePrism({0,0,0}, 32.0, 64.0, 6);
    CHECK(b.validate().valid);
    CHECK(b.faces.size() == 8); // 2 caps + 6 sides
}

TEST_CASE("makePyramid — 4-sided validates", "[primitives]") {
    const Brush b = makePyramid({0,0,0}, 32.0, 64.0, 4);
    CHECK(b.validate().valid);
    CHECK(b.faces.size() == 5); // 1 base + 4 sides
}

TEST_CASE("makePrism — minimum 3 sides enforced", "[primitives]") {
    const Brush b = makePrism({0,0,0}, 16.0, 32.0, 1); // sides clamped to 3
    CHECK(b.validate().valid);
    CHECK(b.faces.size() == 5); // 2 caps + 3 sides
}

