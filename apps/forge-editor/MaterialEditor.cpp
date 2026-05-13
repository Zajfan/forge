#include "MaterialEditor.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <fstream>

namespace forge::editor {

void MaterialEditor::selectMaterial(const std::string& name) noexcept {
    if (!library_) return;
    selected_ = library_->getMaterial(name);
}

void MaterialEditor::draw() noexcept {
    if (!library_) return;

    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Material Editor", nullptr, ImGuiWindowFlags_NoMove)) {
        ImGui::BeginTabBar("MaterialEditorTabs");

        // ──────────────────────────────────────────────────────────────
        // Tab 1: Library Browser
        // ──────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Library")) {
            drawLibraryBrowser();
            ImGui::EndTabItem();
        }

        // ──────────────────────────────────────────────────────────────
        // Tab 2: Properties (edits selected material)
        // ──────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Properties")) {
            if (selected_) {
                ImGui::PushID(selected_.get());

                // Material name
                static char nameBuf[256];
                strncpy(nameBuf, selected_->name.c_str(), 255);
                if (ImGui::InputText("Name", nameBuf, 256, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    renameMaterial(nameBuf);
                }

                ImGui::Separator();

                // ── Color Properties ──────────────────────────────────
                drawColorProperty("Albedo", selected_->albedoColor);
                drawColorProperty("Emissive", selected_->emissiveColor);

                ImGui::Separator();

                // ── PBR Parameters ────────────────────────────────────
                drawMaterialProperty("Metallic", selected_->metallic, 0.0f, 1.0f);
                drawMaterialProperty("Roughness", selected_->roughness, 0.04f, 1.0f);
                drawMaterialProperty("AO", selected_->ambientOcclusion, 0.0f, 1.0f);

                ImGui::Separator();

                // ── Texture Scale Parameters ──────────────────────────
                drawMaterialProperty("Normal Scale", selected_->normalScale, 0.0f, 2.0f);
                drawMaterialProperty("Emissive Scale", selected_->emissiveScale, 0.0f, 4.0f);

                ImGui::Separator();

                // ── Textures ──────────────────────────────────────────
                ImGui::Text("Textures:");
                drawTextureProperty("Albedo Map", selected_->albedoTextureId);
                drawTextureProperty("Normal Map", selected_->normalTextureId);
                drawTextureProperty("Metallic Map", selected_->metallicTextureId);
                drawTextureProperty("Roughness Map", selected_->roughnessTextureId);
                drawTextureProperty("AO Map", selected_->aoTextureId);
                drawTextureProperty("Emissive Map", selected_->emissiveTextureId);

                ImGui::Separator();

                // ── Alpha Blending ────────────────────────────────────
                ImGui::Checkbox("Alpha Blend", &selected_->useAlphaBlend);
                if (selected_->useAlphaBlend) {
                    drawMaterialProperty("Alpha Cutoff", selected_->alphaCutoff, 0.0f, 1.0f);
                }

                ImGui::PopID();
            } else {
                ImGui::TextDisabled("Select a material to edit properties");
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

void MaterialEditor::drawMaterialProperty(const std::string& label, float& value,
                                          float minVal, float maxVal) noexcept {
    ImGui::SliderFloat(label.c_str(), &value, minVal, maxVal);
}

void MaterialEditor::drawColorProperty(const std::string& label, glm::vec3& color) noexcept {
    ImGui::ColorEdit3(label.c_str(), glm::value_ptr(color));
}

void MaterialEditor::drawTextureProperty(const std::string& label, std::string& textureId) noexcept {
    char buffer[512] = {};
    std::snprintf(buffer, sizeof(buffer), "%s", textureId.c_str());
    if (ImGui::InputText(("##tex_" + label).c_str(), buffer, sizeof(buffer))) {
        textureId = buffer;
    }
    ImGui::SameLine();
    if (ImGui::Button(("Browse##" + label).c_str())) {
        // TODO: Open file browser dialog (use portable file dialogs library)
        ImGui::OpenPopup(("texture_browser_" + label).c_str());
    }

    // Simple text input for now; full file dialog would use pfd::open_file()
}

void MaterialEditor::drawLibraryBrowser() noexcept {
    ImGui::Text("Materials: %zu", library_->materials().size());
    ImGui::Separator();

    // Create new material
    static char newMatName[256] = "NewMaterial";
    ImGui::InputText("Material Name", newMatName, 256);
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        auto newMat = library_->getOrCreateMaterial(newMatName);
        selectMaterial(newMatName);
    }

    ImGui::Separator();

    // List all materials
    ImGui::BeginChild("MaterialList", ImVec2(0, -50), true);
    for (const auto& [name, mat] : library_->materials()) {
        bool isSelected = (selected_ == mat);
        if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            selectMaterial(name);
        }

        // Context menu (right-click)
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                deleteSelected();
            }
            if (ImGui::MenuItem("Export")) {
                exportMaterial("materials/" + name + ".json");
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    // Action buttons
    if (selected_) {
        if (ImGui::Button("Delete Selected", ImVec2(-1, 0))) {
            deleteSelected();
        }
    }
}

std::shared_ptr<gfx::Material> MaterialEditor::createNewMaterial() noexcept {
    if (!library_) return nullptr;
    std::string name = "Material_" + std::to_string(nextMaterialIndex_++);
    return library_->getOrCreateMaterial(name);
}

void MaterialEditor::deleteSelected() noexcept {
    // MaterialLibrary doesn't have delete yet, but we can clear and rebuild
    // For now, just deselect
    selected_ = nullptr;
}

void MaterialEditor::renameMaterial(const std::string& newName) noexcept {
    if (!selected_ || !library_) return;
    selected_->name = newName;
}

void MaterialEditor::exportMaterial(const std::string& filepath) const noexcept {
    if (!selected_) return;
    
    try {
        auto jsonData = selected_->toJson();
        std::ofstream file(filepath);
        // Simple key=value format (no JSON library required)
        for (const auto& [k, v] : jsonData) {
            file << k << "=" << v << "\n";
        }
    } catch (...) {
        // Silently fail for now
    }
}

void MaterialEditor::importMaterial(const std::string& filepath) noexcept {
    if (!library_) return;
    
    try {
        std::ifstream file(filepath);
        std::unordered_map<std::string, std::string> jsonData;
        std::string line;
        while (std::getline(file, line)) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                jsonData[key] = value;
            }
        }
        
        auto mat = std::make_shared<gfx::Material>(
            gfx::Material::fromJson(jsonData)
        );
        library_->addMaterial(mat);
    } catch (...) {
        // Silently fail for now
    }
}

} // namespace forge::editor
