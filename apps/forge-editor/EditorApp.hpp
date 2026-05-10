#pragma once

#include "Command.hpp"
#include "Selection.hpp"

#include <forge/gfx.hpp>
#include <forge/build.hpp>
#include <forge/export.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace forge::editor {

// ─── ActiveTool ───────────────────────────────────────────────────────────────

enum class ActiveTool { Select, Move, Rotate, Scale };

// ─── EditorApp ───────────────────────────────────────────────────────────────

class EditorApp {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────
    [[nodiscard]] bool init();
    void run();
    void shutdown();

private:
    // ── Scene management ──────────────────────────────────────────────────────
    void newScene();
    void buildDefaultScene();
    void rebuildEntityMesh(scene::EntityId id);
    void rebuildAllMeshes();
    glm::vec3 materialColour(const std::string& matId) const noexcept;

    // ── File I/O ──────────────────────────────────────────────────────────────
    void exportOBJ();
    void exportMAP();

    // ── UI drawing ────────────────────────────────────────────────────────────
    void drawFrame();
    void drawMainMenuBar();
    void drawToolbar();
    void drawViewport();
    void drawSceneTree();
    void drawProperties();
    void drawStatusBar();
    void drawAddPrimitivesMenu();

    void setupDefaultDockLayout(ImGuiID dockspaceId);

    // ── Viewport interaction ──────────────────────────────────────────────────
    void handleViewportMouse(ImVec2 viewportPos, ImVec2 viewportSize);
    void drawGizmo(ImVec2 viewportPos, ImVec2 viewportSize);

    // ── GL resources ──────────────────────────────────────────────────────────
    gfx::Window       window_;
    gfx::Renderer     renderer_;
    gfx::GridRenderer gridRenderer_;
    gfx::Framebuffer  viewportFbo_;
    gfx::OrbitCamera  camera_;

    // ── Scene + editor state ──────────────────────────────────────────────────
    scene::Scene scene_;
    Selection    selection_;
    CommandStack commands_;
    ActiveTool   activeTool_  = ActiveTool::Select;
    bool         showGrid_    = true;
    bool         wireframe_   = false;
    bool         layoutReady_ = false;

    // GPU mesh cache: EntityId → list of GPU submeshes
    struct EntityGPUData {
        gfx::GPUEntityMesh mesh;
        glm::mat4          model = glm::mat4(1.f); // world transform snapshot
    };
    std::unordered_map<scene::EntityId, EntityGPUData> gpuData_;

    // ── Gizmo drag state ──────────────────────────────────────────────────────
    glm::vec3 gizmoDragStartPos_ = {};
    bool      gizmoDragging_     = false;

    // ── UI state ──────────────────────────────────────────────────────────────
    char renameBuffer_[256] = {};
    std::string statusMessage_;
    float statusTimer_ = 0.f;
    std::filesystem::path lastExportPath_;

    void setStatus(std::string msg, float duration = 3.f);
};

} // namespace forge::editor
