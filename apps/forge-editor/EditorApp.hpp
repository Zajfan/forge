#pragma once

#include "Command.hpp"
#include "Selection.hpp"
#include "FacePicker.hpp"

#include <forge/gfx.hpp>
#include <forge/build.hpp>
#include <forge/export.hpp>
#include <forge/runtime.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace forge::editor {

enum class ActiveTool {
    Select, Move, Rotate, Scale,
    FaceSelect, FaceMove, Clip, Paint,
};

enum class ClipAxis { X, Y, Z };
struct ClipState { ClipAxis axis = ClipAxis::Y; float position = 0.f; bool keepBoth = true; };

class EditorApp {
public:
    [[nodiscard]] bool init();
    void run();
    void shutdown();

private:
    // ── Scene ────────────────────────────────────────────────────────────────
    void newScene();
    void buildDefaultScene();
    void rebuildEntityMesh(scene::EntityId id);
    void rebuildAllMeshes();
    glm::vec3 materialColour(const std::string& id) const noexcept;

    // ── Operations ───────────────────────────────────────────────────────────
    void applyClip();
    void applyCSGSubtract();
    void applyHollow();
    void exportOBJ();
    void exportMAP();

    // ── Play mode ─────────────────────────────────────────────────────────────
    void enterPlayMode();
    void exitPlayMode();
    void runPlayFrame();
    void drawPlayHUD();

    // ── Edit-mode UI ─────────────────────────────────────────────────────────
    void drawFrame();
    void drawMainMenuBar();
    void drawToolbar();
    void drawViewport();
    void drawSceneTree();
    void drawProperties();
    void drawFaceProperties();
    void drawClipProperties();
    void drawStatusBar();
    void drawAddPrimitivesMenu();
    void setupDefaultDockLayout(ImGuiID dock);

    // ── Viewport helpers ──────────────────────────────────────────────────────
    void drawFaceOverlay   (ImVec2 pos, ImVec2 sz, const glm::mat4& vp);
    void drawClipPreview   (ImVec2 pos, ImVec2 sz, const glm::mat4& vp);
    void handleViewportMouse(ImVec2 pos, ImVec2 sz);
    void drawGizmo         (ImVec2 pos, ImVec2 sz);

    // ── GL resources ──────────────────────────────────────────────────────────
    gfx::Window       window_;
    gfx::Renderer     renderer_;
    gfx::GridRenderer gridRenderer_;
    gfx::Framebuffer  viewportFbo_;
    gfx::OrbitCamera  camera_;

    // ── Edit state ────────────────────────────────────────────────────────────
    scene::Scene  scene_;
    Selection     selection_;
    FaceSelection faceSelection_;
    CommandStack  commands_;
    ActiveTool    activeTool_  = ActiveTool::Select;
    ClipState     clipState_;
    double        hollowThickness_ = 16.0;
    char          paintMaterial_[256] = "default";
    bool          showGrid_    = true;
    bool          wireframe_   = false;
    bool          layoutReady_ = false;

    struct EntityGPUData {
        gfx::GPUEntityMesh mesh;
        glm::mat4          model = glm::mat4(1.f);
    };
    std::unordered_map<scene::EntityId, EntityGPUData> gpuData_;

    glm::vec3 gizmoDragStartPos_ = {};
    bool      gizmoDragging_     = false;
    char      renameBuffer_[256] = {};

    // ── Play mode state ───────────────────────────────────────────────────────
    bool                             playMode_ = false;
    std::unique_ptr<runtime::GameRuntime> runtime_;

    // ── Status bar ────────────────────────────────────────────────────────────
    std::string statusMessage_;
    float       statusTimer_ = 0.f;
    void        setStatus(std::string msg, float dur = 3.f);
};

} // namespace forge::editor
