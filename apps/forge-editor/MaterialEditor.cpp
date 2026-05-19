#include "MaterialEditor.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <portable-file-dialogs.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <format>

namespace {

std::string toEditorPath(const std::filesystem::path& p) {
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        const auto rel = std::filesystem::relative(p, cwd, ec);
        if (!ec && !rel.empty()) return rel.generic_string();
    }
    return p.generic_string();
}

bool isTextureExt(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".tga" || ext == ".bmp" || ext == ".dds" || ext == ".ktx";
}

std::vector<std::string> collectTextureAssets(const std::filesystem::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec)) return out;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto& p = entry.path();
        if (!isTextureExt(p)) continue;
        out.push_back(toEditorPath(p));
    }
    std::sort(out.begin(), out.end());
    return out;
}

}

namespace forge::editor {

void MaterialEditor::selectMaterial(const std::string& name) noexcept {
    if (!library_) return;
    selected_ = library_->getMaterial(name);
    selectedName_ = selected_ ? name : std::string{};
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
                nameBuf[255] = '\0';
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

                if (textureCache_) {
                    ImGui::SeparatorText("Texture Assets (drag onto fields)");
                    if (textureAssetRoot_.empty()) {
                        textureAssetRoot_ = textureCache_->textureRoot();
                    }
                    if (ImGui::Button("Refresh Texture List", ImVec2(-1.f, 0.f))) {
                        textureAssetList_ = collectTextureAssets(textureAssetRoot_);
                    }
                    if (textureAssetList_.empty()) {
                        textureAssetList_ = collectTextureAssets(textureAssetRoot_);
                    }

                    ImGui::BeginChild("TextureAssetList", ImVec2(0.f, 120.f), true);
                    for (const auto& texPath : textureAssetList_) {
                        ImGui::Selectable(texPath.c_str(), false);
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("FORGE_TEXTURE_PATH",
                                texPath.c_str(), texPath.size() + 1);
                            ImGui::TextUnformatted(texPath.c_str());
                            ImGui::EndDragDropSource();
                        }
                    }
                    ImGui::EndChild();
                }

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

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FORGE_TEXTURE_PATH")) {
            const char* dropped = static_cast<const char*>(payload->Data);
            if (dropped && payload->DataSize > 0) {
                textureId = dropped;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    if (ImGui::Button(("Browse##" + label).c_str())) {
        try {
            const auto files = pfd::open_file(
                "Select Texture",
                (std::filesystem::current_path() / "textures").string(),
                { "Image Files", "*.png *.jpg *.jpeg *.tga *.bmp *.dds *.ktx", "All Files", "*" },
                pfd::opt::none).result();
            if (!files.empty()) {
                textureId = toEditorPath(std::filesystem::path(files.front()));
            }
        } catch (...) {
            // Ignore file dialog failures and keep the current texture path.
        }
    }

    if (textureCache_ && !textureId.empty()) {
        const uint32_t texId = textureCache_->load(textureId);
        ImGui::SameLine();
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(texId)), ImVec2(28.f, 28.f));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", textureId.c_str());
        }
    }
}

void MaterialEditor::drawLibraryBrowser() noexcept {
    ImGui::Text("Materials: %zu", library_->materials().size());
    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }
    ImGui::Separator();

    // Create new material
    static char newMatName[256] = "NewMaterial";
    ImGui::InputText("Material Name", newMatName, 256);
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        auto newMat = library_->getOrCreateMaterial(newMatName);
        (void)newMat;
        selectMaterial(newMatName);
        statusMessage_ = "Created material: " + std::string(newMatName);
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
                selectMaterial(name);
                requestDeleteSelected();
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
            requestDeleteSelected();
        }
    }

    ImGui::Separator();
    static char ioPathBuf[512] = "materials/material.txt";
    ImGui::InputText("Path", ioPathBuf, sizeof(ioPathBuf));
    if (ImGui::Button("Browse Import...", ImVec2(-1, 0))) {
        try {
            const auto files = pfd::open_file(
                "Import Material",
                (std::filesystem::current_path() / "materials").string(),
                { "Material Files", "*.txt *.mat *.json", "All Files", "*" },
                pfd::opt::none).result();
            if (!files.empty()) {
                const auto importPath = toEditorPath(std::filesystem::path(files.front()));
                std::snprintf(ioPathBuf, sizeof(ioPathBuf), "%s", importPath.c_str());
            }
        } catch (...) {
            // Ignore file dialog failures and keep current path.
        }
    }
    if (ImGui::Button("Browse Export...", ImVec2(-1, 0))) {
        try {
            const auto file = pfd::save_file(
                "Export Material",
                ioPathBuf,
                { "Material Files", "*.txt *.mat *.json", "All Files", "*" },
                pfd::opt::none).result();
            if (!file.empty()) {
                const auto exportPath = toEditorPath(std::filesystem::path(file));
                std::snprintf(ioPathBuf, sizeof(ioPathBuf), "%s", exportPath.c_str());
            }
        } catch (...) {
            // Ignore file dialog failures and keep current path.
        }
    }
    if (ImGui::Button("Import", ImVec2(-1, 0))) {
        importMaterial(ioPathBuf);
    }
    if (selected_ && ImGui::Button("Export Selected", ImVec2(-1, 0))) {
        exportMaterial(ioPathBuf);
    }

    if (confirmDeleteOpen_) {
        ImGui::OpenPopup("Confirm Material Delete");
        confirmDeleteOpen_ = false;
    }
    if (ImGui::BeginPopupModal("Confirm Material Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete material: %s", pendingDeleteName_.c_str());
        bool canProceed = true;
        if (pendingDeleteUsage_.faceCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1.0f),
                "Warning: referenced by %zu face(s) across %zu brush entit(ies).",
                pendingDeleteUsage_.faceCount,
                pendingDeleteUsage_.brushEntityCount);

            ImGui::Checkbox("Replace references before delete", &replaceRefsOnDelete_);
            if (replaceRefsOnDelete_) {
                ImGui::InputText("Replacement Material", replaceDeleteBuffer_.data(), replaceDeleteBuffer_.size());
                const std::string replacement = replaceDeleteBuffer_.data();
                if (replacement.empty() || replacement == pendingDeleteName_) {
                    ImGui::TextDisabled("Enter a different replacement material name.");
                    canProceed = false;
                }
            }
        } else {
            ImGui::TextDisabled("No scene references found.");
        }
        ImGui::Separator();

        ImGui::TextWrapped("Type the material name to confirm deletion:");
        ImGui::InputText("##confirm_delete_name", deleteConfirmBuffer_.data(), deleteConfirmBuffer_.size());
        const bool canDelete = pendingDeleteName_ == std::string(deleteConfirmBuffer_.data());
        if (!canDelete) {
            ImGui::TextDisabled("Name must match exactly.");
        }

        ImGui::BeginDisabled(!canDelete || !canProceed);
        if (ImGui::Button("Delete", ImVec2(140.f, 0.f))) {
            if (replaceRefsOnDelete_ && pendingDeleteUsage_.faceCount > 0) {
                const std::string replacement = replaceDeleteBuffer_.data();
                if (!replacement.empty() && replacement != pendingDeleteName_) {
                    library_->getOrCreateMaterial(replacement);
                    const std::size_t replaced = replaceMaterialReferences(pendingDeleteName_, replacement);
                    statusMessage_ = std::format("Replaced {} face material reference(s) with '{}' before delete.",
                        replaced, replacement);
                }
            }
            deleteMaterialByName(pendingDeleteName_);
            pendingDeleteName_.clear();
            pendingDeleteUsage_ = {};
            deleteConfirmBuffer_.fill('\0');
            replaceDeleteBuffer_.fill('\0');
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140.f, 0.f))) {
            pendingDeleteName_.clear();
            pendingDeleteUsage_ = {};
            deleteConfirmBuffer_.fill('\0');
            replaceDeleteBuffer_.fill('\0');
            statusMessage_ = "Delete cancelled.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::shared_ptr<gfx::Material> MaterialEditor::createNewMaterial() noexcept {
    if (!library_) return nullptr;
    std::string name = "Material_" + std::to_string(nextMaterialIndex_++);
    return library_->getOrCreateMaterial(name);
}

void MaterialEditor::deleteSelected() noexcept {
    if (!selected_ || selectedName_.empty()) return;
    deleteMaterialByName(selectedName_);
}

MaterialEditor::MaterialUsage MaterialEditor::scanMaterialUsage(const std::string& materialName) const noexcept {
    MaterialUsage usage{};
    if (!scene_ || materialName.empty()) return usage;

    for (const auto& [_, entity] : scene_->entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be) continue;

        std::size_t entityFaceRefs = 0;
        for (const auto& brush : be->brushes) {
            for (const auto& face : brush.faces) {
                if (face.materialId == materialName) {
                    ++entityFaceRefs;
                }
            }
        }

        if (entityFaceRefs > 0) {
            ++usage.brushEntityCount;
            usage.faceCount += entityFaceRefs;
        }
    }
    return usage;
}

std::size_t MaterialEditor::replaceMaterialReferences(const std::string& from, const std::string& to) noexcept {
    if (!scene_ || from.empty() || to.empty() || from == to) return 0;

    std::size_t replaced = 0;
    for (auto& [_, entity] : scene_->entities) {
        auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be) continue;
        for (auto& brush : be->brushes) {
            for (auto& face : brush.faces) {
                if (face.materialId == from) {
                    face.materialId = to;
                    ++replaced;
                }
            }
        }
    }
    return replaced;
}

void MaterialEditor::requestDeleteSelected() noexcept {
    if (!selected_ || selectedName_.empty()) return;
    pendingDeleteName_ = selectedName_;
    pendingDeleteUsage_ = scanMaterialUsage(selectedName_);
    replaceRefsOnDelete_ = pendingDeleteUsage_.faceCount > 0;
    std::snprintf(replaceDeleteBuffer_.data(), replaceDeleteBuffer_.size(), "%s", "default");
    deleteConfirmBuffer_.fill('\0');
    confirmDeleteOpen_ = true;
}

void MaterialEditor::deleteMaterialByName(const std::string& name) noexcept {
    if (!library_ || name.empty()) return;
    if (library_->removeMaterial(name)) {
        statusMessage_ = "Deleted material: " + name;
    } else {
        statusMessage_ = "Delete failed for: " + name;
    }

    if (selectedName_ == name) {
        selected_ = nullptr;
        selectedName_.clear();
    }
}

void MaterialEditor::renameMaterial(const std::string& newName) noexcept {
    if (!selected_ || !library_) return;
    if (newName.empty()) {
        statusMessage_ = "Rename failed: name is empty";
        return;
    }
    if (selectedName_.empty()) {
        selectedName_ = selected_->name;
    }
    if (selectedName_ == newName) return;

    const std::string oldName = selectedName_;
    const MaterialUsage oldUsage = scanMaterialUsage(oldName);

    if (library_->renameMaterial(selectedName_, newName)) {
        selectedName_ = newName;
        selected_ = library_->getMaterial(newName);
        std::size_t replaced = 0;
        if (oldUsage.faceCount > 0) {
            replaced = replaceMaterialReferences(oldName, newName);
        }
        if (replaced > 0) {
            statusMessage_ = std::format("Renamed '{}' to '{}' and replaced {} face reference(s).",
                oldName, newName, replaced);
        } else {
            statusMessage_ = "Renamed material to: " + newName;
        }
    } else {
        statusMessage_ = "Rename failed (duplicate or missing source name)";
    }
}

void MaterialEditor::exportMaterial(const std::string& filepath) const noexcept {
    if (!selected_) return;
    
    try {
        const std::filesystem::path outPath(filepath);
        if (!outPath.parent_path().empty()) {
            std::filesystem::create_directories(outPath.parent_path());
        }
        auto jsonData = selected_->toJson();
        std::ofstream file(outPath);
        if (!file.is_open()) return;
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
        if (!file.is_open()) {
            statusMessage_ = "Import failed: could not open file";
            return;
        }
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
        selectMaterial(mat->name);
        statusMessage_ = "Imported material: " + mat->name;
    } catch (...) {
        statusMessage_ = "Import failed: invalid file format";
    }
}

} // namespace forge::editor
