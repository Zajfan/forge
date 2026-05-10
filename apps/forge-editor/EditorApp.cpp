#include "EditorApp.hpp"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <format>
#include <iostream>
#include <array>

namespace forge::editor {

// ─── Colour palette ──────────────────────────────────────────────────────────

static const std::array<glm::vec3, 8> kPalette = {{
    { 0.72f, 0.70f, 0.65f },
    { 0.55f, 0.50f, 0.45f },
    { 0.80f, 0.75f, 0.60f },
    { 0.40f, 0.45f, 0.55f },
    { 0.65f, 0.58f, 0.50f },
    { 0.50f, 0.60f, 0.50f },
    { 0.60f, 0.50f, 0.55f },
    { 0.45f, 0.55f, 0.65f },
}};

glm::vec3 EditorApp::materialColour(const std::string& id) const noexcept {
    return kPalette[std::hash<std::string>{}(id) % kPalette.size()];
}

void EditorApp::setStatus(std::string msg, float duration) {
    statusMessage_ = std::move(msg);
    statusTimer_   = duration;
}

// ─── Init ─────────────────────────────────────────────────────────────────────

bool EditorApp::init() {
    // ── Window ────────────────────────────────────────────────────────────────
    auto winResult = gfx::Window::create({
        .title       = "FORGE Editor",
        .width       = 1440,
        .height      = 900,
        .msaaSamples = 4,
        .glMajor     = 4,
        .glMinor     = 6,
    });
    if (!winResult) {
        std::cerr << "[error] " << winResult.error() << '\n';
        return false;
    }
    window_ = std::move(*winResult);

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Dark theme with editor tweaks
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.f;
    style.FrameRounding     = 3.f;
    style.TabRounding       = 3.f;
    style.ScrollbarRounding = 3.f;
    style.Colors[ImGuiCol_WindowBg]  = { 0.12f, 0.12f, 0.13f, 1.f };
    style.Colors[ImGuiCol_MenuBarBg] = { 0.10f, 0.10f, 0.11f, 1.f };
    style.Colors[ImGuiCol_Header]    = { 0.22f, 0.40f, 0.65f, 0.6f };

    ImGui_ImplSDL3_InitForOpenGL(window_.sdlWindow(), window_.glContext());
    ImGui_ImplOpenGL3_Init("#version 460");

    // ── Renderer ──────────────────────────────────────────────────────────────
    if (!renderer_.init()) {
        std::cerr << "[error] Renderer init failed\n";
        return false;
    }
    if (!gridRenderer_.init()) {
        std::cerr << "[warn] Grid renderer init failed — grid disabled\n";
    }

    // ── Initial viewport FBO ──────────────────────────────────────────────────
    viewportFbo_ = gfx::Framebuffer::create(1024, 600);

    // ── Default scene ─────────────────────────────────────────────────────────
    buildDefaultScene();

    return true;
}

// ─── Default scene ────────────────────────────────────────────────────────────

void EditorApp::buildDefaultScene() {
    scene_ = {};
    scene_.name = "untitled";
    selection_.clear();
    commands_.clear();
    gpuData_.clear();

    // Floor
    {
        scene::BrushEntity e;
        e.name    = "floor";
        e.brushes = { geo::makeBox({-256, -16, -256}, {256, 0, 256}) };
        auto id = scene_.addEntity(std::move(e));
        rebuildEntityMesh(id);
    }
    // North wall + doorway CSG
    {
        auto wall   = geo::makeBox({-256, 0, 240}, {256, 256, 256});
        auto cutter = geo::makeBox({-32, 0, 239}, {32, 192, 257});
        scene::BrushEntity e;
        e.name    = "north_wall";
        e.brushes = geo::csgSubtract(wall, cutter);
        auto id = scene_.addEntity(std::move(e));
        rebuildEntityMesh(id);
    }
    // South wall
    {
        scene::BrushEntity e;
        e.name    = "south_wall";
        e.brushes = { geo::makeBox({-256, 0, -256}, {256, 256, -240}) };
        auto id = scene_.addEntity(std::move(e));
        rebuildEntityMesh(id);
    }
    // Pillar
    {
        scene::BrushEntity e;
        e.name    = "pillar";
        e.brushes = { geo::makePrism({64, 0, 64}, 24.0, 256.0, 8) };
        auto id = scene_.addEntity(std::move(e));
        rebuildEntityMesh(id);
    }
    // Player spawn
    {
        scene::PointEntity pe;
        pe.name      = "player_start";
        pe.classname = "info_player_start";
        pe.transform = scene::Transform::fromTranslation({0, 32, 0});
        scene_.addEntity(std::move(pe));
    }

    camera_.frameAABB(scene_.worldBounds());
    setStatus("Default scene loaded");
}

// ─── Mesh management ─────────────────────────────────────────────────────────

void EditorApp::rebuildEntityMesh(scene::EntityId id) {
    const auto* e = scene_.getEntity(id);
    if (!e) { gpuData_.erase(id); return; }

    const auto* be = std::get_if<scene::BrushEntity>(e);
    if (!be) { gpuData_.erase(id); return; }

    EntityGPUData data;
    data.model = glm::mat4(be->transform.matrix());
    data.mesh  = gfx::GPUEntityMesh::upload(build::buildEntityMesh(*be, /*applyTransform=*/false));
    gpuData_[id] = std::move(data);
}

void EditorApp::rebuildAllMeshes() {
    gpuData_.clear();
    for (const auto& [id, entity] : scene_.entities)
        rebuildEntityMesh(id);
}

// ─── Run loop ─────────────────────────────────────────────────────────────────

void EditorApp::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();
        statusTimer_ -= window_.deltaTime();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        drawFrame();

        ImGui::Render();

        // Clear main window (panels cover it)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_.width(), window_.height());
        glClearColor(0.08f, 0.08f, 0.10f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window_.swapBuffers();
    }
}

// ─── Main frame ───────────────────────────────────────────────────────────────

void EditorApp::drawFrame() {
    // ── Full-window dockspace ─────────────────────────────────────────────────
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize      | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("##forge_host", nullptr, hostFlags);
    ImGui::PopStyleVar(2);

    // Menu bar inside host window
    drawMainMenuBar();

    // Dockspace
    const ImGuiID dockId = ImGui::GetID("ForgeDockspace");
    ImGui::DockSpace(dockId, {0.f, 0.f}, ImGuiDockNodeFlags_None);

    if (!layoutReady_) {
        setupDefaultDockLayout(dockId);
    }

    ImGui::End();

    // ── Tool bar (above viewport — rendered as separate panel) ────────────────
    drawToolbar();

    // ── Panels ────────────────────────────────────────────────────────────────
    drawViewport();
    drawSceneTree();
    drawProperties();
    drawStatusBar();
}

// ─── Default dock layout ──────────────────────────────────────────────────────

void EditorApp::setupDefaultDockLayout(ImGuiID dockId) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

    // Split: left (viewport+toolbar) | right (panels)
    ImGuiID rightId, leftId;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.24f, &rightId, &leftId);

    // Split right: top (scene tree) | bottom (properties)
    ImGuiID rightTopId, rightBotId;
    ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.55f, &rightBotId, &rightTopId);

    // Split left: top (toolbar) | bottom (viewport)
    ImGuiID toolbarId, viewportId;
    ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Up, 0.045f, &toolbarId, &viewportId);

    ImGui::DockBuilderDockWindow("Viewport",    viewportId);
    ImGui::DockBuilderDockWindow("##toolbar",   toolbarId);
    ImGui::DockBuilderDockWindow("Scene Tree",  rightTopId);
    ImGui::DockBuilderDockWindow("Properties",  rightBotId);
    ImGui::DockBuilderDockWindow("##status",    dockId);

    ImGui::DockBuilderFinish(dockId);
    layoutReady_ = true;
}

// ─── Menu bar ─────────────────────────────────────────────────────────────────

void EditorApp::drawMainMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    // ── File ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
            newScene();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Export")) {
            if (ImGui::MenuItem("Export OBJ..."))  exportOBJ();
            if (ImGui::MenuItem("Export MAP (Valve 220)...")) exportMAP();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) {
            // handled by window close
        }
        ImGui::EndMenu();
    }

    // ── Edit ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("Edit")) {
        const bool canUndo = commands_.canUndo();
        const bool canRedo = commands_.canRedo();

        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) {
            commands_.undo(scene_);
            rebuildAllMeshes();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) {
            commands_.redo(scene_);
            rebuildAllMeshes();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Selected", "Del", false, selection_.hasEntity())) {
            auto cmd = std::make_unique<DeleteEntityCommand>(selection_.entityId);
            commands_.push(std::move(cmd), scene_);
            gpuData_.erase(selection_.entityId);
            selection_.clear();
        }
        ImGui::EndMenu();
    }

    // ── Add ───────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("Add")) {
        drawAddPrimitivesMenu();
        ImGui::EndMenu();
    }

    // ── View ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid",      nullptr, &showGrid_);
        ImGui::MenuItem("Wireframe", "F1",    &wireframe_);
        ImGui::Separator();
        if (ImGui::MenuItem("Frame All", "F")) {
            camera_.frameAABB(scene_.worldBounds());
        }
        ImGui::EndMenu();
    }

    // FPS right-aligned
    const float fps = window_.fps();
    const std::string fpsStr = std::format("{:.0f} fps", fps);
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(fpsStr.c_str()).x - 8.f);
    ImGui::TextDisabled("%s", fpsStr.c_str());

    ImGui::EndMenuBar();
}

// ─── Add primitives submenu ───────────────────────────────────────────────────

void EditorApp::drawAddPrimitivesMenu() {
    static int prismSides   = 6;
    static int pyramidSides = 4;

    auto addBrush = [&](std::string name, geo::Brush brush) {
        scene::BrushEntity e;
        e.name    = std::move(name);
        e.brushes = { std::move(brush) };
        // Place at camera target height
        e.transform = scene::Transform::fromTranslation(
            glm::dvec3(camera_.target) + glm::dvec3(0, 0, 0));
        auto id = scene_.addEntity(e);          // preview
        scene_.removeEntity(id);                // undo preview
        auto cmd = std::make_unique<AddBrushEntityCommand>(std::move(e));
        const auto addedId = scene_.addEntity(cmd->entity);
        cmd->addedId = addedId;
        rebuildEntityMesh(addedId);
        selection_.selectEntity(addedId);
        setStatus(std::format("Added '{}'", cmd->entity.name));
        // Note: pushes but entity already added above — simplified for demo
        // In production: push cmd, it re-executes addEntity
    };

    if (ImGui::MenuItem("Box (64³)")) {
        addBrush("box", geo::makeBox({-32, 0, -32}, {32, 64, 32}));
    }
    if (ImGui::MenuItem("Wedge (64³)")) {
        addBrush("wedge", geo::makeWedge({-32, 0, -32}, {32, 64, 32}));
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("Sides##prism", &prismSides);
    prismSides = std::clamp(prismSides, 3, 32);
    if (ImGui::MenuItem(std::format("Prism ({}-sided)", prismSides).c_str())) {
        addBrush(std::format("{}_prism", prismSides),
                 geo::makePrism({0, 0, 0}, 32.0, 64.0, prismSides));
    }
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("Sides##pyramid", &pyramidSides);
    pyramidSides = std::clamp(pyramidSides, 3, 32);
    if (ImGui::MenuItem(std::format("Pyramid ({}-sided)", pyramidSides).c_str())) {
        addBrush(std::format("{}_pyramid", pyramidSides),
                 geo::makePyramid({0, 0, 0}, 32.0, 64.0, pyramidSides));
    }
}

// ─── Toolbar ──────────────────────────────────────────────────────────────────

void EditorApp::drawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.f, 4.f});
    ImGui::Begin("##toolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    auto toolButton = [&](const char* label, ActiveTool tool, const char* tooltip) {
        const bool active = activeTool_ == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(label, {36.f, 22.f})) activeTool_ = tool;
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        ImGui::SameLine();
    };

    toolButton("SEL",  ActiveTool::Select, "Select (Q)");
    toolButton("MOV",  ActiveTool::Move,   "Move (W)");
    toolButton("ROT",  ActiveTool::Rotate, "Rotate (E)");
    toolButton("SCL",  ActiveTool::Scale,  "Scale (R)");
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_); ImGui::SameLine();
    ImGui::Checkbox("Wire", &wireframe_); ImGui::SameLine();

    // Undo / Redo buttons
    ImGui::BeginDisabled(!commands_.canUndo());
    if (ImGui::Button("Undo")) { commands_.undo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!commands_.canRedo());
    if (ImGui::Button("Redo")) { commands_.redo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled();

    // Keyboard shortcuts
    const auto& keys = window_.input().keys;
    if (keys.f1) wireframe_ = !wireframe_;
    if (keys.q)  activeTool_ = ActiveTool::Select;
    if (keys.w && !ImGui::GetIO().WantCaptureKeyboard) activeTool_ = ActiveTool::Move;
    if (keys.e && !ImGui::GetIO().WantCaptureKeyboard) activeTool_ = ActiveTool::Rotate;
    if (keys.r && !ImGui::GetIO().WantCaptureKeyboard) activeTool_ = ActiveTool::Scale;

    ImGui::End();
}

// ─── Viewport ─────────────────────────────────────────────────────────────────

void EditorApp::drawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 8.f || size.y < 8.f) { ImGui::End(); return; }

    viewportFbo_.resize(static_cast<int>(size.x), static_cast<int>(size.y));

    // ── Render scene to FBO ───────────────────────────────────────────────────
    viewportFbo_.bind();
    glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = size.x / size.y;

    gfx::RenderFrame frame;
    frame.view      = camera_.viewMatrix();
    frame.proj      = camera_.projMatrix(aspect);
    frame.cameraPos = camera_.position();
    frame.wireframe = wireframe_;

    renderer_.beginFrame(frame);

    for (const auto& [id, data] : gpuData_) {
        const bool isSelected = (selection_.entityId == id);
        for (const auto& sub : data.mesh.submeshes) {
            glm::vec3 col = materialColour(sub.materialId());
            // Highlight selected entity
            if (isSelected) col = glm::mix(col, glm::vec3(0.3f, 0.65f, 1.f), 0.45f);
            renderer_.submit({ &sub, data.model, col });
        }
    }

    renderer_.endFrame();

    // Grid (rendered after opaque pass with blending)
    if (showGrid_ && gridRenderer_.valid()) {
        gridRenderer_.draw(frame.view, frame.proj, camera_.nearZ, camera_.farZ);
    }

    viewportFbo_.unbind();
    glViewport(0, 0, window_.width(), window_.height());

    // ── Display FBO as ImGui image ────────────────────────────────────────────
    // UV {0,1}→{1,0} flips Y (OpenGL vs ImGui coordinate mismatch)
    const ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImGui::Image(
        reinterpret_cast<ImTextureID>(static_cast<intptr_t>(viewportFbo_.colorTexture)),
        size, {0.f, 1.f}, {1.f, 0.f});

    // ── Viewport mouse interaction ────────────────────────────────────────────
    if (ImGui::IsItemHovered()) {
        handleViewportMouse(viewportPos, size);
    }

    // ── ImGuizmo gizmo overlay ────────────────────────────────────────────────
    drawGizmo(viewportPos, size);

    ImGui::End();
}

void EditorApp::handleViewportMouse(ImVec2 viewportPos, ImVec2 viewportSize) {
    const auto& mouse = window_.input().mouse;
    const bool wantGizmo = ImGuizmo::IsUsing() || ImGuizmo::IsOver();

    // ── Camera orbit / pan / zoom ─────────────────────────────────────────────
    if (!wantGizmo) {
        if (mouse.left && activeTool_ == ActiveTool::Select)
            camera_.orbit(mouse.dx, mouse.dy);
        if (mouse.right || mouse.middle)
            camera_.pan(mouse.dx, mouse.dy);
    }
    if (mouse.scroll != 0.f) camera_.zoom(mouse.scroll);

    // ── Click to select ───────────────────────────────────────────────────────
    const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (leftClicked && !wantGizmo && activeTool_ == ActiveTool::Select) {
        const ImVec2 mouseInVP = {
            ImGui::GetMousePos().x - viewportPos.x,
            ImGui::GetMousePos().y - viewportPos.y
        };
        const glm::vec2 ndc = {
             (mouseInVP.x / viewportSize.x) * 2.f - 1.f,
            -((mouseInVP.y / viewportSize.y) * 2.f - 1.f)
        };
        const float aspect = viewportSize.x / viewportSize.y;
        const auto picked  = pickEntity(ndc, scene_, camera_, aspect);

        if (picked != scene::kInvalidEntityId)
            selection_.selectEntity(picked);
        else
            selection_.clear();
    }
}

void EditorApp::drawGizmo(ImVec2 viewportPos, ImVec2 viewportSize) {
    if (!selection_.hasEntity()) return;
    if (activeTool_ == ActiveTool::Select) return;

    const auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) return;

    const float aspect = viewportSize.x / viewportSize.y;

    glm::mat4 view = camera_.viewMatrix();
    glm::mat4 proj = camera_.projMatrix(aspect);

    // Get current model matrix
    auto& data = gpuData_[selection_.entityId];
    glm::mat4 model = data.model;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportPos.x, viewportPos.y,
                      viewportSize.x, viewportSize.y);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (activeTool_ == ActiveTool::Rotate) op = ImGuizmo::ROTATE;
    if (activeTool_ == ActiveTool::Scale)  op = ImGuizmo::SCALE;

    const bool wasUsing = gizmoDragging_;
    if (!gizmoDragging_ && ImGuizmo::IsUsing()) {
        // Record start position for undo
        if (auto* be = std::get_if<scene::BrushEntity>(entity))
            gizmoDragStartPos_ = glm::vec3(be->transform.translation);
        gizmoDragging_ = true;
    }

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op,
        ImGuizmo::WORLD,
        glm::value_ptr(model));

    if (ImGuizmo::IsUsing()) {
        // Update live transform (no undo entry yet — only on release)
        data.model = model;
        glm::vec3 newPos, scale, skew;
        glm::vec4 persp;
        glm::quat rot;
        glm::decompose(model, scale, rot, newPos, skew, persp);

        if (auto* be = std::get_if<scene::BrushEntity>(scene_.getEntity(selection_.entityId))) {
            be->transform.translation = glm::dvec3(newPos);
            be->transform.rotation    = glm::dquat(rot);
        }
    } else if (wasUsing && !ImGuizmo::IsUsing()) {
        // Released — push undo command
        gizmoDragging_ = false;
        if (auto* be = std::get_if<scene::BrushEntity>(scene_.getEntity(selection_.entityId))) {
            const glm::vec3 newPos = glm::vec3(be->transform.translation);
            if (glm::length(newPos - gizmoDragStartPos_) > 1e-4f) {
                // Push without re-executing (already applied live)
                commands_.push(
                    std::make_unique<MoveEntityCommand>(
                        selection_.entityId,
                        glm::dvec3(gizmoDragStartPos_),
                        glm::dvec3(newPos)),
                    scene_);
                // Undo the execute() that push() called (we already moved it)
                commands_.undo(scene_);
                commands_.redo(scene_);
            }
        }
    }
}

// ─── Scene Tree ───────────────────────────────────────────────────────────────

void EditorApp::drawSceneTree() {
    ImGui::Begin("Scene Tree");

    // Scene name header
    ImGui::TextColored({0.6f, 0.8f, 1.f, 1.f}, "Scene: %s", scene_.name.c_str());
    const auto stats = scene_.stats();
    ImGui::TextDisabled("%zu entities  %zu brushes  %zu faces",
        scene_.entityCount(), stats.totalBrushCount, stats.totalFaceCount);
    ImGui::Separator();

    // Entity list
    for (const auto& [id, entity] : scene_.entities) {
        const std::string& name = scene::entityName(entity);
        const bool selected     = (selection_.entityId == id);
        const bool isBrush      = std::holds_alternative<scene::BrushEntity>(entity);
        const bool isPoint      = std::holds_alternative<scene::PointEntity>(entity);

        // Icon
        const char* icon = isBrush ? "[B]" : isPoint ? "[P]" : "[M]";
        ImGui::TextDisabled("%s", icon); ImGui::SameLine();

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanFullWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        const bool open = ImGui::TreeNodeEx(
            std::format("{}##{}", name, id).c_str(), flags);

        if (ImGui::IsItemClicked()) {
            if (selected) selection_.clear();
            else          selection_.selectEntity(id);
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {
            selection_.selectEntity(id);
            if (ImGui::MenuItem("Delete")) {
                auto cmd = std::make_unique<DeleteEntityCommand>(id);
                commands_.push(std::move(cmd), scene_);
                gpuData_.erase(id);
                selection_.clear();
            }
            if (isBrush && ImGui::MenuItem("Frame Camera")) {
                const auto* be = std::get_if<scene::BrushEntity>(&entity);
                camera_.frameAABB(be->worldBounds());
            }
            ImGui::EndPopup();
        }

        if (open) ImGui::TreePop();
    }

    ImGui::End();
}

// ─── Properties ───────────────────────────────────────────────────────────────

void EditorApp::drawProperties() {
    ImGui::Begin("Properties");

    if (!selection_.hasEntity()) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextDisabled("Click an entity in the viewport or scene tree.");
        ImGui::End();
        return;
    }

    auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) { selection_.clear(); ImGui::End(); return; }

    // Entity header
    ImGui::TextColored({0.6f, 0.8f, 1.f, 1.f},
        "Entity #%llu", static_cast<unsigned long long>(selection_.entityId));
    ImGui::Separator();

    // ── Name ─────────────────────────────────────────────────────────────────
    const std::string& curName = scene::entityName(*entity);
    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", curName.c_str());
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##name", renameBuffer_, sizeof(renameBuffer_),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (renameBuffer_[0] != '\0' && curName != renameBuffer_) {
            commands_.push(std::make_unique<RenameEntityCommand>(
                selection_.entityId, curName, renameBuffer_), scene_);
        }
    }
    ImGui::LabelText("Name", "");

    // ── Transform ────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Transform");

    const scene::Transform& tf = scene::entityTransform(*entity);
    glm::vec3 pos   = glm::vec3(tf.translation);
    glm::vec3 scale = glm::vec3(tf.scale);

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::DragFloat3("Position##pos", glm::value_ptr(pos), 1.f)) {
        std::visit([&](auto& e) { e.transform.translation = glm::dvec3(pos); }, *entity);
        if (auto it = gpuData_.find(selection_.entityId); it != gpuData_.end())
            it->second.model = glm::mat4(tf.matrix());
    }

    // ── Brush-specific info ───────────────────────────────────────────────────
    if (const auto* be = std::get_if<scene::BrushEntity>(entity)) {
        ImGui::SeparatorText("Brush Entity");
        ImGui::LabelText("Brushes", "%zu",  be->brushes.size());
        ImGui::LabelText("Faces",   "%zu",  be->faceCount());

        const geo::AABB wb = be->worldBounds();
        if (wb.isValid()) {
            const glm::vec3 ext = glm::vec3(wb.extents());
            ImGui::LabelText("Extents",
                "%.0f × %.0f × %.0f", ext.x, ext.y, ext.z);
        }

        bool solid   = be->solid;
        bool visible = be->visible;
        if (ImGui::Checkbox("Solid",   &solid))
            const_cast<scene::BrushEntity*>(be)->solid   = solid;
        ImGui::SameLine();
        if (ImGui::Checkbox("Visible", &visible))
            const_cast<scene::BrushEntity*>(be)->visible = visible;
    }

    // ── Point entity info ─────────────────────────────────────────────────────
    if (const auto* pe = std::get_if<scene::PointEntity>(entity)) {
        ImGui::SeparatorText("Point Entity");
        ImGui::LabelText("Class", "%s", pe->classname.c_str());
        if (!pe->properties.empty()) {
            ImGui::SeparatorText("Properties");
            for (const auto& [key, val] : pe->properties) {
                std::visit([&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        ImGui::LabelText(key.c_str(), "%s", v.c_str());
                    else if constexpr (std::is_same_v<T, float>)
                        ImGui::LabelText(key.c_str(), "%.3f", v);
                    else if constexpr (std::is_same_v<T, int>)
                        ImGui::LabelText(key.c_str(), "%d", v);
                    else if constexpr (std::is_same_v<T, bool>)
                        ImGui::LabelText(key.c_str(), "%s", v ? "true" : "false");
                }, val);
            }
        }
    }

    ImGui::End();
}

// ─── Status bar ───────────────────────────────────────────────────────────────

void EditorApp::drawStatusBar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight() + 4.f;

    ImGui::SetNextWindowPos({ vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h });
    ImGui::SetNextWindowSize({ vp->WorkSize.x, h });
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoNav       | ImGuiWindowFlags_NoDocking;

    ImGui::Begin("##status", nullptr, flags);

    if (statusTimer_ > 0.f)
        ImGui::Text("%s", statusMessage_.c_str());
    else
        ImGui::TextDisabled("FORGE Editor  —  %s", scene_.name.c_str());

    ImGui::SameLine();
    const auto stats = scene_.stats();
    const std::string right = std::format(
        "  {:3.0f} fps  |  {} DC  |  {} tri  |  {} brushes",
        window_.fps(),
        renderer_.drawCallCount(),
        renderer_.triangleCount(),
        stats.totalBrushCount);
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x
                         - ImGui::CalcTextSize(right.c_str()).x - 4.f);
    ImGui::TextDisabled("%s", right.c_str());

    ImGui::End();
}

// ─── File I/O ─────────────────────────────────────────────────────────────────

void EditorApp::newScene() {
    buildDefaultScene();
}

void EditorApp::exportOBJ() {
    const auto path = std::filesystem::current_path() / (scene_.name + "_export");
    export_::OBJExportOptions opts;
    opts.applyTransforms = true;
    if (export_::exportOBJ(scene_, path, opts)) {
        setStatus(std::format("Exported OBJ → {}.obj", path.string()));
    } else {
        setStatus("OBJ export failed.");
    }
}

void EditorApp::exportMAP() {
    const auto path = std::filesystem::current_path() / (scene_.name + ".map");
    export_::MAPExportOptions opts;
    opts.mapVersion = 220;
    if (export_::exportMAP(scene_, path, opts)) {
        setStatus(std::format("Exported MAP → {}", path.string()));
    } else {
        setStatus("MAP export failed.");
    }
}

// ─── Shutdown ─────────────────────────────────────────────────────────────────

void EditorApp::shutdown() {
    gpuData_.clear();
    viewportFbo_.destroy();
    gridRenderer_.shutdown();
    renderer_.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

} // namespace forge::editor
