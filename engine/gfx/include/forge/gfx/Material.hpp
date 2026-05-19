#pragma once

#include <forge/gfx/TextureCache.hpp>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <unordered_map>

namespace forge::gfx {

/// Material represents a complete PBR material with properties and texture bindings
struct Material {
    // PBR color properties (sRGB encoded for display)
    glm::vec3 albedoColor     = glm::vec3(0.8f);
    glm::vec3 emissiveColor   = glm::vec3(0.0f);

    // PBR scalar properties
    float metallic            = 0.0f;   // 0 = dielectric, 1 = metal
    float roughness           = 0.5f;   // 0 = mirror, 1 = rough
    float ambientOcclusion    = 1.0f;   // 0 = fully occluded, 1 = full light
    float normalScale         = 1.0f;   // Normal map intensity
    float emissiveScale       = 1.0f;   // Emissive map intensity

    // Texture IDs (maps to TextureCache)
    std::string albedoTextureId;
    std::string normalTextureId;
    std::string roughnessTextureId;
    std::string metallicTextureId;
    std::string aoTextureId;           // Ambient occlusion
    std::string emissiveTextureId;

    // Material metadata
    std::string name;                   // Human-readable name
    bool useAlphaBlend = false;        // Alpha blending (vs opaque)
    float alphaCutoff = 0.5f;          // For alpha-tested materials

    Material() = default;
    explicit Material(const std::string& materialName) : name(materialName) {}

    // Serialize to JSON-compatible map
    std::unordered_map<std::string, std::string> toJson() const;
    
    // Deserialize from JSON-compatible map
    static Material fromJson(const std::unordered_map<std::string, std::string>& data);
};

/// MaterialLibrary manages a collection of materials
class MaterialLibrary {
public:
    MaterialLibrary() = default;

    void addMaterial(const std::shared_ptr<Material>& material);
    bool removeMaterial(const std::string& name);
    bool renameMaterial(const std::string& oldName, const std::string& newName);
    std::shared_ptr<Material> getMaterial(const std::string& name);
    std::shared_ptr<Material> getOrCreateMaterial(const std::string& name);

    const std::unordered_map<std::string, std::shared_ptr<Material>>& materials() const {
        return materials_;
    }

    void clear() { materials_.clear(); }
    size_t size() const { return materials_.size(); }

private:
    std::unordered_map<std::string, std::shared_ptr<Material>> materials_;
};

}
