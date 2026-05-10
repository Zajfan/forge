#pragma once

#include "Command.hpp"
#include "Selection.hpp"
#include "FacePicker.hpp"

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

enum class ActiveTool {
    Select,      // entity AABB selection
    Move,        // entity translate (ImGuizmo)
    Rotate,      // entity rotate
    Scale,       // entity scale
    FaceSelect,  // face-level selection (Möller-Trumbore)
    FaceMove,    // move face along its plane normal
    Clip,        // define a clip plane and split brush
    Paint,       // paint material onto faces
};

// ─── Clip state ───────────────────────────────────────────────────────────────

enum class ClipAxis { X, Y, Z };

struct ClipState {
    ClipAxis axis     = ClipAxis::Y;
    float    position = 0.f;
    bool     keepBoth = true;   // keep front+back (false = keep front only)
};

// ─── EditorApp ───────────────────────────────────────────────────────────────

class EditorApp {
public:
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

    // ── Destructive operations ────────────────────────────────────────────────
    void applyClip();
    void applyCSGSubtract();
    void applyHollow();

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
    void drawFaceProperties();   // shown when face is selected
    void drawClipProperties();   // shown when Clip tool is active
    void drawStatusBar();
    void drawAddPrimitivesMenu();

    void setupDefaultDockLayout(ImGuiID dockspaceId);

    // ── Viewport overlays ─────────────────────────────────────────────────────
    void drawFaceOverlay   (ImVec2 vpPos, ImVec2 vpSize, const glm::mat4& vp);
    void drawClipPreview   (ImVec2 vpPos, ImVec2 vpSize, const glm::mat4& vp);
    void handleViewportMouse(ImVec2 viewportPos, ImVec2 viewportSize);
    void drawGizmo         (ImVec2 viewportPos, ImVec2 viewportSize);

    // ── GL resources ──────────────────────────────────────────────────────────
    gfx::Window       window_;
    gfx::Renderer     renderer_;
    gfx::GridRenderer gridRenderer_;
    gfx::Framebuffer  viewportFbo_;
    gfx::OrbitCamera  camera_;

    // ── Scene + editor state ──────────────────────────────────────────────────
    scene::Scene  scene_;
    Selection     selection_;
    FaceSelection faceSelection_;
    CommandStack  commands_;

    ActiveTool    activeTool_  = ActiveTool::Select;
    ClipState     clipState_;
    double        hollowThickness_ = 16.0;
    char          paintMaterial_[256] = "default";

    bool showGrid_    = true;
    bool wireframe_   = false;
    bool layoutReady_ = false;

    // GPU mesh cache
    struct EntityGPUData {
        gfx::GPUEntityMesh mesh;
        glm::mat4          model = glm::mat4(1.f);
    };
    std::unordered_map<scene::EntityId, EntityGPUData> gpuData_;

    // Gizmo drag tracking
    glm::vec3 gizmoDragStartPos_ = {};
    bool      gizmoDragging_     = false;

    // UI state
    char  renameBuffer_[256] = {};
    std::string statusMessage_;
    float statusTimer_ = 0.f;

    void setStatus(std::string msg, float duration = 3.f);
};

} // namespace forge::editor
