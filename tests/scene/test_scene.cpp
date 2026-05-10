#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <forge/scene.hpp>
#include <forge/geo/Primitives.hpp>

using namespace forge::scene;
using Catch::Approx;

// ─── Transform ────────────────────────────────────────────────────────────────

TEST_CASE("Transform::identity produces identity matrix", "[transform]") {
    const Transform tf = Transform::identity();
    const glm::dmat4 m = tf.matrix();
    CHECK(m == glm::identity<glm::dmat4>());
}

TEST_CASE("Transform::transformPoint — translation only", "[transform]") {
    Transform tf;
    tf.translation = { 10.0, 20.0, 30.0 };
    const glm::dvec3 result = tf.transformPoint({ 0, 0, 0 });
    CHECK(result.x == Approx(10.0));
    CHECK(result.y == Approx(20.0));
    CHECK(result.z == Approx(30.0));
}

TEST_CASE("Transform::transformPoint — scale only", "[transform]") {
    Transform tf;
    tf.scale = { 2.0, 3.0, 4.0 };
    const glm::dvec3 result = tf.transformPoint({ 1.0, 1.0, 1.0 });
    CHECK(result.x == Approx(2.0));
    CHECK(result.y == Approx(3.0));
    CHECK(result.z == Approx(4.0));
}

TEST_CASE("Transform::transformVector — not affected by translation", "[transform]") {
    Transform tf;
    tf.translation = { 100, 200, 300 };
    const glm::dvec3 result = tf.transformVector({ 1, 0, 0 });
    CHECK(result.x == Approx(1.0).margin(1e-9));
    CHECK(result.y == Approx(0.0).margin(1e-9));
    CHECK(result.z == Approx(0.0).margin(1e-9));
}

TEST_CASE("Transform::transformNormal — unit-length result", "[transform]") {
    Transform tf;
    tf.scale = { 2.0, 5.0, 0.5 }; // non-uniform scale
    const glm::dvec3 n = tf.transformNormal({ 0, 1, 0 });
    CHECK(glm::length(n) == Approx(1.0).margin(1e-6));
}

TEST_CASE("Transform::fromTranslation convenience constructor", "[transform]") {
    const auto tf = Transform::fromTranslation({ 5, 10, 15 });
    const glm::dvec3 p = tf.transformPoint({ 0, 0, 0 });
    CHECK(p.x == Approx(5.0));
    CHECK(p.y == Approx(10.0));
    CHECK(p.z == Approx(15.0));
}

TEST_CASE("Transform::isIdentity returns true for default", "[transform]") {
    const Transform tf;
    CHECK(tf.isIdentity());
}

TEST_CASE("Transform::isIdentity returns false after modification", "[transform]") {
    Transform tf;
    tf.translation.x = 1.0;
    CHECK_FALSE(tf.isIdentity());
}

// ─── Entity types ─────────────────────────────────────────────────────────────

TEST_CASE("PointEntity::property returns default for missing key", "[entity]") {
    PointEntity pe;
    const float val = pe.property<float>("light", 100.f);
    CHECK(val == Approx(100.f));
}

TEST_CASE("PointEntity::set and property round-trip", "[entity]") {
    PointEntity pe;
    pe.set<float>("light", 250.f);
    pe.set<std::string>("target", "light_relay_01");
    pe.set<bool>("shadow", true);

    CHECK(pe.property<float>("light") == Approx(250.f));
    CHECK(pe.property<std::string>("target") == "light_relay_01");
    CHECK(pe.property<bool>("shadow") == true);
}

TEST_CASE("PointEntity::property returns default for wrong type", "[entity]") {
    PointEntity pe;
    pe.set<float>("light", 100.f);
    // Requesting int when float was stored → returns default
    const int val = pe.property<int>("light", 42);
    CHECK(val == 42);
}

TEST_CASE("BrushEntity::faceCount totals across all brushes", "[entity]") {
    BrushEntity be;
    be.brushes = {
        forge::geo::makeBox({0,0,0}, {64,64,64}),   // 6 faces
        forge::geo::makeBox({0,0,0}, {32,32,32}),   // 6 faces
    };
    CHECK(be.faceCount() == 12);
}

TEST_CASE("BrushEntity::worldBounds applies transform", "[entity]") {
    BrushEntity be;
    be.brushes = { forge::geo::makeBox({0,0,0}, {64,64,64}) };
    be.transform = Transform::fromTranslation({ 100, 0, 0 });

    const forge::geo::AABB wb = be.worldBounds();
    CHECK(wb.mins.x == Approx(100.0).margin(1e-3));
    CHECK(wb.maxs.x == Approx(164.0).margin(1e-3));
}

// ─── Scene ────────────────────────────────────────────────────────────────────

TEST_CASE("Scene::addEntity assigns unique sequential IDs", "[scene]") {
    Scene scene;
    const EntityId id1 = scene.addEntity(BrushEntity{});
    const EntityId id2 = scene.addEntity(PointEntity{});
    const EntityId id3 = scene.addEntity(MeshEntity{});

    CHECK(id1 != id2);
    CHECK(id2 != id3);
    CHECK(id1 != id3);
}

TEST_CASE("Scene::getEntity returns correct entity", "[scene]") {
    Scene scene;
    BrushEntity be;
    be.name = "my_brush";
    const EntityId id = scene.addEntity(be);

    Entity* e = scene.getEntity(id);
    REQUIRE(e != nullptr);
    REQUIRE(std::holds_alternative<BrushEntity>(*e));
    CHECK(std::get<BrushEntity>(*e).name == "my_brush");
}

TEST_CASE("Scene::getEntity returns nullptr for unknown ID", "[scene]") {
    Scene scene;
    CHECK(scene.getEntity(9999) == nullptr);
}

TEST_CASE("Scene::removeEntity removes the entity", "[scene]") {
    Scene scene;
    const EntityId id = scene.addEntity(BrushEntity{});
    scene.removeEntity(id);
    CHECK(scene.getEntity(id) == nullptr);
    CHECK(scene.entityCount() == 0);
}

TEST_CASE("Scene::entitiesOfType returns only matching type", "[scene]") {
    Scene scene;
    scene.addEntity(BrushEntity{});
    scene.addEntity(BrushEntity{});
    scene.addEntity(PointEntity{});
    scene.addEntity(MeshEntity{});

    const auto brushes = scene.entitiesOfType<BrushEntity>();
    const auto points  = scene.entitiesOfType<PointEntity>();
    const auto meshes  = scene.entitiesOfType<MeshEntity>();

    CHECK(brushes.size() == 2);
    CHECK(points.size()  == 1);
    CHECK(meshes.size()  == 1);
}

TEST_CASE("Scene::findByClassname finds the correct entity", "[scene]") {
    Scene scene;
    PointEntity spawn;
    spawn.classname = "info_player_start";
    spawn.name      = "spawn1";
    scene.addEntity(spawn);
    scene.addEntity(PointEntity{}); // different classname

    auto [id, ptr] = scene.findByClassname("info_player_start");
    REQUIRE(ptr != nullptr);
    CHECK(ptr->name == "spawn1");
}

TEST_CASE("Scene::findByClassname returns null for missing classname", "[scene]") {
    Scene scene;
    auto [id, ptr] = scene.findByClassname("func_door");
    CHECK(id  == kInvalidEntityId);
    CHECK(ptr == nullptr);
}

TEST_CASE("Scene::stats returns correct counts", "[scene]") {
    Scene scene;
    BrushEntity be;
    be.brushes = { forge::geo::makeBox({0,0,0},{64,64,64}) }; // 6 faces
    scene.addEntity(be);
    scene.addEntity(PointEntity{});
    scene.addEntity(MeshEntity{});

    const auto s = scene.stats();
    CHECK(s.brushEntityCount == 1);
    CHECK(s.pointEntityCount == 1);
    CHECK(s.meshEntityCount  == 1);
    CHECK(s.totalBrushCount  == 1);
    CHECK(s.totalFaceCount   == 6);
}

TEST_CASE("Scene::clear resets all state", "[scene]") {
    Scene scene;
    scene.addEntity(BrushEntity{});
    scene.addEntity(PointEntity{});
    scene.clear();
    CHECK(scene.entityCount() == 0);
    // IDs should restart from 1
    const EntityId id = scene.addEntity(BrushEntity{});
    CHECK(id == 1);
}

TEST_CASE("entityName free function works for all entity types", "[entity]") {
    BrushEntity be; be.name = "my_brush";
    PointEntity pe; pe.name = "my_point";
    MeshEntity  me; me.name = "my_mesh";

    CHECK(entityName(Entity{be}) == "my_brush");
    CHECK(entityName(Entity{pe}) == "my_point");
    CHECK(entityName(Entity{me}) == "my_mesh");
}
