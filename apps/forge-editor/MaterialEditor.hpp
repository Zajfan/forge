#pragma once

#include <forge/gfx/Material.hpp>
#include <forge/gfx/TextureCache.hpp>
#include <forge/scene/Scene.hpp>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace forge::editor {

/// Material editor state — tracks active material and panel visibility
class MaterialEditor {
public:
    MaterialEditor() = default;

    void setMaterialLibrary(gfx::MaterialLibrary* lib) noexcept {
        library_ = lib;
    }
    void setScene(scene::Scene* scene) noexcept {
        scene_ = scene;
    }
    void setTextureCache(gfx::TextureCache* cache) noexcept {
        textureCache_ = cache;
    }

    void selectMaterial(const std::string& name) noexcept;
    [[nodiscard]] std::shared_ptr<gfx::Material> selected() const noexcept {
        return selected_;
    }

    /// Draw the material editor panel (ImGui)
    void draw() noexcept;

    /// Draw a single material property row
    void drawMaterialProperty(const std::string& label, float& value,
                              float minVal = 0.0f, float maxVal = 1.0f) noexcept;

    /// Draw color picker for material color properties
    void drawColorProperty(const std::string& label, glm::vec3& color) noexcept;

    /// Draw texture ID field with file browser button
    void drawTextureProperty(const std::string& label, std::string& textureId) noexcept;

    /// Draw material library browser (all materials + creation)
    void drawLibraryBrowser() noexcept;

    /// Create a new material with default name
    std::shared_ptr<gfx::Material> createNewMaterial() noexcept;

    /// Delete selected material
    void deleteSelected() noexcept;

    /// Rename selected material
    void renameMaterial(const std::string& newName) noexcept;

    /// Export selected material to JSON file
    void exportMaterial(const std::string& filepath) const noexcept;

    /// Import material from JSON file
    void importMaterial(const std::string& filepath) noexcept;

    struct MaterialUsage {
        std::size_t brushEntityCount = 0;
        std::size_t faceCount = 0;
    };

    [[nodiscard]] MaterialUsage scanMaterialUsage(const std::string& materialName) const noexcept;
    [[nodiscard]] std::size_t replaceMaterialReferences(const std::string& from, const std::string& to) noexcept;
    void requestDeleteSelected() noexcept;
    void deleteMaterialByName(const std::string& name) noexcept;

private:
    gfx::MaterialLibrary* library_ = nullptr;
    gfx::TextureCache* textureCache_ = nullptr;
    scene::Scene* scene_ = nullptr;
    std::shared_ptr<gfx::Material> selected_ = nullptr;
    std::string selectedName_;
    std::string statusMessage_;
    bool confirmDeleteOpen_ = false;
    std::string pendingDeleteName_;
    MaterialUsage pendingDeleteUsage_{};
    bool replaceRefsOnDelete_ = true;
    std::array<char, 256> replaceDeleteBuffer_{};
    std::array<char, 256> deleteConfirmBuffer_{};
    bool showLibraryBrowser_ = true;
    bool showProperties_ = true;
    bool showTexturePreview_ = false;
    std::string newMaterialName_;
    int nextMaterialIndex_ = 0;
    std::vector<std::string> textureAssetList_;
    std::filesystem::path textureAssetRoot_;
};

} // namespace forge::editor
