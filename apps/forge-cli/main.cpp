#include <forge/geo.hpp>
#include <forge/scene.hpp>
#include <forge/export.hpp>

#include <filesystem>
#include <format>
#include <iostream>
#include <string>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void printSeparator(char c = '─', int w = 60) {
    std::cout << std::string(w, c) << '\n';
}

static void printStats(const forge::scene::Scene& scene) {
    const auto s = scene.stats();
    printSeparator();
    std::cout << std::format("  Scene:         {}\n", scene.name);
    std::cout << std::format("  Entities:      {} brush  {} point  {} mesh\n",
        s.brushEntityCount, s.pointEntityCount, s.meshEntityCount);
    std::cout << std::format("  Total brushes: {}\n", s.totalBrushCount);
    std::cout << std::format("  Total faces:   {}\n", s.totalFaceCount);

    const auto bounds = scene.worldBounds();
    if (bounds.isValid()) {
        const auto ext = bounds.extents();
        std::cout << std::format("  World AABB:    {:.0f} × {:.0f} × {:.0f} units\n",
            ext.x, ext.y, ext.z);
    }
    printSeparator();
}

// ─── Scene builder ────────────────────────────────────────────────────────────

/// Build a small test room scene demonstrating:
///   - Primitive construction (makeBox)
///   - CSG subtraction (doorway carve)
///   - Scene entity placement (BrushEntity, PointEntity)
///   - Multiple brushes per entity
forge::scene::Scene buildTestScene() {
    using namespace forge;
    namespace fgeo  = forge::geo;
    namespace fscn  = forge::scene;

    fscn::Scene scene;
    scene.name   = "forge_test_room";
    scene.author = "forge-cli";

    std::cout << "\nBuilding scene...\n";

    // ── Floor ─────────────────────────────────────────────────────────────────
    {
        auto floor = fgeo::makeBox({-256, -16, -256}, {256, 0, 256});
        floor.id = "floor";
        floor.faces[2].materialId = "stone/floor01"; // +Y face = top

        fscn::BrushEntity e;
        e.name = "floor";
        e.brushes = { floor };
        scene.addEntity(std::move(e));
        std::cout << "  + floor\n";
    }

    // ── Ceiling ───────────────────────────────────────────────────────────────
    {
        auto ceiling = fgeo::makeBox({-256, 256, -256}, {256, 272, 256});
        ceiling.id = "ceiling";
        fscn::BrushEntity e;
        e.name = "ceiling";
        e.brushes = { ceiling };
        scene.addEntity(std::move(e));
        std::cout << "  + ceiling\n";
    }

    // ── North wall with doorway ───────────────────────────────────────────────
    {
        auto northWall = fgeo::makeBox({-256, 0, 240}, {256, 256, 256});
        northWall.id   = "north_wall";

        // Carve a 64-wide × 192-tall doorway in the centre
        const auto doorwayCutter = fgeo::makeBox({-32, 0, 239}, {32, 192, 257});
        auto fragments = fgeo::csgSubtract(northWall, doorwayCutter);

        std::cout << std::format("  + north wall  ({} fragment(s) after doorway carve)\n",
            fragments.size());

        fscn::BrushEntity e;
        e.name    = "north_wall";
        e.brushes = std::move(fragments);
        scene.addEntity(std::move(e));
    }

    // ── South wall (solid) ────────────────────────────────────────────────────
    {
        auto southWall = fgeo::makeBox({-256, 0, -256}, {256, 256, -240});
        southWall.id   = "south_wall";
        fscn::BrushEntity e;
        e.name = "south_wall";
        e.brushes = { southWall };
        scene.addEntity(std::move(e));
        std::cout << "  + south wall\n";
    }

    // ── East and West walls ───────────────────────────────────────────────────
    {
        auto eastWall = fgeo::makeBox({240, 0, -256}, {256, 256, 256});
        eastWall.id = "east_wall";
        fscn::BrushEntity e;
        e.name = "east_wall";
        e.brushes = { eastWall };
        scene.addEntity(std::move(e));
        std::cout << "  + east wall\n";
    }
    {
        auto westWall = fgeo::makeBox({-256, 0, -256}, {-240, 256, 256});
        westWall.id = "west_wall";
        fscn::BrushEntity e;
        e.name = "west_wall";
        e.brushes = { westWall };
        scene.addEntity(std::move(e));
        std::cout << "  + west wall\n";
    }

    // ── A pillar (octagonal prism) ────────────────────────────────────────────
    {
        auto pillar = fgeo::makePrism({64, 0, 64}, 24.0, 256.0, 8);
        pillar.id = "pillar_a";
        fscn::BrushEntity e;
        e.name = "pillar_a";
        e.brushes = { pillar };
        scene.addEntity(std::move(e));
        std::cout << "  + pillar (8-sided prism)\n";
    }

    // ── A ramp / wedge ────────────────────────────────────────────────────────
    {
        auto ramp = fgeo::makeWedge({-200, 0, -200}, {-100, 64, -100});
        ramp.id = "ramp_a";
        fscn::BrushEntity e;
        e.name = "ramp_a";
        e.brushes = { ramp };
        scene.addEntity(std::move(e));
        std::cout << "  + ramp (wedge)\n";
    }

    // ── Player spawn point ────────────────────────────────────────────────────
    {
        fscn::PointEntity spawn;
        spawn.name      = "player_start_1";
        spawn.classname = "info_player_start";
        spawn.transform = fscn::Transform::fromTranslation({0, 32, 0});
        spawn.set<float>("angle", 90.f);
        scene.addEntity(std::move(spawn));
        std::cout << "  + player spawn\n";
    }

    // ── Lights ────────────────────────────────────────────────────────────────
    {
        fscn::PointEntity light;
        light.name      = "light_centre";
        light.classname = "light";
        light.transform = fscn::Transform::fromTranslation({0, 220, 0});
        light.set<float>("light",  300.f);
        light.set<glm::vec3>("_color", {1.f, 0.95f, 0.8f});
        scene.addEntity(std::move(light));

        fscn::PointEntity light2;
        light2.name      = "light_doorway";
        light2.classname = "light";
        light2.transform = fscn::Transform::fromTranslation({0, 160, 248});
        light2.set<float>("light", 200.f);
        scene.addEntity(std::move(light2));
        std::cout << "  + 2 lights\n";
    }

    return scene;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const std::filesystem::path outputDir =
        (argc > 1) ? argv[1] : std::filesystem::current_path() / "forge_output";

    printSeparator('═');
    std::cout << "  FORGE CLI — geometry kernel demo\n";
    printSeparator('═');

    // ── Build scene ──────────────────────────────────────────────────────────
    auto scene = buildTestScene();
    printStats(scene);

    // ── Create output directory ───────────────────────────────────────────────
    std::filesystem::create_directories(outputDir);
    std::cout << "Output directory: " << outputDir << "\n\n";

    // ── Validate all brushes ──────────────────────────────────────────────────
    std::cout << "Validating brushes...\n";
    int invalidCount = 0;
    for (const auto& [id, entity] : scene.entities) {
        if (const auto* be = std::get_if<forge::scene::BrushEntity>(&entity)) {
            for (const auto& brush : be->brushes) {
                const auto v = brush.validate();
                if (!v.valid) {
                    ++invalidCount;
                    std::cout << std::format("  [WARN] brush '{}' in '{}': {}\n",
                        brush.id, be->name, v.errors.front());
                }
            }
        }
    }
    if (invalidCount == 0)
        std::cout << "  All brushes valid.\n";
    std::cout << "\n";

    // ── Export OBJ ───────────────────────────────────────────────────────────
    {
        const auto objPath = outputDir / "forge_test_room";
        forge::export_::OBJExportOptions opts;
        opts.applyTransforms = true;
        opts.scale           = 1.0;

        std::cout << "Exporting OBJ...\n";
        if (forge::export_::exportOBJ(scene, objPath, opts)) {
            std::cout << std::format("  -> {}.obj\n", objPath.string());
            std::cout << std::format("  -> {}.mtl\n", objPath.string());
        } else {
            std::cerr << "  [ERROR] OBJ export failed.\n";
        }
    }

    // ── Export MAP ────────────────────────────────────────────────────────────
    {
        const auto mapPath = outputDir / "forge_test_room.map";
        forge::export_::MAPExportOptions opts;
        opts.mapVersion    = 220; // Valve 220 — TrenchBroom compatible
        opts.useWorldspawn = true;

        std::cout << "\nExporting Quake MAP (Valve 220)...\n";
        if (forge::export_::exportMAP(scene, mapPath, opts)) {
            std::cout << std::format("  -> {}\n", mapPath.string());
        } else {
            std::cerr << "  [ERROR] MAP export failed.\n";
        }
    }

    printSeparator('═');
    std::cout << "Done. Open the .obj in Blender or the .map in TrenchBroom.\n";
    printSeparator('═');
    std::cout << '\n';

    return 0;
}
