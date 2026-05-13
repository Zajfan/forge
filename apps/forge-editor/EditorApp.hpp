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

#include <forge/gfx.hpp>
#include <forge/serial.hpp>
#include <forge/build.hpp>
#include <forge/export.hpp>
#include <forge/runtime.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace forge::editor {

enum class ActiveTool {
    Select, Move, Rotate, Scale,
    FaceSelect, FaceMove, Clip, Paint, VertexSelect, BoxCreate,
};

enum class ViewportLayout { Single, Quad };

enum class ClipAxis { X, Y, Z };
struct ClipState { ClipAxis axis = ClipAxis::Y; float position = 0.f; bool keepBoth = true; };

class EditorApp {
public:
    struct EntityGPUData {
        gfx::GPUEntityMesh mesh;
        glm::mat4          model = glm::mat4(1.f);
    };

    [[nodiscard]] bool init();
    void run();
    void shutdown();

private:
    // ── Scene ────────────────────────────────────────────────────────────────
    void newScene();
    void buildDefaultScene();
    void rebuildEntityMesh(scene::EntityId id);
    void rebuildAllMeshes();
    void updateEntityTransform(scene::EntityId id);  ///< Update GPU model matrix only (no mesh rebuild)
    glm::vec3 materialColour(const std::string& id) const noexcept;
    scene::EntityId addPrimitiveEntity(std::string name, geo::Brush brush, const glm::dvec3& worldPos);
    glm::dvec3 viewportSpawnPoint(ImVec2 vpPos, ImVec2 vpSz) const;
    void addDraggedBox(const glm::dvec3& a, const glm::dvec3& b, double height);
    void extrudeSelectedFace(double distance);
    void insetSelectedFace(double insetAmount, double depth);
    void bridgeSelectedFaces();
    void registerBridgeFaceSelection(const FaceHit& pick);
    [[nodiscard]] std::string entityLayer(scene::EntityId id);
    [[nodiscard]] std::string entityGroup(scene::EntityId id);
    [[nodiscard]] bool layerVisible(const std::string& layer) const;
    [[nodiscard]] bool layerLocked(const std::string& layer) const;
    [[nodiscard]] glm::vec3 layerTint(const std::string& layer) const;
    [[nodiscard]] bool entityVisibleByLayer(scene::EntityId id) const;
    [[nodiscard]] bool entityVisibleByGroup(scene::EntityId id) const;
    [[nodiscard]] bool entityVisibleInEditor(scene::EntityId id) const;
    [[nodiscard]] bool entityLockedByLayer(scene::EntityId id) const;
    void reconcileEditorState();

    // ── Operations ───────────────────────────────────────────────────────────
    void applyClip();
    void savePrefabFromSelection();
    void insertPrefab();
    void applyCSGSubtract();
    void applyCSGUnion();
    void applyCSGIntersect();
    void applyCSGXor();
    void applyHollow();
    void exportOBJ();
    void exportMAP();
    void exportGLTF();
    void saveScene();
    void saveSceneAs();
    void openScene();

    // ── Play mode ─────────────────────────────────────────────────────────────
    void enterPlayMode();
    void enterPlayModeAudio();
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
    void drawInspector();
    void drawMaterialBrowser();
    void drawUndoHistory();
    void drawEntityClassBrowser();
    void drawValidationPanel();
    void runValidation();
    void duplicateSelection();
    void nudgeSelection(const glm::dvec3& delta);
    void hideSelection();
    void isolateSelection();
    void clearHiddenIsolation();
    void buildBSP();
    void drawBSPPanel();
    void drawMeshEntities(const gfx::RenderFrame& frame);
    void drawScriptConsole();
    void drawBloomSettings();
    void drawAudioSettings();
    void tickEntityScripts(float dt);
    void triggerSpeakerEntities();
    void drawFaceProperties();
    void drawClipProperties();
    void drawStatusBar();
    void drawAddPrimitivesMenu();
    void setupDefaultDockLayout(ImGuiID dock);

    // ── Viewport helpers ──────────────────────────────────────────────────────
    void drawFaceOverlay   (ImVec2 pos, ImVec2 sz, const glm::mat4& vp);
    void drawVertexOverlay (ImVec2 pos, ImVec2 sz, const glm::mat4& vp);
    void drawClipPreview   (ImVec2 pos, ImVec2 sz, const glm::mat4& vp);
    void drawBoxCreatePreview(ImVec2 pos, ImVec2 sz, const glm::mat4& vp);
    void drawMarqueePreview(ImVec2 pos);
    void handleViewportMouse(ImVec2 pos, ImVec2 sz);
    void drawGizmo         (ImVec2 pos, ImVec2 sz);
    void handleOrthoPaneMouse(int pane, ImVec2 panePos, ImVec2 paneSz);

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
    bool                 orthoEditMode_   = true;

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
    bool                 showBloom_        = false;
    float                bloomThreshold_   = 0.80f;
    float                bloomIntensity_   = 1.00f;
    bool                 bloomEnabled_     = false;
    bool                 showBloomSettings_ = false;

    // Audio (active during play mode)
    audio::AudioEngine   audioEngine_;
    bool                 audioInitialised_ = false;
    bool                 showAudioSettings_ = false;
    float                masterVolume_      = 0.8f;

    // Scripting
    script::ScriptEnv    scriptEnv_;
    bool                 showScriptConsole_ = false;
    char                scriptInput_[512]  = {};

    // ── Edit state ────────────────────────────────────────────────────────────
    scene::Scene  scene_;
    MultiSelection selection_;
    FaceSelection faceSelection_;
    VertexSelection vertexSelection_;
    CommandStack  commands_;
    ActiveTool    activeTool_  = ActiveTool::Select;
    ClipState     clipState_;
    double        hollowThickness_ = 16.0;
    char          paintMaterial_[256] = "default";
    
    // Paint tool state for drag-to-paint
    struct PaintDragState {
        bool                           dragging = false;
        ImVec2                         startPos = {};
        std::vector<FaceSelection>     paintedFaces;     ///< accumulated faces during drag
        std::vector<std::string>       origMaterials;    ///< original materials for undo
    } paintDrag_;
    
    bool          showGrid_    = true;
    bool          wireframe_   = false;
    bool          snapEnabled_ = true;
    float         gridSize_    = 32.f;
    bool          layoutReady_ = false;
    ImGuiID       dockspaceId_ = 0;
    std::unordered_map<scene::EntityId, EntityGPUData> gpuData_;

    struct BoxCreateState {
        bool      draggingFootprint = false;
        bool      adjustingHeight   = false;
        glm::dvec3 start = {};
        glm::dvec3 end   = {};
        double    height = 64.0;
        float     heightStartMouseY = 0.f;
    } boxCreate_;

    struct MarqueeState {
        bool   dragging = false;
        ImVec2 start{};
        ImVec2 end{};
    } marquee_;

    std::optional<FaceSelection> bridgeSourceFace_;

    std::unordered_map<scene::EntityId, std::string> entityLayers_;
    std::unordered_map<scene::EntityId, std::string> entityGroups_;
    std::unordered_set<scene::EntityId> hiddenEntities_;
    std::unordered_set<scene::EntityId> isolatedEntities_;
    std::unordered_map<std::string, bool> layerVisibility_;
    std::unordered_map<std::string, bool> layerLocked_;
    std::unordered_map<std::string, glm::vec3> layerTint_;
    std::string soloLayer_;
    std::string groupFilter_;
    char newLayerName_[64] = "";

    bool                 showMaterialBrowser_ = false;
    bool                 showUndoHistory_ = false;
    bool                 showEntityClasses_ = false;
    std::filesystem::path currentFile_;

    glm::vec3 gizmoDragStartPos_ = {};
    bool      gizmoDragging_     = false;
    char      renameBuffer_[256] = {};

    // Inline rename in Scene Tree (double-click to activate)
    scene::EntityId renamingId_          = scene::kInvalidEntityId;
    char            treeRenameBuffer_[256] = {};

    // UV drag undo coalescing (snapshot on drag-start, push command on drag-end)
    struct UVDragState {
        glm::vec2 offset{};
        glm::vec2 scale{1.f, 1.f};
        float     rotation = 0.f;
        bool      active   = false;
    } uvDragState_;

    struct OrthoDragState {
        struct Entry {
            scene::EntityId id = scene::kInvalidEntityId;
            glm::dvec3      startTranslation{};
        };
        bool            active = false;
        int             pane   = -1;
        scene::EntityId entityId = scene::kInvalidEntityId;
        glm::vec2       startMouse{};
        glm::vec2       lastMouse{};
        std::vector<Entry> entries;
    } orthoDragState_;

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
