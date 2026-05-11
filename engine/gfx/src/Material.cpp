#include <forge/gfx/Material.hpp>

namespace forge::gfx {

std::unordered_map<std::string, std::string> Material::toJson() const {
    return {
        {"name", name},
        {"albedoColor.r", std::to_string(albedoColor.r)},
        {"albedoColor.g", std::to_string(albedoColor.g)},
        {"albedoColor.b", std::to_string(albedoColor.b)},
        {"emissiveColor.r", std::to_string(emissiveColor.r)},
        {"emissiveColor.g", std::to_string(emissiveColor.g)},
        {"emissiveColor.b", std::to_string(emissiveColor.b)},
        {"metallic", std::to_string(metallic)},
        {"roughness", std::to_string(roughness)},
        {"ambientOcclusion", std::to_string(ambientOcclusion)},
        {"normalScale", std::to_string(normalScale)},
        {"emissiveScale", std::to_string(emissiveScale)},
        {"albedoTextureId", albedoTextureId},
        {"normalTextureId", normalTextureId},
        {"roughnessTextureId", roughnessTextureId},
        {"metallicTextureId", metallicTextureId},
        {"aoTextureId", aoTextureId},
        {"emissiveTextureId", emissiveTextureId},
        {"useAlphaBlend", useAlphaBlend ? "true" : "false"},
        {"alphaCutoff", std::to_string(alphaCutoff)},
    };
}

Material Material::fromJson(const std::unordered_map<std::string, std::string>& data) {
    Material m;
    
    auto get = [&data](const std::string& key, const std::string& defaultVal = "") -> std::string {
        auto it = data.find(key);
        return it != data.end() ? it->second : defaultVal;
    };

    auto getFloat = [&get](const std::string& key, float defaultVal = 0.0f) -> float {
        try { return std::stof(get(key)); } catch (...) { return defaultVal; }
    };

    m.name = get("name", "Untitled");
    m.albedoColor = glm::vec3(
        getFloat("albedoColor.r", 0.8f),
        getFloat("albedoColor.g", 0.8f),
        getFloat("albedoColor.b", 0.8f)
    );
    m.emissiveColor = glm::vec3(
        getFloat("emissiveColor.r", 0.0f),
        getFloat("emissiveColor.g", 0.0f),
        getFloat("emissiveColor.b", 0.0f)
    );
    m.metallic = getFloat("metallic", 0.0f);
    m.roughness = getFloat("roughness", 0.5f);
    m.ambientOcclusion = getFloat("ambientOcclusion", 1.0f);
    m.normalScale = getFloat("normalScale", 1.0f);
    m.emissiveScale = getFloat("emissiveScale", 1.0f);
    m.albedoTextureId = get("albedoTextureId");
    m.normalTextureId = get("normalTextureId");
    m.roughnessTextureId = get("roughnessTextureId");
    m.metallicTextureId = get("metallicTextureId");
    m.aoTextureId = get("aoTextureId");
    m.emissiveTextureId = get("emissiveTextureId");
    m.useAlphaBlend = get("useAlphaBlend") == "true";
    m.alphaCutoff = getFloat("alphaCutoff", 0.5f);

    return m;
}

void MaterialLibrary::addMaterial(const std::shared_ptr<Material>& material) {
    if (material) {
        materials_[material->name] = material;
    }
}

std::shared_ptr<Material> MaterialLibrary::getMaterial(const std::string& name) {
    auto it = materials_.find(name);
    return it != materials_.end() ? it->second : nullptr;
}

std::shared_ptr<Material> MaterialLibrary::getOrCreateMaterial(const std::string& name) {
    auto it = materials_.find(name);
    if (it != materials_.end()) {
        return it->second;
    }
    auto material = std::make_shared<Material>(name);
    materials_[name] = material;
    return material;
}

}
