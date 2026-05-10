#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <forge/build.hpp>
#include <forge/geo/Primitives.hpp>

using namespace forge;
using Catch::Approx;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static scene::Scene makeTwoEntityScene() {
    scene::Scene s;
    s.name = "test";

    scene::BrushEntity floor;
    floor.name    = "floor";
    floor.brushes = { geo::makeBox({-128, -8, -128}, {128, 0, 128}) };
    s.addEntity(std::move(floor));

    scene::BrushEntity wall;
    wall.name    = "wall";
    wall.brushes = { geo::makeBox({-128, 0, 120}, {128, 128, 128}) };
    s.addEntity(std::move(wall));

    return s;
}

// ─── planarUV ────────────────────────────────────────────────────────────────

TEST_CASE("planarUV — Y-dominant face gives XZ projection", "[build][uv]") {
    // A Y-dominant normal should project onto the XZ plane:
    // u = dot(pos, {1,0,0}), v = dot(pos, {0,0,1})
    const glm::vec3 n   = { 0.f, 1.f, 0.f };
    const glm::vec3 pos = { 3.f, 10.f, 7.f };
    const glm::vec2 uv  = build::planarUV(pos, n);
    CHECK(uv.x == Approx(3.f).margin(1e-4f));
    CHECK(uv.y == Approx(7.f).margin(1e-4f));
}

TEST_CASE("planarUV — uvOffset shifts result", "[build][uv]") {
    const glm::vec3 pos = { 1.f, 0.f, 0.f };
    const glm::vec3 n   = { 0.f, 1.f, 0.f };
    const glm::vec2 uv0 = build::planarUV(pos, n, {0,0});
    const glm::vec2 uv1 = build::planarUV(pos, n, {10, 5});
    CHECK(uv1.x - uv0.x == Approx(10.f).margin(1e-4f));
    CHECK(uv1.y - uv0.y == Approx(5.f).margin(1e-4f));
}

TEST_CASE("planarUV — uvScale shrinks coordinates", "[build][uv]") {
    const glm::vec3 pos = { 64.f, 0.f, 0.f };
    const glm::vec3 n   = { 0.f, 1.f, 0.f };
    const glm::vec2 uv  = build::planarUV(pos, n, {0,0}, {2.f, 2.f});
    CHECK(uv.x == Approx(32.f).margin(1e-3f));
}

// ─── buildFaceMesh ───────────────────────────────────────────────────────────

TEST_CASE("buildFaceMesh — returns non-empty for valid face", "[build][face]") {
    const geo::Brush box  = geo::makeBox({0,0,0}, {64,64,64});
    const build::MeshData m = build::buildFaceMesh(box, 0);
    CHECK_FALSE(m.empty());
}

TEST_CASE("buildFaceMesh — box face has 4 vertices (quad = 2 triangles)", "[build][face]") {
    const geo::Brush box = geo::makeBox({0,0,0}, {64,64,64});
    const auto m = build::buildFaceMesh(box, 0);
    CHECK(m.vertexCount()   == 4);
    CHECK(m.triangleCount() == 2);
    CHECK(m.indices.size()  == 6);
}

TEST_CASE("buildFaceMesh — materialId matches face", "[build][face]") {
    geo::Brush box = geo::makeBox({0,0,0}, {64,64,64});
    box.faces[0].materialId = "brick/wall01";
    const auto m = build::buildFaceMesh(box, 0);
    CHECK(m.materialId == "brick/wall01");
}

TEST_CASE("buildFaceMesh — all normals equal face plane normal", "[build][face]") {
    const geo::Brush box = geo::makeBox({0,0,0}, {64,64,64});
    const auto m = build::buildFaceMesh(box, 0);
    const glm::vec3 expected = glm::vec3(box.faces[0].plane.normal);
    for (const auto& v : m.vertices) {
        CHECK(v.normal.x == Approx(expected.x).margin(1e-5f));
        CHECK(v.normal.y == Approx(expected.y).margin(1e-5f));
        CHECK(v.normal.z == Approx(expected.z).margin(1e-5f));
    }
}

TEST_CASE("buildFaceMesh — bounds contains all vertices", "[build][face]") {
    const geo::Brush box = geo::makeBox({0,0,0}, {64,64,64});
    const auto m = build::buildFaceMesh(box, 0);
    for (const auto& v : m.vertices) {
        const glm::dvec3 p(v.position);
        CHECK(m.bounds.contains(p));
    }
}

// ─── buildBrushMesh ──────────────────────────────────────────────────────────

TEST_CASE("buildBrushMesh — box yields 1 submesh (all faces same material)", "[build][brush]") {
    const auto box  = geo::makeBox({0,0,0}, {64,64,64});
    const auto mesh = build::buildBrushMesh(box);
    // All 6 faces share "default" material → 1 submesh
    CHECK(mesh.submeshes.size() == 1);
}

TEST_CASE("buildBrushMesh — box submesh has correct vertex count", "[build][brush]") {
    const auto box  = geo::makeBox({0,0,0}, {64,64,64});
    const auto mesh = build::buildBrushMesh(box);
    REQUIRE_FALSE(mesh.submeshes.empty());
    // 6 faces × 4 vertices each = 24 vertices total
    CHECK(mesh.submeshes[0].vertexCount() == 24);
}

TEST_CASE("buildBrushMesh — box has 12 triangles (6 faces × 2)", "[build][brush]") {
    const auto box  = geo::makeBox({0,0,0}, {64,64,64});
    const auto mesh = build::buildBrushMesh(box);
    CHECK(mesh.triangleCount() == 12);
}

TEST_CASE("buildBrushMesh — multi-material box splits into multiple submeshes", "[build][brush]") {
    auto box = geo::makeBox({0,0,0}, {64,64,64});
    box.faces[0].materialId = "metal/plate";
    box.faces[1].materialId = "stone/brick";
    box.invalidate();
    const auto mesh = build::buildBrushMesh(box);
    CHECK(mesh.submeshes.size() == 3); // metal, stone, default
}

TEST_CASE("buildBrushMesh — bounds covers all geometry", "[build][brush]") {
    const auto box  = geo::makeBox({-32,-32,-32}, {32,32,32});
    const auto mesh = build::buildBrushMesh(box);
    CHECK(mesh.bounds.mins.x <= -31.9);
    CHECK(mesh.bounds.maxs.x >=  31.9);
}

// ─── buildEntityMesh ─────────────────────────────────────────────────────────

TEST_CASE("buildEntityMesh — single brush entity yields non-empty mesh", "[build][entity]") {
    scene::BrushEntity be;
    be.name    = "box";
    be.brushes = { geo::makeBox({0,0,0}, {64,64,64}) };

    const auto mesh = build::buildEntityMesh(be);
    CHECK_FALSE(mesh.empty());
}

TEST_CASE("buildEntityMesh — applyTransform moves vertices to world space", "[build][entity]") {
    scene::BrushEntity be;
    be.name      = "shifted";
    be.brushes   = { geo::makeBox({0,0,0}, {64,64,64}) };
    be.transform = scene::Transform::fromTranslation({100, 0, 0});

    const auto localMesh = build::buildEntityMesh(be, /*applyTransform=*/false);
    const auto worldMesh = build::buildEntityMesh(be, /*applyTransform=*/true);

    REQUIRE_FALSE(localMesh.empty());
    REQUIRE_FALSE(worldMesh.empty());

    // World mesh should be offset by 100 on X
    CHECK(worldMesh.bounds.mins.x == Approx(100.0).margin(1e-3));
    CHECK(localMesh.bounds.mins.x == Approx(0.0).margin(1e-3));
}

TEST_CASE("buildEntityMesh — invisible entities not included in scene build", "[build][scene]") {
    scene::Scene s;

    scene::BrushEntity visible;
    visible.name    = "visible";
    visible.visible = true;
    visible.brushes = { geo::makeBox({0,0,0}, {64,64,64}) };
    s.addEntity(std::move(visible));

    scene::BrushEntity hidden;
    hidden.name    = "hidden";
    hidden.visible = false;
    hidden.brushes = { geo::makeBox({0,0,0}, {64,64,64}) };
    s.addEntity(std::move(hidden));

    const auto meshes = build::buildSceneMeshes(s);
    // Only 1 entity (the visible one)
    CHECK(meshes.size() == 1);
}

// ─── buildSceneMeshes ────────────────────────────────────────────────────────

TEST_CASE("buildSceneMeshes — returns one entry per visible BrushEntity", "[build][scene]") {
    const auto s      = makeTwoEntityScene();
    const auto meshes = build::buildSceneMeshes(s);
    CHECK(meshes.size() == 2);
}

TEST_CASE("buildSceneMeshes — all returned meshes are non-empty", "[build][scene]") {
    const auto meshes = build::buildSceneMeshes(makeTwoEntityScene());
    for (const auto& [id, mesh] : meshes) {
        CHECK_FALSE(mesh.empty());
    }
}

TEST_CASE("buildSceneMeshes — point entities are excluded", "[build][scene]") {
    scene::Scene s;

    scene::BrushEntity be;
    be.brushes = { geo::makeBox({0,0,0},{64,64,64}) };
    s.addEntity(std::move(be));

    scene::PointEntity pe;
    pe.classname = "light";
    s.addEntity(std::move(pe));

    const auto meshes = build::buildSceneMeshes(s);
    CHECK(meshes.size() == 1); // only the brush entity
}

// ─── Vertex layout ────────────────────────────────────────────────────────────

TEST_CASE("Vertex size is exactly 32 bytes", "[build][vertex]") {
    CHECK(sizeof(build::Vertex) == 32);
}

TEST_CASE("Vertex position offset is 0", "[build][vertex]") {
    CHECK(offsetof(build::Vertex, position) == 0);
}

TEST_CASE("Vertex normal offset is 12", "[build][vertex]") {
    CHECK(offsetof(build::Vertex, normal) == 12);
}

TEST_CASE("Vertex uv offset is 24", "[build][vertex]") {
    CHECK(offsetof(build::Vertex, uv) == 24);
}
