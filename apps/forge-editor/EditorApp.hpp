#pragma once

#include "Command.hpp"
#include "Selection.hpp"
#include "FacePicker.hpp"
#include "GridSnap.hpp"
#include "EntityRegistry.hpp"
#include "PrefabSystem.hpp"
#include "LevelValidator.hpp"
#include "MaterialEditor.hpp"
#include <forge/bsp.hpp>
#include <forge/audio.hpp>
#include <forge/script.hpp>
#include <forge/audio.hpp>
#include <forge/script.hpp>

#include <forge/gfx.hpp>
#include <forge/serial.hpp>
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
    void savePrefabFromSelection();
    void insertPrefab();
    void applyCSGSubtract();
    void applyHollow();
    void exportOBJ();
    void exportMAP();
    void exportGLTF();
    void saveScene();
    void saveSceneAs();
    void openScene();

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
    void drawQuadViewport();
    void drawSceneTree();
    void drawProperties();
    void drawMaterialBrowser();
    void drawUndoHistory();
    void drawEntityClassBrowser();
    void drawValidationPanel();
    void runValidation();
    void duplicateSelection();
    void buildBSP();
    void drawBSPPanel();
    void drawMeshEntities(const gfx::RenderFrame& frame);
    void drawScriptConsole();
    void drawBloomSettings();
    void drawAudioSettings();
    void drawScriptConsole();
    void tickEntityScripts(float dt);
    void triggerSpeakerEntities();
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
    gfx::Window          window_;
    gfx::Renderer        renderer_;
    gfx::GridRenderer    gridRenderer_;
    gfx::SkyboxRenderer  skyboxRenderer_;
    gfx::TextureCache    textureCache_;
    gfx::OrbitCamera     camera_;

    // Multi-viewport
    ViewportLayout       viewportLayout_  = ViewportLayout::Single;
    gfx::Framebuffer     viewportFbo_;         // single-view FBO
    std::array<gfx::Framebuffer, 4> quadFbos_; // quad-view FBOs [persp,top,front,right]
    std::array<gfx::OrthoCamera,  3> orthoCams_; // top, front, right
    int                  activeQuadPane_  = 0;  // 0=persp,1=top,2=front,3=right

    // Skybox, fog, shadows
    bool  showSkybox_    = true;
    bool  showFog_       = false;
    bool  showShadows_   = true;
    gfx::ShadowMap shadowMap_;

    // Validation
    ValidationReport lastValidation_;
    bool             showValidation_ = false;

    // BSP
    bsp::BSPTree        bspTree_;
    bool                bspBuilt_     = false;
    bool                showBSPStats_ = false;

    // Mesh asset cache
    gfx::MeshAssetCache  meshAssetCache_;

    // Material editor
    MaterialEditor       materialEditor_;
    gfx::MaterialLibrary materialLibrary_;
    bool                 showMaterialEditor_ = true;

    // Bloom
    gfx::BloomRenderer   bloomRenderer_;
    bool                 showBloom_        = true;
    float                bloomThreshold_   = 0.80f;
    float                bloomIntensity_   = 1.00f;

    // Audio (active during play mode)
    audio::AudioEngine   audioEngine_;
    bool                 audioInitialised_ = false;

    // Scripting
    script::ScriptEnv    scriptEnv_;
    bool                 showScriptConsole_ = false;
    char                 scriptInputBuf_[1024] = {};

    // Bloom
    gfx::BloomRenderer  bloomRenderer_;
    bool                bloomEnabled_ = false;
    bool                showBloomSettings_ = false;

    // Audio
    audio::AudioEngine  audioEngine_;
    bool                showAudioSettings_ = false;
    float               masterVolume_      = 0.8f;

    // Scripting
    script::ScriptEnv   scriptEnv_;
    bool                showScriptConsole_ = false;
    char                scriptInput_[512]  = {};

    // ── Edit state ────────────────────────────────────────────────────────────
    scene::Scene  scene_;
    MultiSelection selection_;
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
    std::string textureRootPath_;  ///< User-set texture search directory
    float       statusTimer_ = 0.f;
    void        setStatus(std::string msg, float dur = 3.f);
};

} // namespace forge::editor
