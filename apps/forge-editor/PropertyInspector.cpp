#include "PropertyInspector.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace forge::editor {

// Helper to get properties from an entity variant
static std::unordered_map<std::string, scene::PropertyValue>* getEntityProperties(scene::Entity& entity) {
    if (auto* brush = std::get_if<scene::BrushEntity>(&entity)) {
        return &brush->properties;
    } else if (auto* point = std::get_if<scene::PointEntity>(&entity)) {
        return &point->properties;
    }
    return nullptr;
}

static std::string trimCopy(std::string s) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static bool propertyValueEquals(const scene::PropertyValue& a, const scene::PropertyValue& b) {
    if (a.index() != b.index()) return false;
    return std::visit(
        [](const auto& lhs, const auto& rhs) -> bool {
            using L = std::decay_t<decltype(lhs)>;
            using R = std::decay_t<decltype(rhs)>;
            if constexpr (!std::is_same_v<L, R>) {
                return false;
            } else if constexpr (std::is_same_v<L, glm::vec3>) {
                const glm::vec3 d = lhs - rhs;
                return std::fabs(d.x) < 1e-5f && std::fabs(d.y) < 1e-5f && std::fabs(d.z) < 1e-5f;
            } else {
                return lhs == rhs;
            }
        },
        a, b);
}

static bool tryParsePropertyValue(
    const scene::PropertyValue& currentValue,
    const std::string& text,
    scene::PropertyValue& outValue)
{
    const std::string trimmed = trimCopy(text);

    if (std::holds_alternative<std::string>(currentValue)) {
        outValue = text;
        return true;
    }
    if (std::holds_alternative<int>(currentValue)) {
        try {
            std::size_t pos = 0;
            const int v = std::stoi(trimmed, &pos);
            if (pos != trimmed.size()) return false;
            outValue = v;
            return true;
        } catch (...) {
            return false;
        }
    }
    if (std::holds_alternative<float>(currentValue)) {
        try {
            std::size_t pos = 0;
            const float v = std::stof(trimmed, &pos);
            if (pos != trimmed.size()) return false;
            outValue = v;
            return true;
        } catch (...) {
            return false;
        }
    }
    if (std::holds_alternative<bool>(currentValue)) {
        std::string lowered = trimmed;
        std::transform(
            lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lowered == "true" || lowered == "1") {
            outValue = true;
            return true;
        }
        if (lowered == "false" || lowered == "0") {
            outValue = false;
            return true;
        }
        return false;
    }
    if (std::holds_alternative<glm::vec3>(currentValue)) {
        std::string normalized = trimmed;
        std::replace(normalized.begin(), normalized.end(), ',', ' ');
        std::stringstream ss(normalized);
        float x = 0.f, y = 0.f, z = 0.f;
        std::string extra;
        if ((ss >> x >> y >> z) && !(ss >> extra)) {
            outValue = glm::vec3{x, y, z};
            return true;
        }
        return false;
    }
    return false;
}

// Helper to get transform from entity variant
static const scene::Transform& getEntityTransform(const scene::Entity& entity) {
    return std::visit([](const auto& x) -> const scene::Transform& { return x.transform; }, entity);
}

// Helper to get entity name
static const std::string& getEntityName(const scene::Entity& entity) {
    return std::visit([](const auto& x) -> const std::string& { return x.name; }, entity);
}

void PropertyInspector::draw(scene::Scene& scene, const MultiSelection& selection, bool& isOpen, CommandStack& commands) {
    ImGui::SetNextWindowSize({350.f, 400.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Property Inspector", &isOpen);

    if (selection.empty()) {
        ImGui::TextDisabled("(No entity selected)");
        ImGui::End();
        return;
    }

    // Only show properties for single selection
    if (selection.ids.size() > 1) {
        ImGui::TextColored({1.f, 0.8f, 0.2f, 1.f}, "(%zu entities selected)", selection.ids.size());
        ImGui::TextDisabled("Edit one entity at a time");
        ImGui::End();
        return;
    }

    const auto selectedId = *selection.ids.begin();
    auto* entity = scene.getEntity(selectedId);
    if (!entity) {
        ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "Entity not found!");
        ImGui::End();
        return;
    }

    lastSelectedId_ = selectedId;

    // Show entity name and type
    ImGui::SeparatorText("Entity");
    const std::string& name = getEntityName(*entity);
    const std::string type = std::visit([](const auto& x) {
        if constexpr (std::is_same_v<decltype(x), const scene::BrushEntity&>) return "Brush";
        else if constexpr (std::is_same_v<decltype(x), const scene::PointEntity&>) return "Point";
        else return "Mesh";
    }, *entity);
    ImGui::Text("Name: %s", name.c_str());
    ImGui::Text("Type: %s", type.c_str());

    // Show transform (read-only)
    ImGui::SeparatorText("Transform");
    const auto& trans = getEntityTransform(*entity);
    if (ImGui::BeginTable("##transform", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Translation");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f, %.2f, %.2f", trans.translation.x, trans.translation.y, trans.translation.z);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Rotation");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("(%.2f, %.2f, %.2f, %.2f)", trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Scale");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f, %.2f, %.2f", trans.scale.x, trans.scale.y, trans.scale.z);

        ImGui::EndTable();
    }

    // Show properties
    auto* props = getEntityProperties(*entity);
    if (!props || props->empty()) {
        ImGui::SeparatorText("Properties");
        ImGui::TextDisabled("(No custom properties)");
    } else {
        ImGui::SeparatorText("Properties");
        if (ImGui::BeginTable("##properties", 2, ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& [key, value] : *props) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", key.c_str());
                ImGui::TableSetColumnIndex(1);

                ImGui::PushID(key.c_str());

                // Display based on type with inline editing capability
                if (std::holds_alternative<std::string>(value)) {
                    const auto& str = std::get<std::string>(value);
                    ImGui::Text("%s", str.c_str());
                    if (ImGui::IsItemClicked()) {
                        editingProperty_ = key;
                        std::snprintf(editBuffer_, kEditBufferSize, "%s", str.c_str());
                        ImGui::OpenPopup("##edit_property");
                    }
                } else if (std::holds_alternative<int>(value)) {
                    const auto num = std::get<int>(value);
                    ImGui::Text("%d", num);
                    if (ImGui::IsItemClicked()) {
                        editingProperty_ = key;
                        std::snprintf(editBuffer_, kEditBufferSize, "%d", num);
                        ImGui::OpenPopup("##edit_property");
                    }
                } else if (std::holds_alternative<float>(value)) {
                    const auto num = std::get<float>(value);
                    ImGui::Text("%.4f", num);
                    if (ImGui::IsItemClicked()) {
                        editingProperty_ = key;
                        std::snprintf(editBuffer_, kEditBufferSize, "%.4f", num);
                        ImGui::OpenPopup("##edit_property");
                    }
                } else if (std::holds_alternative<bool>(value)) {
                    const auto b = std::get<bool>(value);
                    ImGui::TextColored(b ? ImVec4{0.3f, 1.f, 0.3f, 1.f} : ImVec4{0.7f, 0.7f, 0.7f, 1.f},
                                       "%s", b ? "true" : "false");
                    if (ImGui::IsItemClicked()) {
                        editingProperty_ = key;
                        std::snprintf(editBuffer_, kEditBufferSize, "%s", b ? "true" : "false");
                        ImGui::OpenPopup("##edit_property");
                    }
                } else if (std::holds_alternative<glm::vec3>(value)) {
                    const auto& v = std::get<glm::vec3>(value);
                    ImGui::Text("%.2f, %.2f, %.2f", v.x, v.y, v.z);
                    if (ImGui::IsItemClicked()) {
                        editingProperty_ = key;
                        std::snprintf(editBuffer_, kEditBufferSize, "%.2f, %.2f, %.2f", v.x, v.y, v.z);
                        ImGui::OpenPopup("##edit_property");
                    }
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    // Edit property modal
    if (ImGui::BeginPopupModal("##edit_property", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (editingProperty_) {
            ImGui::Text("Edit property: %s", editingProperty_->c_str());
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            scene::PropertyValue currentValueForHint;
            bool hasCurrentValue = false;
            if (auto* currentEntityForHint = scene.getEntity(selectedId)) {
                if (auto* currentPropsForHint = getEntityProperties(*currentEntityForHint)) {
                    if (auto it = currentPropsForHint->find(*editingProperty_); it != currentPropsForHint->end()) {
                        currentValueForHint = it->second;
                        hasCurrentValue = true;
                    }
                }
            }

            const bool enterPressed = ImGui::InputText(
                "##value", editBuffer_, kEditBufferSize, ImGuiInputTextFlags_EnterReturnsTrue);

            if (hasCurrentValue) {
                if (std::holds_alternative<int>(currentValueForHint)) {
                    ImGui::TextDisabled("Hint: integer (e.g. 42)");
                } else if (std::holds_alternative<float>(currentValueForHint)) {
                    ImGui::TextDisabled("Hint: float (e.g. 3.14)");
                } else if (std::holds_alternative<bool>(currentValueForHint)) {
                    ImGui::TextDisabled("Hint: bool (true/false/1/0)");
                } else if (std::holds_alternative<glm::vec3>(currentValueForHint)) {
                    ImGui::TextDisabled("Hint: vec3 format x, y, z");
                } else {
                    ImGui::TextDisabled("Hint: text value");
                }

                scene::PropertyValue parsedPreview;
                if (!tryParsePropertyValue(currentValueForHint, std::string(editBuffer_), parsedPreview)) {
                    ImGui::TextColored(ImVec4{1.f, 0.45f, 0.45f, 1.f}, "Input format is invalid for this type");
                }
            }

            bool applyRequested = enterPressed;
            bool cancelRequested = ImGui::IsKeyPressed(ImGuiKey_Escape);

            if (ImGui::Button("Apply", {80, 0})) {
                applyRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {80, 0})) {
                cancelRequested = true;
            }

            if (applyRequested) {
                // Refresh entity pointer and apply the value
                auto* currentEntity = scene.getEntity(selectedId);
                auto* currentProps = currentEntity ? getEntityProperties(*currentEntity) : nullptr;
                if (currentProps && currentProps->count(*editingProperty_)) {
                    const auto& currentValue = currentProps->at(*editingProperty_);
                    const std::string bufStr(editBuffer_);

                    scene::PropertyValue parsedValue;
                    if (tryParsePropertyValue(currentValue, bufStr, parsedValue)) {
                        if (!propertyValueEquals(currentValue, parsedValue)) {
                            commands.push(
                                std::make_unique<SetEntityPropertyCommand>(
                                    selectedId,
                                    *editingProperty_,
                                    std::optional<scene::PropertyValue>{currentValue},
                                    parsedValue),
                                scene);
                        }
                        editingProperty_.reset();
                        ImGui::CloseCurrentPopup();
                    }
                }
            }

            if (cancelRequested) {
                editingProperty_.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace forge::editor
