#include <catch2/catch_test_macros.hpp>
#include <forge/gfx/Material.hpp>
#include <memory>

using namespace forge::gfx;

TEST_CASE("Material initialization", "[material]") {
    Material m("TestMat");
    REQUIRE(m.name == "TestMat");
    REQUIRE(m.metallic == 0.0f);
    REQUIRE(m.roughness == 0.5f);
    REQUIRE(m.ambientOcclusion == 1.0f);
}

TEST_CASE("Material JSON serialization", "[material]") {
    Material m("PBRTest");
    m.albedoColor = {1.0f, 0.5f, 0.2f};
    m.metallic = 0.8f;
    m.roughness = 0.3f;
    m.normalScale = 2.0f;
    m.albedoTextureId = "brick/wall01";
    m.normalTextureId = "brick/wall01_normal";

    auto json = m.toJson();
    REQUIRE(json["name"] == "PBRTest");
    REQUIRE(json["metallic"] == std::to_string(0.8f));

    Material m2 = Material::fromJson(json);
    REQUIRE(m2.name == "PBRTest");
    REQUIRE(m2.metallic == Approx(0.8f));
    REQUIRE(m2.roughness == Approx(0.3f));
    REQUIRE(m2.normalScale == Approx(2.0f));
    REQUIRE(m2.albedoTextureId == "brick/wall01");
}

TEST_CASE("MaterialLibrary", "[material]") {
    MaterialLibrary lib;
    
    auto mat1 = std::make_shared<Material>("Steel");
    mat1->metallic = 1.0f;
    mat1->roughness = 0.2f;
    lib.addMaterial(mat1);

    REQUIRE(lib.size() == 1);
    REQUIRE(lib.getMaterial("Steel") != nullptr);
    REQUIRE(lib.getMaterial("Steel")->metallic == 1.0f);
    
    auto mat2 = lib.getOrCreateMaterial("Wood");
    REQUIRE(lib.size() == 2);
    REQUIRE(mat2->name == "Wood");
    
    auto mat2Again = lib.getMaterial("Wood");
    REQUIRE(mat2Again == mat2);
}

TEST_CASE("Material defaults", "[material]") {
    Material m;
    REQUIRE(m.albedoColor == glm::vec3(0.8f));
    REQUIRE(m.emissiveColor == glm::vec3(0.0f));
    REQUIRE(m.metallic == 0.0f);
    REQUIRE(m.roughness == 0.5f);
    REQUIRE(m.normalScale == 1.0f);
    REQUIRE(!m.useAlphaBlend);
}

TEST_CASE("Material JSON round-trip", "[material]") {
    Material original("ComplexMat");
    original.albedoColor = {0.2f, 0.3f, 0.4f};
    original.emissiveColor = {0.1f, 0.05f, 0.0f};
    original.metallic = 0.5f;
    original.roughness = 0.6f;
    original.ambientOcclusion = 0.9f;
    original.useAlphaBlend = true;
    original.alphaCutoff = 0.25f;
    
    auto json = original.toJson();
    Material restored = Material::fromJson(json);
    
    REQUIRE(restored.name == original.name);
    REQUIRE(restored.albedoColor == original.albedoColor);
    REQUIRE(restored.emissiveColor == original.emissiveColor);
    REQUIRE(restored.metallic == original.metallic);
    REQUIRE(restored.roughness == original.roughness);
    REQUIRE(restored.ambientOcclusion == original.ambientOcclusion);
    REQUIRE(restored.useAlphaBlend == original.useAlphaBlend);
    REQUIRE(restored.alphaCutoff == original.alphaCutoff);
}
