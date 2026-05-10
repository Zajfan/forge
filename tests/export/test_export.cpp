#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <forge/export.hpp>
#include <forge/geo/Primitives.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace forge;
using Catch::Approx;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static scene::Scene makeSingleBoxScene() {
    scene::Scene s;
    s.name = "test";

    scene::BrushEntity be;
    be.name = "test_box";
    be.brushes = { geo::makeBox({0,0,0}, {64,64,64}) };
    s.addEntity(std::move(be));
    return s;
}

static scene::Scene makeFullScene() {
    scene::Scene s;
    s.name = "full";

    // Two brush entities
    {
        scene::BrushEntity be;
        be.name    = "floor";
        be.brushes = { geo::makeBox({-128,  -8, -128}, {128, 0, 128}) };
        s.addEntity(std::move(be));
    }
    {
        scene::BrushEntity be;
        be.name    = "wall";
        be.brushes = { geo::makeBox({-128, 0, 120}, {128, 128, 128}) };
        s.addEntity(std::move(be));
    }

    // A point entity
    {
        scene::PointEntity pe;
        pe.name      = "spawn";
        pe.classname = "info_player_start";
        pe.transform = scene::Transform::fromTranslation({0, 32, 0});
        pe.set<float>("angle", 180.f);
        s.addEntity(std::move(pe));
    }

    // A light
    {
        scene::PointEntity light;
        light.name      = "main_light";
        light.classname = "light";
        light.transform = scene::Transform::fromTranslation({0, 100, 0});
        light.set<float>("light", 300.f);
        s.addEntity(std::move(light));
    }

    return s;
}

static std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p);
    return { std::istreambuf_iterator<char>(f), {} };
}

// ─── OBJ exporter ────────────────────────────────────────────────────────────

TEST_CASE("exportOBJ — returns true for valid scene", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_obj";
    const auto scene = makeSingleBoxScene();
    CHECK(export_::exportOBJ(scene, tmp) == true);
    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

TEST_CASE("exportOBJ — .obj file is created", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_obj_exists";
    const auto scene = makeSingleBoxScene();
    export_::exportOBJ(scene, tmp);
    CHECK(std::filesystem::exists(tmp.string() + ".obj"));
    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

TEST_CASE("exportOBJ — .mtl file is created", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_mtl_exists";
    const auto scene = makeSingleBoxScene();
    export_::exportOBJ(scene, tmp);
    CHECK(std::filesystem::exists(tmp.string() + ".mtl"));
    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

TEST_CASE("exportOBJ — obj contains vertex lines", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_obj_verts";
    const auto scene = makeSingleBoxScene();
    export_::exportOBJ(scene, tmp);

    const std::string content = readFile(tmp.string() + ".obj");
    CHECK(content.find("v ") != std::string::npos);
    CHECK(content.find("vn ") != std::string::npos);
    CHECK(content.find("f ") != std::string::npos);

    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

TEST_CASE("exportOBJ — obj contains object name", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_obj_name";
    const auto scene = makeSingleBoxScene();
    export_::exportOBJ(scene, tmp);

    const std::string content = readFile(tmp.string() + ".obj");
    CHECK(content.find("o test_box") != std::string::npos);

    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

TEST_CASE("exportOBJ — obj contains mtllib reference", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_obj_mtllib";
    const auto scene = makeSingleBoxScene();
    export_::exportOBJ(scene, tmp);

    const std::string content = readFile(tmp.string() + ".obj");
    CHECK(content.find("mtllib") != std::string::npos);

    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

TEST_CASE("exportOBJ — fails gracefully for non-writable path", "[export][obj]") {
    const auto scene = makeSingleBoxScene();
    const bool ok = export_::exportOBJ(scene, "/proc/no/such/dir/file");
    CHECK_FALSE(ok);
}

TEST_CASE("exportOBJ — multi-entity scene exports all objects", "[export][obj]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_obj_multi";
    const auto scene = makeFullScene();
    export_::exportOBJ(scene, tmp);

    const std::string content = readFile(tmp.string() + ".obj");
    CHECK(content.find("o floor") != std::string::npos);
    CHECK(content.find("o wall")  != std::string::npos);

    std::filesystem::remove(tmp.string() + ".obj");
    std::filesystem::remove(tmp.string() + ".mtl");
}

// ─── MAP exporter ────────────────────────────────────────────────────────────

TEST_CASE("exportMAP — returns true for valid scene", "[export][map]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test.map";
    const auto scene = makeSingleBoxScene();
    CHECK(export_::exportMAP(scene, tmp) == true);
    std::filesystem::remove(tmp);
}

TEST_CASE("exportMAP — .map file is created", "[export][map]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_exists.map";
    const auto scene = makeSingleBoxScene();
    export_::exportMAP(scene, tmp);
    CHECK(std::filesystem::exists(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("exportMAP — contains worldspawn entity", "[export][map]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_ws.map";
    const auto scene = makeSingleBoxScene();
    export_::exportMAP(scene, tmp);

    const std::string content = readFile(tmp);
    CHECK(content.find("worldspawn") != std::string::npos);
    std::filesystem::remove(tmp);
}

TEST_CASE("exportMAP — contains brush face lines (three-point form)", "[export][map]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_faces.map";
    const auto scene = makeSingleBoxScene();
    export_::exportMAP(scene, tmp);

    const std::string content = readFile(tmp);
    // Every face line starts with '( '
    CHECK(content.find("( ") != std::string::npos);
    std::filesystem::remove(tmp);
}

TEST_CASE("exportMAP — point entities have classname and origin", "[export][map]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_pt.map";
    const auto scene = makeFullScene();
    export_::exportMAP(scene, tmp);

    const std::string content = readFile(tmp);
    CHECK(content.find("info_player_start") != std::string::npos);
    CHECK(content.find("\"origin\"")        != std::string::npos);
    CHECK(content.find("light")             != std::string::npos);
    std::filesystem::remove(tmp);
}

TEST_CASE("exportMAP — Valve 220 format contains UV vectors", "[export][map]") {
    const auto tmp   = std::filesystem::temp_directory_path() / "forge_test_v220.map";
    const auto scene = makeSingleBoxScene();

    export_::MAPExportOptions opts;
    opts.mapVersion = 220;
    export_::exportMAP(scene, tmp, opts);

    const std::string content = readFile(tmp);
    // Valve 220 uses [ ] brackets for UV vectors
    CHECK(content.find("[ ") != std::string::npos);
    std::filesystem::remove(tmp);
}

TEST_CASE("exportMAP — fails gracefully for non-writable path", "[export][map]") {
    const auto scene = makeSingleBoxScene();
    const bool ok = export_::exportMAP(scene, "/proc/no/such/dir/file.map");
    CHECK_FALSE(ok);
}
