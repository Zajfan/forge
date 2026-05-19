#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <nlohmann/json.hpp>

#include <forge/serial.hpp>
#include <forge/geo/Primitives.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using Catch::Approx;

TEST_CASE("Project serializer round-trip persists materials, face refs, and editor metadata", "[serial][project]") {
    using namespace forge;

    scene::Scene scene;
    scene.name = "serial_project_rt";

    scene::BrushEntity be;
    be.name = "test_brush";
    be.brushes = { geo::makeBox({0, 0, 0}, {64, 64, 64}) };
    for (auto& face : be.brushes[0].faces) {
        face.materialId = "MatA";
    }
    const auto id = scene.addEntity(std::move(be));

    gfx::MaterialLibrary materials;
    auto matA = std::make_shared<gfx::Material>("MatA");
    matA->roughness = 0.35f;
    materials.addMaterial(matA);
    auto matB = std::make_shared<gfx::Material>("MatB");
    matB->metallic = 0.8f;
    materials.addMaterial(matB);

    serial::EditorMetadata editor;
    editor.entityLayers[id] = "Architecture";
    editor.entityGroups[id] = "Shell";
    editor.layerVisibility["Architecture"] = true;
    editor.layerLocked["Architecture"] = false;
    editor.layerTint["Architecture"] = {0.7f, 0.8f, 0.9f};
    editor.soloLayer = "";
    editor.groupFilter = "Shell";

    const auto path = std::filesystem::temp_directory_path() / "forge_project_roundtrip_test.forge";

    const auto saveErr = serial::saveProject(scene, materials, editor, path);
    REQUIRE(saveErr.empty());

    const auto loaded = serial::loadProject(path);
    REQUIRE(loaded.has_value());

    const auto* loadedEntity = loaded->scene.getEntity(id);
    REQUIRE(loadedEntity != nullptr);
    const auto* loadedBrush = std::get_if<scene::BrushEntity>(loadedEntity);
    REQUIRE(loadedBrush != nullptr);
    REQUIRE_FALSE(loadedBrush->brushes.empty());
    REQUIRE_FALSE(loadedBrush->brushes[0].faces.empty());
    CHECK(loadedBrush->brushes[0].faces[0].materialId == "MatA");

    const auto& mats = loaded->materials.materials();
    REQUIRE(mats.contains("MatA"));
    REQUIRE(mats.contains("MatB"));
    REQUIRE(mats.at("MatA") != nullptr);
    REQUIRE(mats.at("MatB") != nullptr);
    CHECK(mats.at("MatA")->roughness == Approx(0.35f));
    CHECK(mats.at("MatB")->metallic == Approx(0.8f));

    REQUIRE(loaded->editor.entityLayers.contains(id));
    CHECK(loaded->editor.entityLayers.at(id) == "Architecture");
    REQUIRE(loaded->editor.entityGroups.contains(id));
    CHECK(loaded->editor.entityGroups.at(id) == "Shell");
    REQUIRE(loaded->editor.layerTint.contains("Architecture"));
    CHECK(loaded->editor.layerTint.at("Architecture").x == Approx(0.7f));
    CHECK(loaded->editor.layerTint.at("Architecture").y == Approx(0.8f));
    CHECK(loaded->editor.layerTint.at("Architecture").z == Approx(0.9f));
    CHECK(loaded->editor.groupFilter == "Shell");

    std::filesystem::remove(path);
}

TEST_CASE("Legacy scene-only project loads with sensible defaults", "[serial][project]") {
        const auto path = std::filesystem::temp_directory_path() / "forge_legacy_scene_only.forge";

        const std::string legacyJson = R"json(
{
    "forge_version": "0.1.0",
    "name": "legacy_scene",
    "author": "",
    "entities": {
        "1": {
            "type": "brush",
            "name": "legacy_box",
            "classname": "",
            "solid": true,
            "visible": true,
            "layer": "default",
            "transform": {
                "translation": [0.0, 0.0, 0.0],
                "rotation": [0.0, 0.0, 0.0, 1.0],
                "scale": [1.0, 1.0, 1.0]
            },
            "properties": {},
            "brushes": [
                {
                    "id": "legacy_brush",
                    "faces": [
                        {
                            "plane": { "normal": [1.0, 0.0, 0.0], "distance": 0.0 },
                            "materialId": "MatLegacy",
                            "uvOffset": [0.0, 0.0],
                            "uvScale": [1.0, 1.0],
                            "uvRotation": 0.0
                        }
                    ]
                }
            ]
        }
    },
    "lighting": {
        "ambientColor": [0.05, 0.05, 0.05],
        "sunDirection": [-0.5, -1.0, -0.5],
        "sunColor": [1.0, 0.95, 0.8],
        "sunIntensity": 1.0
    },
    "fog": {
        "density": 0.0,
        "color": [0.5, 0.5, 0.5]
    }
}
)json";

        {
                std::ofstream out(path);
                REQUIRE(out.is_open());
                out << legacyJson;
                REQUIRE(out.good());
        }

        const auto loaded = forge::serial::loadProject(path);
        if (!loaded) {
            FAIL(loaded.error());
        }

        CHECK(loaded->scene.name == "legacy_scene");
        CHECK(loaded->materials.size() == 0);
        CHECK(loaded->editor.entityLayers.empty());
        CHECK(loaded->editor.entityGroups.empty());
        CHECK(loaded->editor.layerVisibility.empty());
        CHECK(loaded->editor.layerLocked.empty());
        CHECK(loaded->editor.layerTint.empty());
        CHECK(loaded->editor.soloLayer.empty());
        CHECK(loaded->editor.groupFilter.empty());
        CHECK_FALSE(loaded->schemaNote.empty());
        CHECK(loaded->schemaNote.find("legacy schema version") != std::string::npos);

        const auto* entity = loaded->scene.getEntity(1);
        REQUIRE(entity != nullptr);
        const auto* brush = std::get_if<forge::scene::BrushEntity>(entity);
        REQUIRE(brush != nullptr);
        REQUIRE_FALSE(brush->brushes.empty());
        REQUIRE_FALSE(brush->brushes[0].faces.empty());
        CHECK(brush->brushes[0].faces[0].materialId == "MatLegacy");

        std::filesystem::remove(path);
}

TEST_CASE("Forward-compatible project ignores unknown future fields", "[serial][project]") {
        using namespace forge;

        scene::Scene scene;
        scene.name = "future_scene";
        scene.author = "future-author";

        scene::BrushEntity brushEntity;
        brushEntity.name = "future_brush";
        brushEntity.brushes = { geo::makeBox({0, 0, 0}, {64, 64, 64}) };
        for (auto& face : brushEntity.brushes[0].faces) {
            face.materialId = "MatFuture";
        }
        const auto entityId = scene.addEntity(std::move(brushEntity));

        gfx::MaterialLibrary materials;
        auto matFuture = std::make_shared<gfx::Material>("MatFuture");
        matFuture->roughness = 0.2f;
        materials.addMaterial(matFuture);

        serial::EditorMetadata editor;
        editor.entityLayers[entityId] = "FutureLayer";
        editor.entityGroups[entityId] = "FutureGroup";
        editor.layerVisibility["FutureLayer"] = true;
        editor.layerLocked["FutureLayer"] = false;
        editor.layerTint["FutureLayer"] = {0.9f, 0.1f, 0.4f};

        const auto path = std::filesystem::temp_directory_path() / "forge_forward_compat_unknown_fields.forge";
        const auto saveErr = serial::saveProject(scene, materials, editor, path);
        REQUIRE(saveErr.empty());

        auto root = nlohmann::json::parse(std::ifstream(path));
        root["forge_version"] = "0.3.0";
        root["futureRootField"] = 12345;
        root["unknownTopLevel"] = {
            {"enabled", true},
            {"nested", {1, 2, 3}}
        };
        root["entities"][std::to_string(entityId)]["futureEntityField"] = {
            {"kind", "discard"}
        };
        root["entities"][std::to_string(entityId)]["transform"]["futureTransformFlag"] = "ignore-me";
        root["entities"][std::to_string(entityId)]["brushes"][0]["futureBrushField"] = "ignore-me";
        root["entities"][std::to_string(entityId)]["brushes"][0]["faces"][0]["futureFaceFlag"] = 42;
        root["materials"]["MatFuture"]["unknownMaterialField"] = "ignore-me";
        root["editor"]["futureEditorField"] = {"discard", "me"};

        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << root.dump(2);
            REQUIRE(out.good());
        }

        const auto loaded = forge::serial::loadProject(path);
        REQUIRE(loaded.has_value());

        CHECK(loaded->scene.name == "future_scene");
        CHECK(loaded->scene.author == "future-author");

        const auto* entity = loaded->scene.getEntity(entityId);
        REQUIRE(entity != nullptr);
        const auto* brush = std::get_if<forge::scene::BrushEntity>(entity);
        REQUIRE(brush != nullptr);
        REQUIRE_FALSE(brush->brushes.empty());
        REQUIRE_FALSE(brush->brushes[0].faces.empty());
        CHECK(brush->brushes[0].faces[0].materialId == "MatFuture");

        const auto& mats = loaded->materials.materials();
        REQUIRE(mats.contains("MatFuture"));
        REQUIRE(mats.at("MatFuture") != nullptr);
        CHECK(mats.at("MatFuture")->name == "MatFuture");

        REQUIRE(loaded->editor.entityLayers.contains(entityId));
        CHECK(loaded->editor.entityLayers.at(entityId) == "FutureLayer");
        REQUIRE(loaded->editor.entityGroups.contains(entityId));
        CHECK(loaded->editor.entityGroups.at(entityId) == "FutureGroup");
        REQUIRE(loaded->editor.layerVisibility.contains("FutureLayer"));
        CHECK(loaded->editor.layerVisibility.at("FutureLayer"));
        REQUIRE(loaded->editor.layerTint.contains("FutureLayer"));
        CHECK(loaded->editor.layerTint.at("FutureLayer").x == Approx(0.9f));

        CHECK_FALSE(loaded->schemaNote.empty());
        CHECK(loaded->schemaNote.find("future schema version") != std::string::npos);

        std::filesystem::remove(path);
}

TEST_CASE("Forward-compatible project ignores unknown fields in properties and textures", "[serial][project]") {
        using namespace forge;

        scene::Scene scene;
        scene.name = "nested_unknown_scene";
        scene.author = "tester";

        scene::PointEntity pointEntity;
        pointEntity.name = "info_point";
        pointEntity.classname = "info_player_start";
        pointEntity.properties["team"] = std::string("red");
        pointEntity.properties["health"] = 100;
        pointEntity.properties["speed"] = 1.5f;
        pointEntity.properties["active"] = true;
        pointEntity.properties["spawn_offset"] = glm::vec3(0.0f, 1.8f, 0.0f);
        const auto pointId = scene.addEntity(std::move(pointEntity));

        gfx::MaterialLibrary materials;
        auto matWithTextures = std::make_shared<gfx::Material>("MatWithTextures");
        matWithTextures->albedoTextureId = "tex_albedo.dds";
        matWithTextures->normalTextureId = "tex_normal.dds";
        materials.addMaterial(matWithTextures);

        serial::EditorMetadata editor;
        editor.entityLayers[pointId] = "Spawns";

        const auto path = std::filesystem::temp_directory_path() / "forge_nested_compat_unknown.forge";
        const auto saveErr = serial::saveProject(scene, materials, editor, path);
        REQUIRE(saveErr.empty());

        auto root = nlohmann::json::parse(std::ifstream(path));
        root["forge_version"] = "0.4.0";

        // Inject unknown fields inside each property object
        if (root["entities"][std::to_string(pointId)].contains("properties")) {
            auto& props = root["entities"][std::to_string(pointId)]["properties"];
            for (auto& [propKey, propValue] : props.items()) {
                propValue["futurePropertyField"] = "discard-me";
                propValue["unknownPropMeta"] = {"version", 2};
            }
        }

        // Inject unknown fields at material level
        if (root["materials"].contains("MatWithTextures")) {
            auto& mat = root["materials"]["MatWithTextures"];
            mat["futureTexturePipeline"] = {"hdr", true};
            mat["unknownMaterialMeta"] = {"encoded", "yes"};
        }

        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << root.dump(2);
            REQUIRE(out.good());
        }

        const auto loaded = forge::serial::loadProject(path);
        REQUIRE(loaded.has_value());

        CHECK(loaded->scene.name == "nested_unknown_scene");
        CHECK(loaded->scene.author == "tester");

        const auto* entity = loaded->scene.getEntity(pointId);
        REQUIRE(entity != nullptr);
        const auto* point = std::get_if<scene::PointEntity>(entity);
        REQUIRE(point != nullptr);
        CHECK(point->classname == "info_player_start");
        REQUIRE(point->properties.size() == 5);

        const auto& team = point->properties.at("team");
        REQUIRE(std::holds_alternative<std::string>(team));
        CHECK(std::get<std::string>(team) == "red");

        const auto& health = point->properties.at("health");
        REQUIRE(std::holds_alternative<int>(health));
        CHECK(std::get<int>(health) == 100);

        const auto& speed = point->properties.at("speed");
        REQUIRE(std::holds_alternative<float>(speed));
        CHECK(std::get<float>(speed) == Approx(1.5f));

        const auto& active = point->properties.at("active");
        REQUIRE(std::holds_alternative<bool>(active));
        CHECK(std::get<bool>(active) == true);

        const auto& spawnOffset = point->properties.at("spawn_offset");
        REQUIRE(std::holds_alternative<glm::vec3>(spawnOffset));
        const auto& vec = std::get<glm::vec3>(spawnOffset);
        CHECK(vec.x == Approx(0.0f));
        CHECK(vec.y == Approx(1.8f));
        CHECK(vec.z == Approx(0.0f));

        const auto& mats = loaded->materials.materials();
        REQUIRE(mats.contains("MatWithTextures"));
        REQUIRE(mats.at("MatWithTextures") != nullptr);
        const auto* mat = mats.at("MatWithTextures").get();
        CHECK(mat->albedoTextureId == "tex_albedo.dds");
        CHECK(mat->normalTextureId == "tex_normal.dds");

        REQUIRE(loaded->editor.entityLayers.contains(pointId));
        CHECK(loaded->editor.entityLayers.at(pointId) == "Spawns");

        CHECK_FALSE(loaded->schemaNote.empty());
        CHECK(loaded->schemaNote.find("future schema version") != std::string::npos);

        std::filesystem::remove(path);
}

TEST_CASE("Project with corrupted entity ID in face references loads with graceful fallback", "[serial][project]") {
        using namespace forge;

        scene::Scene scene;
        scene.name = "corrupted_entity_ids";

        scene::BrushEntity brushEntity;
        brushEntity.name = "corrupt_brush";
        brushEntity.brushes = { geo::makeBox({0, 0, 0}, {64, 64, 64}) };
        for (auto& face : brushEntity.brushes[0].faces) {
            face.materialId = "MatA";
        }
        const auto entityId = scene.addEntity(std::move(brushEntity));

        gfx::MaterialLibrary materials;
        auto matA = std::make_shared<gfx::Material>("MatA");
        matA->roughness = 0.5f;
        materials.addMaterial(matA);

        serial::EditorMetadata editor;
        editor.entityLayers[entityId] = "Layer1";

        const auto path = std::filesystem::temp_directory_path() / "forge_corrupted_entity_refs.forge";
        const auto saveErr = serial::saveProject(scene, materials, editor, path);
        REQUIRE(saveErr.empty());

        // Corrupt face references by changing materialId to a nonexistent material
        auto root = nlohmann::json::parse(std::ifstream(path));
        for (auto& [entIdStr, entityJson] : root["entities"].items()) {
            if (entityJson.contains("brushes")) {
                for (auto& brush : entityJson["brushes"]) {
                    if (brush.contains("faces")) {
                        for (auto& face : brush["faces"]) {
                            // Reference a material that doesn't exist in the library
                            face["materialId"] = "NonexistentMaterial_XYZ";
                        }
                    }
                }
            }
        }

        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << root.dump(2);
            REQUIRE(out.good());
        }

        const auto loaded = serial::loadProject(path);
        REQUIRE(loaded.has_value());
        CHECK(loaded->scene.name == "corrupted_entity_ids");

        const auto* entity = loaded->scene.getEntity(entityId);
        REQUIRE(entity != nullptr);
        const auto* brush = std::get_if<scene::BrushEntity>(entity);
        REQUIRE(brush != nullptr);
        REQUIRE_FALSE(brush->brushes.empty());
        REQUIRE_FALSE(brush->brushes[0].faces.empty());
        // Face should still be loaded, even if materialId is corrupted
        // The referenced material won't be found, but the face data persists
        CHECK(brush->brushes[0].faces[0].materialId == "NonexistentMaterial_XYZ");

        std::filesystem::remove(path);
}

TEST_CASE("Project with missing required material texture falls back gracefully", "[serial][project]") {
        using namespace forge;

        scene::Scene scene;
        scene.name = "missing_textures";

        scene::PointEntity pointEntity;
        pointEntity.name = "textured_point";
        pointEntity.classname = "info_point";
        pointEntity.properties["color"] = glm::vec3(1.0f, 0.5f, 0.0f);
        const auto pointId = scene.addEntity(std::move(pointEntity));

        gfx::MaterialLibrary materials;
        auto matWithMissingTextures = std::make_shared<gfx::Material>("MatMissingTex");
        // Set texture IDs that don't actually exist
        matWithMissingTextures->albedoTextureId = "missing_albedo.dds";
        matWithMissingTextures->normalTextureId = "missing_normal.dds";
        matWithMissingTextures->metallicTextureId = "missing_metallic.dds";
        materials.addMaterial(matWithMissingTextures);

        serial::EditorMetadata editor;
        editor.entityLayers[pointId] = "Objects";

        const auto path = std::filesystem::temp_directory_path() / "forge_missing_textures.forge";
        const auto saveErr = serial::saveProject(scene, materials, editor, path);
        REQUIRE(saveErr.empty());

        const auto loaded = serial::loadProject(path);
        REQUIRE(loaded.has_value());
        CHECK(loaded->scene.name == "missing_textures");

        const auto& mats = loaded->materials.materials();
        REQUIRE(mats.contains("MatMissingTex"));
        const auto* mat = mats.at("MatMissingTex").get();
        // Texture IDs should be preserved even if files don't exist
        CHECK(mat->albedoTextureId == "missing_albedo.dds");
        CHECK(mat->normalTextureId == "missing_normal.dds");
        CHECK(mat->metallicTextureId == "missing_metallic.dds");

        // Scene should load successfully despite missing texture files
        const auto* entity = loaded->scene.getEntity(pointId);
        REQUIRE(entity != nullptr);
        const auto* point = std::get_if<scene::PointEntity>(entity);
        REQUIRE(point != nullptr);
        CHECK(point->classname == "info_point");

        std::filesystem::remove(path);
}

TEST_CASE("Project with excessive nesting in properties loads without stack overflow", "[serial][project]") {
        using namespace forge;

        scene::Scene scene;
        scene.name = "deeply_nested";

        scene::PointEntity pointEntity;
        pointEntity.name = "nested_point";
        pointEntity.classname = "nested_entity";
        pointEntity.properties["simple"] = std::string("value");
        const auto pointId = scene.addEntity(std::move(pointEntity));

        gfx::MaterialLibrary materials;
        auto simpleMat = std::make_shared<gfx::Material>("SimpleMat");
        materials.addMaterial(simpleMat);

        serial::EditorMetadata editor;
        editor.entityLayers[pointId] = "Layer1";

        const auto path = std::filesystem::temp_directory_path() / "forge_deep_nesting.forge";
        const auto saveErr = serial::saveProject(scene, materials, editor, path);
        REQUIRE(saveErr.empty());

        // Inject deeply nested unknown fields
        auto root = nlohmann::json::parse(std::ifstream(path));
        
        // Create a deeply nested object (10+ levels)
        auto deepNested = nlohmann::json::object();
        auto* current = &deepNested;
        for (int i = 0; i < 15; ++i) {
            (*current)["level_" + std::to_string(i)] = nlohmann::json::object();
            current = &((*current)["level_" + std::to_string(i)]);
        }
        (*current)["deep_value"] = "deeply_nested_unknown_field";

        // Inject at various levels
        root["deeply_nested_root"] = deepNested;
        if (root["entities"][std::to_string(pointId)].contains("properties")) {
            auto& props = root["entities"][std::to_string(pointId)]["properties"];
            props["simple"]["futurePropertyMeta"] = deepNested;
            props["simple"]["futurePropertyArray"] = {deepNested, deepNested};
        }
        if (root["materials"].contains("SimpleMat")) {
            root["materials"]["SimpleMat"]["nested_meta"] = deepNested;
        }

        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << root.dump(2);
            REQUIRE(out.good());
        }

        // This should load successfully without stack overflow
        const auto loaded = serial::loadProject(path);
        REQUIRE(loaded.has_value());
        CHECK(loaded->scene.name == "deeply_nested");

        const auto* entity = loaded->scene.getEntity(pointId);
        REQUIRE(entity != nullptr);
        const auto* point = std::get_if<scene::PointEntity>(entity);
        REQUIRE(point != nullptr);
        CHECK(point->classname == "nested_entity");

        std::filesystem::remove(path);
}

    TEST_CASE("Project with malformed JSON syntax fails to parse", "[serial][project]") {
        const auto path = std::filesystem::temp_directory_path() / "forge_malformed_json.forge";

        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << "{\n  \"forge_version\": \"0.2.0\",\n  \"entities\": [\n";
            REQUIRE(out.good());
        }

        const auto loaded = forge::serial::loadProject(path);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().find("JSON parse error") != std::string::npos);

        std::filesystem::remove(path);
    }

    TEST_CASE("Project with invalid transform types fails gracefully", "[serial][project]") {
        using namespace forge;

        scene::Scene scene;
        scene.name = "bad_transform_types";

        scene::PointEntity pointEntity;
        pointEntity.name = "bad_point";
        pointEntity.classname = "info_point";
        const auto pointId = scene.addEntity(std::move(pointEntity));

        gfx::MaterialLibrary materials;
        serial::EditorMetadata editor;

        const auto path = std::filesystem::temp_directory_path() / "forge_bad_transform_types.forge";
        const auto saveErr = serial::saveProject(scene, materials, editor, path);
        REQUIRE(saveErr.empty());

        auto root = nlohmann::json::parse(std::ifstream(path));
        root["entities"][std::to_string(pointId)]["transform"]["translation"] = "not-a-vector";

        {
            std::ofstream out(path);
            REQUIRE(out.is_open());
            out << root.dump(2);
            REQUIRE(out.good());
        }

        const auto loaded = forge::serial::loadProject(path);
        REQUIRE_FALSE(loaded.has_value());
        const bool hasExpectedErrorText =
            loaded.error().find("Load error") != std::string::npos ||
            loaded.error().find("JSON parse error") != std::string::npos;
        CHECK(hasExpectedErrorText);

        std::filesystem::remove(path);
    }
