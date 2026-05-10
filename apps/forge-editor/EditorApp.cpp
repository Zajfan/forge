#include "EditorApp.hpp"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <array>
#include <format>
#include <iostream>
#include <forge/export/GLTFExporter.hpp>
#include <filesystem>
#include <functional>

namespace forge::editor {

// ─── Colour palette ──────────────────────────────────────────────────────────

static const std::array<glm::vec3, 8> kPalette = {{
    { 0.72f, 0.70f, 0.65f }, { 0.55f, 0.50f, 0.45f },
    { 0.80f, 0.75f, 0.60f }, { 0.40f, 0.45f, 0.55f },
    { 0.65f, 0.58f, 0.50f }, { 0.50f, 0.60f, 0.50f },
    { 0.60f, 0.50f, 0.55f }, { 0.45f, 0.55f, 0.65f },
}};

glm::vec3 EditorApp::materialColour(const std::string& id) const noexcept {
    return kPalette[std::hash<std::string>{}(id) % kPalette.size()];
}

void EditorApp::setStatus(std::string msg, float dur) {
    statusMessage_ = std::move(msg);
    statusTimer_   = dur;
}

// ─── World-to-screen helper ───────────────────────────────────────────────────

static ImVec2 w2s(glm::vec3 wp, const glm::mat4& vp,
                   ImVec2 pos, ImVec2 sz) noexcept {
    const glm::vec4 c = vp * glm::vec4(wp, 1.f);
    if (c.w < 1e-4f) return { -1e6f, -1e6f };
    const glm::vec3 n = glm::vec3(c) / c.w;
    return { pos.x + (n.x * .5f + .5f) * sz.x,
             pos.y + (1.f - (n.y * .5f + .5f)) * sz.y };
}

// ─── Init ─────────────────────────────────────────────────────────────────────

bool EditorApp::init() {
    auto wr = gfx::Window::create({ .title="FORGE Editor",.width=1440,.height=900,.msaaSamples=4,.glMajor=4,.glMinor=6 });
    if (!wr) { std::cerr << "[error] " << wr.error() << '\n'; return false; }
    window_ = std::move(*wr);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    auto& st = ImGui::GetStyle();
    st.WindowRounding = 4.f; st.FrameRounding = 3.f; st.TabRounding = 3.f;
    st.Colors[ImGuiCol_WindowBg]  = {0.12f,0.12f,0.13f,1.f};
    st.Colors[ImGuiCol_MenuBarBg] = {0.10f,0.10f,0.11f,1.f};
    st.Colors[ImGuiCol_Header]    = {0.22f,0.40f,0.65f,0.6f};
    ImGui_ImplSDL3_InitForOpenGL(window_.sdlWindow(), window_.glContext());
    ImGui_ImplOpenGL3_Init("#version 460");

    if (!renderer_.init()) { std::cerr << "[error] Renderer init\n"; return false; }
    gridRenderer_.init();
    viewportFbo_ = gfx::Framebuffer::create(1024, 600);
    buildDefaultScene();
    return true;
}

// ─── Default scene ────────────────────────────────────────────────────────────

void EditorApp::buildDefaultScene() {
    scene_ = {}; scene_.name = "untitled";
    selection_.clear(); faceSelection_.clear();
    commands_.clear(); gpuData_.clear();

    auto add = [&](scene::BrushEntity e) {
        auto id = scene_.addEntity(std::move(e)); rebuildEntityMesh(id); };

    scene::BrushEntity floor;
    floor.name = "floor"; floor.brushes = { geo::makeBox({-256,-16,-256},{256,0,256}) };
    add(std::move(floor));

    {
        auto w = geo::makeBox({-256,0,240},{256,256,256});
        auto c = geo::makeBox({-32,0,239},{32,192,257});
        scene::BrushEntity nw; nw.name = "north_wall"; nw.brushes = geo::csgSubtract(w, c);
        add(std::move(nw));
    }
    { scene::BrushEntity e; e.name="south_wall"; e.brushes={geo::makeBox({-256,0,-256},{256,256,-240})}; add(std::move(e)); }
    { scene::BrushEntity e; e.name="east_wall";  e.brushes={geo::makeBox({240,0,-256},{256,256,256})};   add(std::move(e)); }
    { scene::BrushEntity e; e.name="west_wall";  e.brushes={geo::makeBox({-256,0,-256},{-240,256,256})}; add(std::move(e)); }
    { scene::BrushEntity e; e.name="ceiling";    e.brushes={geo::makeBox({-256,256,-256},{256,272,256})}; add(std::move(e)); }
    { scene::BrushEntity e; e.name="pillar";     e.brushes={geo::makePrism({64,0,64},24.0,256.0,8)};    add(std::move(e)); }

    { scene::PointEntity pe; pe.name="player_start"; pe.classname="info_player_start";
      pe.transform=scene::Transform::fromTranslation({0,32,0}); scene_.addEntity(std::move(pe)); }

    camera_.frameAABB(scene_.worldBounds());
    setStatus("Default scene loaded");
}

// ─── Mesh management ─────────────────────────────────────────────────────────

void EditorApp::rebuildEntityMesh(scene::EntityId id) {
    auto* e = scene_.getEntity(id);
    if (!e) { gpuData_.erase(id); return; }
    auto* be = std::get_if<scene::BrushEntity>(e);
    if (!be) { gpuData_.erase(id); return; }
    EntityGPUData d;
    d.model = glm::mat4(be->transform.matrix());
    d.mesh  = gfx::GPUEntityMesh::upload(build::buildEntityMesh(*be, false));
    gpuData_[id] = std::move(d);
}

void EditorApp::rebuildAllMeshes() {
    gpuData_.clear();
    for (const auto& [id, _] : scene_.entities) rebuildEntityMesh(id);
}

// ─── Run loop ─────────────────────────────────────────────────────────────────

void EditorApp::run() {
    while (!window_.shouldClose()) {
        // Route to play mode frame or editor frame
        if (playMode_) { runPlayFrame(); continue; }

        window_.pollEvents();
        statusTimer_ -= window_.deltaTime();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        drawFrame();
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_.width(), window_.height());
        glClearColor(0.08f, 0.08f, 0.10f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        window_.swapBuffers();
    }
}

// ─── Frame ────────────────────────────────────────────────────────────────────

void EditorApp::drawFrame() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("##host", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus|
        ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    drawMainMenuBar();
    const ImGuiID dock = ImGui::GetID("ForgeDock");
    ImGui::DockSpace(dock, {0.f,0.f});
    if (!layoutReady_) setupDefaultDockLayout(dock);
    ImGui::End();

    drawToolbar();
    drawViewport();
    drawSceneTree();
    drawProperties();
    drawStatusBar();
}

// ─── Dock layout ─────────────────────────────────────────────────────────────

void EditorApp::setupDefaultDockLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID right, left;
    ImGui::DockBuilderSplitNode(dock,  ImGuiDir_Right, 0.24f, &right, &left);
    ImGuiID rtop, rbot;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down,  0.55f, &rbot,  &rtop);
    ImGuiID tbar, vp;
    ImGui::DockBuilderSplitNode(left,  ImGuiDir_Up,    0.045f, &tbar, &vp);
    ImGui::DockBuilderDockWindow("Viewport",   vp);
    ImGui::DockBuilderDockWindow("##toolbar",  tbar);
    ImGui::DockBuilderDockWindow("Scene Tree", rtop);
    ImGui::DockBuilderDockWindow("Properties", rbot);
    ImGui::DockBuilderFinish(dock);
    layoutReady_ = true;
}

// ─── Menu bar ─────────────────────────────────────────────────────────────────

void EditorApp::drawMainMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene",  "Ctrl+N"))    newScene();
        if (ImGui::MenuItem("Open Scene...","Ctrl+O"))  openScene();
        ImGui::Separator();
        if (ImGui::MenuItem("Save",          "Ctrl+S")) saveScene();
        if (ImGui::MenuItem("Save As..."))               saveSceneAs();
        ImGui::Separator();
        if (ImGui::BeginMenu("Export")) {
            if (ImGui::MenuItem("OBJ..."))               exportOBJ();
            if (ImGui::MenuItem("MAP (Valve 220)..."))   exportMAP();
            if (ImGui::MenuItem("GLTF 2.0 / GLB..."))   exportGLTF();
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo","Ctrl+Z",false,commands_.canUndo())) {
            commands_.undo(scene_); rebuildAllMeshes();
        }
        if (ImGui::MenuItem("Redo","Ctrl+Y",false,commands_.canRedo())) {
            commands_.redo(scene_); rebuildAllMeshes();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Selected","Del",false,selection_.hasEntity())) {
            commands_.push(std::make_unique<DeleteEntityCommand>(selection_.entityId), scene_);
            gpuData_.erase(selection_.entityId); selection_.clear(); faceSelection_.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("CSG Subtract (selection=cutter)",nullptr,false,selection_.hasEntity()))
            applyCSGSubtract();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add")) { drawAddPrimitivesMenu(); ImGui::EndMenu(); }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid",      nullptr, &showGrid_);
        ImGui::MenuItem("Wireframe", "F1",    &wireframe_);
        ImGui::Separator();
        if (ImGui::MenuItem("Frame All","F")) camera_.frameAABB(scene_.worldBounds());
        if (ImGui::MenuItem("Frame Selected",nullptr,false,selection_.hasEntity())) {
            if (auto* e = scene_.getEntity(selection_.entityId))
                if (auto* be = std::get_if<scene::BrushEntity>(e))
                    camera_.frameAABB(be->worldBounds());
        }
        ImGui::EndMenu();
    }

    // FPS right-aligned
    const std::string fps = std::format("{:.0f} fps", window_.fps());
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x
                         - ImGui::CalcTextSize(fps.c_str()).x - 8.f);
    ImGui::TextDisabled("%s", fps.c_str());
    ImGui::EndMenuBar();
}

// ─── Add primitives submenu ───────────────────────────────────────────────────

void EditorApp::drawAddPrimitivesMenu() {
    static int pSides = 6, ySides = 4;

    auto add = [&](std::string name, geo::Brush b) {
        scene::BrushEntity e;
        e.name    = std::move(name);
        e.brushes = { std::move(b) };
        e.transform = scene::Transform::fromTranslation(glm::dvec3(camera_.target));
        auto id = scene_.addEntity(std::move(e));
        rebuildEntityMesh(id);
        selection_.selectEntity(id);
        faceSelection_.clear();
        setStatus(std::format("Added '{}'", scene::entityName(*scene_.getEntity(id))));
    };

    if (ImGui::MenuItem("Box (64³)"))
        add("box",    geo::makeBox({-32,0,-32},{32,64,32}));
    if (ImGui::MenuItem("Wedge (64³)"))
        add("wedge",  geo::makeWedge({-32,0,-32},{32,64,32}));
    ImGui::Separator();
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##ps",&pSides); pSides=std::clamp(pSides,3,32);
    ImGui::SameLine();
    if (ImGui::MenuItem(std::format("{}-Prism",pSides).c_str()))
        add(std::format("{}_prism",pSides), geo::makePrism({0,0,0},32.,64.,pSides));
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##ys",&ySides); ySides=std::clamp(ySides,3,32);
    ImGui::SameLine();
    if (ImGui::MenuItem(std::format("{}-Pyramid",ySides).c_str()))
        add(std::format("{}_pyramid",ySides), geo::makePyramid({0,0,0},32.,64.,ySides));
}

// ─── Toolbar ──────────────────────────────────────────────────────────────────

void EditorApp::drawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4.f,4.f});
    ImGui::Begin("##toolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    auto btn = [&](const char* lbl, ActiveTool t, const char* tip) {
        const bool on = activeTool_ == t;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(lbl,{38.f,22.f})) {
            activeTool_ = t;
            if (t != ActiveTool::FaceSelect && t != ActiveTool::FaceMove && t != ActiveTool::Paint)
                faceSelection_.clear();
            if (t != ActiveTool::VertexSelect)
                vertexSelection_.clear();
        }
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tip);
        ImGui::SameLine();
    };

    // Entity tools
    btn("SEL",  ActiveTool::Select,     "Select entity (Q)");
    btn("MOV",  ActiveTool::Move,       "Move entity (W)");
    btn("ROT",  ActiveTool::Rotate,     "Rotate entity (E)");
    btn("SCL",  ActiveTool::Scale,      "Scale entity (R)");

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();

    // Face / geometry tools
    btn("FSEL", ActiveTool::FaceSelect, "Select face (T)");
    btn("FMOV", ActiveTool::FaceMove,   "Move face along normal (Y)");
    btn("CLIP", ActiveTool::Clip,       "Clip brush with plane (C)");
    btn("PAINT",ActiveTool::Paint,      "Paint material (P)");
    btn("VTEX", ActiveTool::VertexSelect,"Select vertex (V)");

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ImGui::Checkbox("Grid",&showGrid_); ImGui::SameLine();
    ImGui::Checkbox("Wire",&wireframe_); ImGui::SameLine();

    ImGui::BeginDisabled(!commands_.canUndo());
    if (ImGui::Button("Undo")) { commands_.undo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!commands_.canRedo());
    if (ImGui::Button("Redo")) { commands_.redo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled();

    // Play / Stop
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, {0.15f, 0.55f, 0.15f, 1.f});
    if (ImGui::Button(playMode_ ? "  STOP  " : "  PLAY  ", {80.f, 22.f})) {
        if (!playMode_) enterPlayMode();
        else            exitPlayMode();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(playMode_ ? "Stop (ESC)" : "Enter play mode (F5)");
    if (!playMode_ && window_.input().keys.f5) enterPlayMode();

    // Keyboard shortcuts
    const auto& k = window_.input().keys;
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (k.f1) wireframe_ = !wireframe_;
        if (k.q)  activeTool_ = ActiveTool::Select;
        if (k.w)  activeTool_ = ActiveTool::Move;
        if (k.e)  activeTool_ = ActiveTool::Rotate;
        if (k.r)  activeTool_ = ActiveTool::Scale;
    }
    ImGui::End();
}

// ─── Viewport ─────────────────────────────────────────────────────────────────

void EditorApp::drawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    const ImVec2 sz = ImGui::GetContentRegionAvail();
    if (sz.x < 8.f || sz.y < 8.f) { ImGui::End(); return; }

    viewportFbo_.resize((int)sz.x, (int)sz.y);

    // ── Render scene to FBO ───────────────────────────────────────────────────
    viewportFbo_.bind();
    glClearColor(0.10f,0.10f,0.12f,1.f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    const float aspect = sz.x / sz.y;
    gfx::RenderFrame frame;
    frame.view      = camera_.viewMatrix();
    frame.proj      = camera_.projMatrix(aspect);
    frame.cameraPos = camera_.position();
    frame.wireframe = wireframe_;

    // Collect point lights from scene
    {
        auto lights = scene_.findAllByClassname("light");
        for (auto& [lid, pe] : lights) {
            gfx::PointLight pl;
            pl.position  = glm::vec3(pe->transform.translation);
            pl.intensity = pe->property<float>("light", 300.f);
            const auto col = pe->property<glm::vec3>("_color", glm::vec3{1.f,0.95f,0.8f});
            pl.color     = col;
            pl.radius    = pe->property<float>("radius", 512.f);
            frame.pointLights.push_back(pl);
        }
    }

    renderer_.beginFrame(frame);
    for (const auto& [id, data] : gpuData_) {
        const bool sel = (selection_.entityId == id);
        for (const auto& sub : data.mesh.submeshes) {
            glm::vec3 col = materialColour(sub.materialId());
            if (sel) col = glm::mix(col, glm::vec3(0.3f,0.65f,1.f), 0.4f);
            renderer_.submit({ &sub, data.model, col });
        }
    }
    renderer_.endFrame();

    if (showGrid_ && gridRenderer_.valid())
        gridRenderer_.draw(frame.view, frame.proj, camera_.nearZ, camera_.farZ);

    viewportFbo_.unbind();
    glViewport(0, 0, window_.width(), window_.height());

    // ── Display FBO ───────────────────────────────────────────────────────────
    const ImVec2 vpPos = ImGui::GetCursorScreenPos();
    ImGui::Image(
        reinterpret_cast<ImTextureID>(static_cast<intptr_t>(viewportFbo_.colorTexture)),
        sz, {0.f,1.f}, {1.f,0.f});

    // ── ImGui overlays drawn on top of the image ──────────────────────────────
    const glm::mat4 vp = frame.proj * frame.view;
    if (faceSelection_.valid())
        drawFaceOverlay(vpPos, sz, vp);
    if (vertexSelection_.valid())
        drawVertexOverlay(vpPos, sz, vp);
    if (activeTool_ == ActiveTool::Clip && selection_.hasEntity())
        drawClipPreview(vpPos, sz, vp);

    // ── Mouse input ───────────────────────────────────────────────────────────
    if (ImGui::IsItemHovered())
        handleViewportMouse(vpPos, sz);

    // ── Gizmo ────────────────────────────────────────────────────────────────
    drawGizmo(vpPos, sz);

    ImGui::End();
}

// ─── Face overlay ─────────────────────────────────────────────────────────────

void EditorApp::drawFaceOverlay(ImVec2 pos, ImVec2 sz, const glm::mat4& vp) {
    auto* entity = scene_.getEntity(faceSelection_.entityId);
    if (!entity) return;
    auto* be = std::get_if<scene::BrushEntity>(entity);
    if (!be || faceSelection_.brushIdx >= be->brushes.size()) return;

    const auto& brush = be->brushes[faceSelection_.brushIdx];
    if (faceSelection_.faceIdx >= brush.faces.size()) return;

    const auto& poly = brush.facePolygon(faceSelection_.faceIdx);
    if (poly.size() < 3) return;

    auto* dl = ImGui::GetWindowDrawList();

    // Build screen-space polygon
    std::vector<ImVec2> pts;
    pts.reserve(poly.size());
    for (const auto& lp : poly) {
        const glm::vec3 wp = glm::vec3(be->transform.transformPoint(lp));
        pts.push_back(w2s(wp, vp, pos, sz));
    }

    // Filled (semi-transparent highlight)
    dl->AddConvexPolyFilled(pts.data(), (int)pts.size(),
                             IM_COL32(80,180,255,45));

    // Outline
    for (std::size_t i = 0; i < pts.size(); ++i) {
        dl->AddLine(pts[i], pts[(i+1) % pts.size()],
                    IM_COL32(100,210,255,230), 2.f);
    }

    // Face normal arrow (centre → centre + normal*24)
    const glm::vec3 centre = [&] {
        glm::dvec3 c{};
        for (const auto& lp : poly) c += lp;
        return glm::vec3(be->transform.transformPoint(c / double(poly.size())));
    }();
    const glm::vec3 tip = centre
        + glm::vec3(be->transform.transformNormal(brush.faces[faceSelection_.faceIdx].plane.normal))
        * 24.f;

    dl->AddLine(w2s(centre, vp, pos, sz),
                w2s(tip,    vp, pos, sz),
                IM_COL32(255,220,50,200), 1.5f);

    // Label
    const auto labelPos = w2s(tip, vp, pos, sz);
    if (labelPos.x > -1e5f)
        dl->AddText(labelPos, IM_COL32(255,220,50,200),
                    brush.faces[faceSelection_.faceIdx].materialId.c_str());
}

// ─── Clip preview ─────────────────────────────────────────────────────────────

void EditorApp::drawClipPreview(ImVec2 pos, ImVec2 sz, const glm::mat4& vp) {
    const float p = clipState_.position;
    const float H = 300.f; // half-extent

    std::array<glm::vec3, 4> corners;
    switch (clipState_.axis) {
    case ClipAxis::X:
        corners = {{ {p,-H,-H},{p, H,-H},{p, H, H},{p,-H, H} }}; break;
    case ClipAxis::Y:
        corners = {{ {-H,p,-H},{ H,p,-H},{ H,p, H},{-H,p, H} }}; break;
    case ClipAxis::Z:
        corners = {{ {-H,-H,p},{ H,-H,p},{ H, H,p},{-H, H,p} }}; break;
    }

    std::array<ImVec2, 4> pts;
    for (int i = 0; i < 4; ++i) pts[i] = w2s(corners[i], vp, pos, sz);

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddConvexPolyFilled(pts.data(), 4, IM_COL32(255,160,50,35));
    for (int i = 0; i < 4; ++i)
        dl->AddLine(pts[i], pts[(i+1)%4], IM_COL32(255,160,50,200), 1.5f);
}

// ─── Viewport mouse ──────────────────────────────────────────────────────────

void EditorApp::handleViewportMouse(ImVec2 vpPos, ImVec2 vpSz) {
    const auto& m = window_.input().mouse;
    const bool gizmoOver = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    const float aspect   = vpSz.x / vpSz.y;

    // Camera orbit / pan / zoom
    if (!gizmoOver) {
        const bool entityTool =
            activeTool_ == ActiveTool::Select ||
            activeTool_ == ActiveTool::FaceSelect ||
            activeTool_ == ActiveTool::Clip;
        if (m.left && entityTool)   camera_.orbit(m.dx, m.dy);
        if (m.right || m.middle)    camera_.pan  (m.dx, m.dy);
    }
    if (m.scroll != 0.f) camera_.zoom(m.scroll);

    // Click handling
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver) {
        const ImVec2 rel = { ImGui::GetMousePos().x - vpPos.x,
                             ImGui::GetMousePos().y - vpPos.y };
        const glm::vec2 ndc = {
             (rel.x / vpSz.x) * 2.f - 1.f,
            -((rel.y / vpSz.y) * 2.f - 1.f)
        };

        switch (activeTool_) {
        case ActiveTool::Select: {
            const auto id = pickEntity(ndc, scene_, camera_, aspect);
            if (id != scene::kInvalidEntityId) { selection_.selectEntity(id); faceSelection_.clear(); }
            else                               { selection_.clear(); faceSelection_.clear(); }
            break;
        }
        case ActiveTool::FaceSelect:
        case ActiveTool::FaceMove:
        case ActiveTool::Paint: {
            const auto hit = pickFace(ndc, scene_, camera_, aspect);
            if (hit) {
                faceSelection_.select(hit->entityId, hit->brushIdx, hit->faceIdx);
                selection_.selectEntity(hit->entityId);
                // In paint mode, immediately set the paint material
                if (activeTool_ == ActiveTool::Paint) {
                    auto* e = scene_.getEntity(hit->entityId);
                    auto* be = std::get_if<scene::BrushEntity>(e);
                    if (be) {
                        const std::string& old = be->brushes[hit->brushIdx].faces[hit->faceIdx].materialId;
                        if (old != paintMaterial_) {
                            commands_.push(std::make_unique<SetFaceMaterialCommand>(
                                hit->entityId, hit->brushIdx, hit->faceIdx,
                                old, paintMaterial_), scene_);
                            rebuildEntityMesh(hit->entityId);
                            setStatus(std::format("Painted '{}' → {}", old, paintMaterial_));
                        }
                    }
                }
            } else {
                faceSelection_.clear();
            }
            break;
        }
        case ActiveTool::VertexSelect: {
            // Pick closest vertex to click ray
            const auto hit = pickFace(ndc, scene_, camera_, aspect);
            if (hit) {
                // From the hit face, find closest vertex to the actual hit point
                auto* e = scene_.getEntity(hit->entityId);
                auto* be = std::get_if<scene::BrushEntity>(e);
                if (be && hit->brushIdx < be->brushes.size()) {
                    const auto& verts = be->brushes[hit->brushIdx].vertices();
                    glm::dvec3 best; float bestD = 1e30f;
                    for (const auto& v : verts) {
                        const glm::vec3 wp = glm::vec3(be->transform.transformPoint(v));
                        const float d = glm::length(wp - hit->point);
                        if (d < bestD) { bestD = d; best = v; }
                    }
                    if (bestD < 1e29f) {
                        vertexSelection_ = { hit->entityId, hit->brushIdx,
                            be->transform.transformPoint(best) };
                        selection_.selectEntity(hit->entityId);
                    }
                }
            } else {
                vertexSelection_.clear();
            }
            break;
        }
        default: {
            const auto id = pickEntity(ndc, scene_, camera_, aspect);
            if (id != scene::kInvalidEntityId) selection_.selectEntity(id);
            else                               selection_.clear();
            faceSelection_.clear();
            vertexSelection_.clear();
            break;
        }
        }
    }
}

// ─── Gizmo ───────────────────────────────────────────────────────────────────

void EditorApp::drawGizmo(ImVec2 vpPos, ImVec2 vpSz) {
    if (!selection_.hasEntity()) return;
    if (activeTool_ == ActiveTool::Select ||
        activeTool_ == ActiveTool::FaceSelect ||
        activeTool_ == ActiveTool::FaceMove ||
        activeTool_ == ActiveTool::Clip ||
        activeTool_ == ActiveTool::Paint) return;

    auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) return;

    const float aspect = vpSz.x / vpSz.y;
    glm::mat4 view = camera_.viewMatrix();
    glm::mat4 proj = camera_.projMatrix(aspect);
    glm::mat4 model = gpuData_.count(selection_.entityId)
                    ? gpuData_[selection_.entityId].model
                    : glm::mat4(1.f);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(vpPos.x, vpPos.y, vpSz.x, vpSz.y);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (activeTool_ == ActiveTool::Rotate) op = ImGuizmo::ROTATE;
    if (activeTool_ == ActiveTool::Scale)  op = ImGuizmo::SCALE;

    const bool wasUsing = gizmoDragging_;
    if (!gizmoDragging_ && ImGuizmo::IsUsing()) {
        if (auto* be = std::get_if<scene::BrushEntity>(entity))
            gizmoDragStartPos_ = glm::vec3(be->transform.translation);
        gizmoDragging_ = true;
    }

    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                         op, ImGuizmo::WORLD, glm::value_ptr(model));

    if (ImGuizmo::IsUsing()) {
        if (auto it = gpuData_.find(selection_.entityId); it != gpuData_.end())
            it->second.model = model;
        glm::vec3 pos, scl, skw; glm::vec4 psp; glm::quat rot;
        glm::decompose(model, scl, rot, pos, skw, psp);
        if (auto* be = std::get_if<scene::BrushEntity>(entity)) {
            be->transform.translation = glm::dvec3(pos);
            be->transform.rotation    = glm::dquat(rot);
        }
    } else if (wasUsing && !ImGuizmo::IsUsing()) {
        gizmoDragging_ = false;
        if (auto* be = std::get_if<scene::BrushEntity>(entity)) {
            const glm::vec3 newPos = glm::vec3(be->transform.translation);
            if (glm::length(newPos - gizmoDragStartPos_) > 1e-4f) {
                // Re-push via undo/redo to record correctly
                commands_.push(std::make_unique<MoveEntityCommand>(
                    selection_.entityId,
                    glm::dvec3(gizmoDragStartPos_),
                    glm::dvec3(newPos)), scene_);
                commands_.undo(scene_); commands_.redo(scene_);
            }
        }
    }
}

// ─── Scene Tree ───────────────────────────────────────────────────────────────

void EditorApp::drawSceneTree() {
    ImGui::Begin("Scene Tree");
    ImGui::TextColored({0.6f,0.8f,1.f,1.f}, "Scene: %s", scene_.name.c_str());
    const auto st = scene_.stats();
    ImGui::TextDisabled("%zu entities  %zu brushes",
        scene_.entityCount(), st.totalBrushCount);
    ImGui::Separator();

    for (const auto& [id, entity] : scene_.entities) {
        const bool sel  = (selection_.entityId == id);
        const bool isBE = std::holds_alternative<scene::BrushEntity>(entity);
        const bool isPE = std::holds_alternative<scene::PointEntity>(entity);
        const char* ico = isBE ? "[B]" : isPE ? "[P]" : "[M]";
        ImGui::TextDisabled("%s",ico); ImGui::SameLine();
        ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_SpanFullWidth;
        if (sel) fl |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(
            std::format("{}##{}", scene::entityName(entity), id).c_str(), fl);
        if (ImGui::IsItemClicked()) {
            if (sel) { selection_.clear(); faceSelection_.clear(); }
            else     { selection_.selectEntity(id); faceSelection_.clear(); }
        }
        if (ImGui::BeginPopupContextItem()) {
            selection_.selectEntity(id);
            if (ImGui::MenuItem("Delete")) {
                commands_.push(std::make_unique<DeleteEntityCommand>(id), scene_);
                gpuData_.erase(id); selection_.clear(); faceSelection_.clear();
            }
            if (isBE && ImGui::MenuItem("Frame Camera")) {
                const auto* be = std::get_if<scene::BrushEntity>(&entity);
                camera_.frameAABB(be->worldBounds());
            }
            if (isBE && ImGui::MenuItem("CSG Subtract (this=cutter)")) {
                selection_.selectEntity(id);
                applyCSGSubtract();
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

    // ── Vertex properties ────────────────────────────────────────────────────
    if (vertexSelection_.valid() && activeTool_ == ActiveTool::VertexSelect) {
        drawVertexProperties_impl(*this, scene_, vertexSelection_, commands_,
            [this](scene::EntityId id) { rebuildEntityMesh(id); });
        ImGui::Separator();
    }

    // ── Face properties (takes priority when face selected) ──────────────────
    if (faceSelection_.valid() &&
        (activeTool_ == ActiveTool::FaceSelect ||
         activeTool_ == ActiveTool::FaceMove   ||
         activeTool_ == ActiveTool::Paint))
    {
        drawFaceProperties();
        ImGui::Separator();
    }

    // ── Clip properties ───────────────────────────────────────────────────────
    if (activeTool_ == ActiveTool::Clip) {
        drawClipProperties();
        ImGui::Separator();
    }

    // ── Entity properties ─────────────────────────────────────────────────────
    if (!selection_.hasEntity()) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::End(); return;
    }

    auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) { selection_.clear(); ImGui::End(); return; }

    ImGui::TextColored({0.6f,0.8f,1.f,1.f}, "Entity #%llu",
        static_cast<unsigned long long>(selection_.entityId));
    ImGui::Separator();

    // Name
    const std::string& cur = scene::entityName(*entity);
    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", cur.c_str());
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##name", renameBuffer_, sizeof(renameBuffer_),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (renameBuffer_[0] && cur != renameBuffer_)
            commands_.push(std::make_unique<RenameEntityCommand>(
                selection_.entityId, cur, renameBuffer_), scene_);
    }

    // Transform
    ImGui::SeparatorText("Transform");
    const scene::Transform& tf = scene::entityTransform(*entity);
    glm::vec3 pos = glm::vec3(tf.translation);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 1.f)) {
        std::visit([&](auto& e) { e.transform.translation = glm::dvec3(pos); }, *entity);
        if (auto it = gpuData_.find(selection_.entityId); it != gpuData_.end())
            it->second.model = glm::mat4(tf.matrix());
    }

    // Brush entity specifics
    if (const auto* be = std::get_if<scene::BrushEntity>(entity)) {
        ImGui::SeparatorText("Brush Entity");
        ImGui::LabelText("Brushes", "%zu", be->brushes.size());
        ImGui::LabelText("Faces",   "%zu", be->faceCount());
        const geo::AABB wb = be->worldBounds();
        if (wb.isValid()) {
            const glm::vec3 e = glm::vec3(wb.extents());
            ImGui::LabelText("Extents", "%.0f × %.0f × %.0f", e.x, e.y, e.z);
        }
        bool solid = be->solid, vis = be->visible;
        if (ImGui::Checkbox("Solid",   &solid))   const_cast<scene::BrushEntity*>(be)->solid   = solid;
        ImGui::SameLine();
        if (ImGui::Checkbox("Visible", &vis))     const_cast<scene::BrushEntity*>(be)->visible = vis;

        ImGui::SeparatorText("Operations");

        // CSG Subtract
        if (ImGui::Button("CSG Subtract (this=cutter)", {-1.f, 0.f}))
            applyCSGSubtract();
        ImGui::SetItemTooltip("Carves this entity out of all overlapping solid brushes, then removes it.");

        // Hollow
        if (be->brushes.size() == 1) {
            ImGui::SetNextItemWidth(120.f);
            ImGui::DragScalar("Wall thickness", ImGuiDataType_Double,
                               &hollowThickness_, 1.0, nullptr, nullptr, "%.0f");
            ImGui::SameLine();
            if (ImGui::Button("Hollow")) applyHollow();
        } else {
            ImGui::TextDisabled("(Hollow: select single-brush entity)");
        }
    }

    // Point entity specifics
    if (const auto* pe = std::get_if<scene::PointEntity>(entity)) {
        ImGui::SeparatorText("Point Entity");
        ImGui::LabelText("Class", "%s", pe->classname.c_str());
        if (!pe->properties.empty()) {
            ImGui::SeparatorText("Properties");
            for (const auto& [k, v] : pe->properties) {
                std::visit([&](const auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T,std::string>) ImGui::LabelText(k.c_str(),"%s",val.c_str());
                    else if constexpr (std::is_same_v<T,float>)  ImGui::LabelText(k.c_str(),"%.3f",val);
                    else if constexpr (std::is_same_v<T,int>)    ImGui::LabelText(k.c_str(),"%d",val);
                    else if constexpr (std::is_same_v<T,bool>)   ImGui::LabelText(k.c_str(),"%s",val?"true":"false");
                }, v);
            }
        }
    }

    ImGui::End();
}

// ─── Face properties ─────────────────────────────────────────────────────────

void EditorApp::drawFaceProperties() {
    ImGui::SeparatorText("Selected Face");

    auto* entity = scene_.getEntity(faceSelection_.entityId);
    if (!entity) { faceSelection_.clear(); return; }
    auto* be = std::get_if<scene::BrushEntity>(entity);
    if (!be || faceSelection_.brushIdx >= be->brushes.size()) { faceSelection_.clear(); return; }

    auto& brush = be->brushes[faceSelection_.brushIdx];
    if (faceSelection_.faceIdx >= brush.faces.size()) { faceSelection_.clear(); return; }
    auto& face = brush.faces[faceSelection_.faceIdx];

    // Info
    ImGui::LabelText("Brush", "%zu / %zu", faceSelection_.brushIdx, be->brushes.size()-1);
    ImGui::LabelText("Face",  "%zu / %zu", faceSelection_.faceIdx,  brush.faces.size()-1);
    const glm::vec3 n = glm::vec3(face.plane.normal);
    ImGui::LabelText("Normal", "%.2f  %.2f  %.2f", n.x, n.y, n.z);

    // Plane distance (face move tool)
    if (activeTool_ == ActiveTool::FaceMove) {
        float dist = static_cast<float>(face.plane.distance);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat("Plane Distance", &dist, 0.5f, -4096.f, 4096.f)) {
            face.plane.distance = static_cast<double>(dist);
            brush.invalidate();
            rebuildEntityMesh(faceSelection_.entityId);
        }
        ImGui::SetItemTooltip("Moves the face along its normal. No undo — use with care.");
    }

    // Material
    ImGui::SeparatorText("Material");
    char matBuf[256];
    std::snprintf(matBuf, sizeof(matBuf), "%s", face.materialId.c_str());
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##mat", matBuf, sizeof(matBuf),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (face.materialId != matBuf) {
            commands_.push(std::make_unique<SetFaceMaterialCommand>(
                faceSelection_.entityId, faceSelection_.brushIdx, faceSelection_.faceIdx,
                face.materialId, matBuf), scene_);
            rebuildEntityMesh(faceSelection_.entityId);
            setStatus(std::format("Painted '{}'", matBuf));
        }
    }
    ImGui::LabelText("Material##lbl","");

    // UV controls
    ImGui::SeparatorText("UV Mapping");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat2("Offset", glm::value_ptr(face.uvOffset), 0.5f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat2("Scale",  glm::value_ptr(face.uvScale),  0.01f, 0.01f, 64.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat("Rotation",&face.uvRotation, 1.f, -180.f, 180.f);

    // Apply UV changes
    if (ImGui::IsItemDeactivatedAfterEdit() ||
        ImGui::IsItemActivated()) // trigger rebuild after any UV drag
    {
        rebuildEntityMesh(faceSelection_.entityId);
    }
}

// ─── Clip properties ─────────────────────────────────────────────────────────

void EditorApp::drawClipProperties() {
    ImGui::SeparatorText("Clip Tool");

    if (!selection_.hasEntity()) {
        ImGui::TextDisabled("Select an entity to clip.");
        return;
    }

    // Axis selector
    ImGui::Text("Clip axis:");
    ImGui::SameLine();
    if (ImGui::RadioButton("X", clipState_.axis == ClipAxis::X)) clipState_.axis = ClipAxis::X;
    ImGui::SameLine();
    if (ImGui::RadioButton("Y", clipState_.axis == ClipAxis::Y)) clipState_.axis = ClipAxis::Y;
    ImGui::SameLine();
    if (ImGui::RadioButton("Z", clipState_.axis == ClipAxis::Z)) clipState_.axis = ClipAxis::Z;

    // Position
    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat("Position", &clipState_.position, 1.f, -4096.f, 4096.f);

    // Keep both halves?
    ImGui::Checkbox("Keep both halves", &clipState_.keepBoth);

    ImGui::Spacing();
    if (ImGui::Button("Apply Clip", {-1.f, 0.f})) applyClip();
}

// ─── Apply: clip ─────────────────────────────────────────────────────────────

void EditorApp::applyClip() {
    auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) return;
    auto* be = std::get_if<scene::BrushEntity>(entity);
    if (!be || be->brushes.empty()) return;

    // Build the clip plane
    glm::dvec3 normal{};
    switch (clipState_.axis) {
    case ClipAxis::X: normal = {1,0,0}; break;
    case ClipAxis::Y: normal = {0,1,0}; break;
    case ClipAxis::Z: normal = {0,0,1}; break;
    }
    const geo::Plane clipPlane = geo::Plane::fromNormalPoint(
        normal, normal * static_cast<double>(clipState_.position));

    // Split all brushes
    std::vector<geo::Brush> result;
    for (const auto& brush : be->brushes) {
        auto [front, back] = geo::splitBrushByPlane(brush, clipPlane);
        if (front) result.push_back(std::move(*front));
        if (back  && clipState_.keepBoth) result.push_back(std::move(*back));
    }

    if (result.empty()) {
        setStatus("Clip produced no valid brushes.");
        return;
    }

    const std::vector<geo::Brush> origBrushes = be->brushes;
    commands_.push(std::make_unique<ClipBrushCommand>(
        selection_.entityId, origBrushes, result), scene_);

    rebuildEntityMesh(selection_.entityId);
    faceSelection_.clear();
    setStatus(std::format("Clipped → {} brush(es)", result.size()));
}

// ─── Apply: CSG subtract ─────────────────────────────────────────────────────

void EditorApp::applyCSGSubtract() {
    if (!selection_.hasEntity()) return;
    auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) return;
    auto* cutterBE = std::get_if<scene::BrushEntity>(entity);
    if (!cutterBE) return;

    auto cmd = std::make_unique<CSGSubtractCommand>(
        selection_.entityId, *cutterBE);
    commands_.push(std::move(cmd), scene_);

    selection_.clear();
    faceSelection_.clear();
    rebuildAllMeshes();
    setStatus("CSG Subtract applied.");
}

// ─── Apply: hollow ───────────────────────────────────────────────────────────

void EditorApp::applyHollow() {
    if (!selection_.hasEntity()) return;
    auto* entity = scene_.getEntity(selection_.entityId);
    if (!entity) return;
    auto* be = std::get_if<scene::BrushEntity>(entity);
    if (!be || be->brushes.size() != 1) return;

    auto cmd = std::make_unique<HollowEntityCommand>(selection_.entityId, *be);
    cmd->wallThickness = hollowThickness_;
    commands_.push(std::move(cmd), scene_);

    selection_.clear();
    faceSelection_.clear();
    rebuildAllMeshes();
    setStatus(std::format("Hollowed with thickness {:.0f}", hollowThickness_));
}

// ─── Status bar ───────────────────────────────────────────────────────────────

void EditorApp::drawStatusBar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight() + 4.f;
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h});
    ImGui::SetNextWindowSize({vp->WorkSize.x, h});
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##status", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoDocking);

    if (statusTimer_ > 0.f)
        ImGui::Text("%s", statusMessage_.c_str());
    else
        ImGui::TextDisabled("FORGE Editor  —  %s", scene_.name.c_str());

    ImGui::SameLine();
    const auto st = scene_.stats();
    const std::string r = std::format("  {:.0f} fps  |  {} DC  |  {} tri  |  {} brushes",
        window_.fps(), renderer_.drawCallCount(), renderer_.triangleCount(), st.totalBrushCount);
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x
                         - ImGui::CalcTextSize(r.c_str()).x - 4.f);
    ImGui::TextDisabled("%s", r.c_str());
    ImGui::End();
}

// ─── File I/O ─────────────────────────────────────────────────────────────────

void EditorApp::newScene()  { buildDefaultScene(); }

void EditorApp::exportOBJ() {
    const auto p = std::filesystem::current_path() / (scene_.name + "_export");
    if (export_::exportOBJ(scene_, p, {}))
        setStatus(std::format("OBJ → {}.obj", p.string()));
    else
        setStatus("OBJ export failed.");
}

void EditorApp::exportMAP() {
    const auto p = std::filesystem::current_path() / (scene_.name + ".map");
    export_::MAPExportOptions opts; opts.mapVersion = 220;
    if (export_::exportMAP(scene_, p, opts))
        setStatus(std::format("MAP → {}", p.string()));
    else
        setStatus("MAP export failed.");
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

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 5 — Play Mode
// ═══════════════════════════════════════════════════════════════════════════════

#include <forge/runtime.hpp>
#include <SDL3/SDL.h>

namespace forge::editor {

// ─── Enter play mode ─────────────────────────────────────────────────────────

void EditorApp::enterPlayMode() {
    if (playMode_) return;

    // Find player spawn position from scene
    glm::vec3 spawnPos = glm::vec3(camera_.target); // fallback = camera target
    auto [id, pe] = scene_.findByClassname("info_player_start");
    if (pe) spawnPos = glm::vec3(pe->transform.translation);

    // Build runtime
    runtime_ = std::make_unique<runtime::GameRuntime>();
    if (!runtime_->init(scene_, spawnPos)) {
        setStatus("[error] Play mode init failed — check console.");
        runtime_.reset();
        return;
    }

    // Capture mouse for FPS look
    SDL_SetWindowRelativeMouseMode(window_.sdlWindow(), true);

    playMode_ = true;
    setStatus("▶ Play mode — ESC to stop");
}

// ─── Exit play mode ──────────────────────────────────────────────────────────

void EditorApp::exitPlayMode() {
    if (!playMode_) return;

    // Release mouse
    SDL_SetWindowRelativeMouseMode(window_.sdlWindow(), false);

    if (runtime_) {
        runtime_->shutdown();
        runtime_.reset();
    }

    playMode_ = false;
    setStatus("■ Stopped play mode");
}

// ─── Play frame ──────────────────────────────────────────────────────────────

void EditorApp::runPlayFrame() {
    window_.pollEvents();

    if (window_.input().keys.escape) {
        exitPlayMode();
        return;
    }

    const float dt     = window_.deltaTime();
    const float aspect = window_.aspectRatio();

    // Step runtime (physics + player)
    runtime_->update(dt, window_.input());

    // Render full-screen (no FBO, no docking — covers entire window)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_.width(), window_.height());
    glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gfx::RenderFrame frame;
    frame.view      = runtime_->viewMatrix();
    frame.proj      = runtime_->projMatrix(aspect);
    frame.cameraPos = runtime_->cameraPosition();
    frame.wireframe = false;

    // Collect point lights from scene
    {
        auto lights = scene_.findAllByClassname("light");
        for (auto& [lid, pe] : lights) {
            gfx::PointLight pl;
            pl.position  = glm::vec3(pe->transform.translation);
            pl.intensity = pe->property<float>("light", 300.f);
            const auto col = pe->property<glm::vec3>("_color", glm::vec3{1.f,0.95f,0.8f});
            pl.color     = col;
            pl.radius    = pe->property<float>("radius", 512.f);
            frame.pointLights.push_back(pl);
        }
    }

    renderer_.beginFrame(frame);
    for (const auto& [id, data] : gpuData_) {
        for (const auto& sub : data.mesh.submeshes)
            renderer_.submit({ &sub, data.model, materialColour(sub.materialId()) });
    }
    renderer_.endFrame();

    // ImGui HUD
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawPlayHUD();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    window_.swapBuffers();
}

// ─── Play HUD ────────────────────────────────────────────────────────────────

void EditorApp::drawPlayHUD() {
    // Crosshair
    {
        const float cx = window_.width()  * 0.5f;
        const float cy = window_.height() * 0.5f;
        const float r  = 8.f;
        const float gap = 3.f;
        auto* dl = ImGui::GetForegroundDrawList();
        const auto col = IM_COL32(220, 220, 220, 200);
        dl->AddLine({cx - r, cy}, {cx - gap, cy}, col, 1.5f);
        dl->AddLine({cx + gap, cy}, {cx + r, cy}, col, 1.5f);
        dl->AddLine({cx, cy - r}, {cx, cy - gap}, col, 1.5f);
        dl->AddLine({cx, cy + gap}, {cx, cy + r}, col, 1.5f);
    }

    // Info overlay (top-left)
    ImGui::SetNextWindowPos({8.f, 8.f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##playhud", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);

    ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "▶ PLAY MODE");
    ImGui::Separator();
    ImGui::Text("%.0f fps", window_.fps());

    const glm::vec3 pos = runtime_->playerPosition();
    ImGui::Text("pos  %.0f  %.0f  %.0f", pos.x, pos.y, pos.z);
    ImGui::Text("yaw  %.0f°", runtime_->playerYaw());
    ImGui::Text("gnd  %s", runtime_->playerOnGround() ? "yes" : "no");

    ImGui::Separator();
    ImGui::TextDisabled("WASD  move    Q/E  jump");
    ImGui::TextDisabled("Shift  sprint");
    ImGui::TextDisabled("ESC  stop");

    ImGui::End();
}

} // namespace forge::editor

// ─── Phase 6 additions ────────────────────────────────────────────────────────

namespace forge::editor {

// ─── Vertex overlay ───────────────────────────────────────────────────────────

void EditorApp::drawVertexOverlay(ImVec2 pos, ImVec2 sz, const glm::mat4& vp) {
    if (!vertexSelection_.valid()) return;

    const ImVec2 screenPt = w2s(glm::vec3(vertexSelection_.position), vp, pos, sz);
    if (screenPt.x < -1e5f) return;

    auto* dl = ImGui::GetWindowDrawList();
    // Outer ring
    dl->AddCircleFilled(screenPt, 8.f,  IM_COL32(255,180,0,200));
    dl->AddCircle      (screenPt, 8.f,  IM_COL32(255,255,255,200), 12, 1.5f);
    // Inner dot
    dl->AddCircleFilled(screenPt, 3.f,  IM_COL32(255,255,255,240));

    // Position label
    const auto lbl = std::format("({:.0f}, {:.0f}, {:.0f})",
        vertexSelection_.position.x,
        vertexSelection_.position.y,
        vertexSelection_.position.z);
    dl->AddText({screenPt.x + 12.f, screenPt.y - 8.f},
                IM_COL32(255,220,80,220), lbl.c_str());
}

// ─── Vertex properties (shown in drawProperties when vertex is selected) ──────
// Called from within drawProperties() when activeTool_ == VertexSelect

static void drawVertexProperties_impl(
    EditorApp& app,
    scene::Scene& scene,
    VertexSelection& vs,
    CommandStack& cmds,
    const std::function<void(scene::EntityId)>& rebuildFn)
{
    ImGui::SeparatorText("Selected Vertex");

    auto* e  = scene.getEntity(vs.entityId);
    auto* be = e ? std::get_if<scene::BrushEntity>(e) : nullptr;
    if (!be || vs.brushIdx >= be->brushes.size()) { vs.clear(); return; }

    auto& brush = be->brushes[vs.brushIdx];

    // Convert world-space position back to local space for editing
    const glm::dvec3 localPos = glm::dvec3(
        glm::inverse(be->transform.matrix()) * glm::dvec4(vs.position, 1.0));

    glm::vec3 editPos = glm::vec3(vs.position);
    ImGui::SetNextItemWidth(-1.f);

    const bool changed = ImGui::DragFloat3("World Position",
        glm::value_ptr(editPos), 0.5f);

    if (changed) {
        const glm::dvec3 newWorld(editPos);
        const glm::dvec3 oldWorld = vs.position;

        // Convert to local space
        const glm::dvec3 oldLocal = glm::dvec3(
            glm::inverse(be->transform.matrix()) * glm::dvec4(oldWorld, 1.0));
        const glm::dvec3 newLocal = glm::dvec3(
            glm::inverse(be->transform.matrix()) * glm::dvec4(newWorld, 1.0));

        // Build command
        MoveVertexCommand cmd(vs.entityId, vs.brushIdx, oldLocal, newLocal);
        computeVertexMove(brush, oldLocal, newLocal, cmd);

        if (!cmd.affected.empty()) {
            cmds.push(std::make_unique<MoveVertexCommand>(std::move(cmd)), scene);
            rebuildFn(vs.entityId);
            vs.position = newWorld; // update selection to new position
        }
    }
    ImGui::SetItemTooltip("Drag to move vertex. Adjacent face planes are recomputed.");
}

// ─── Save / Load ─────────────────────────────────────────────────────────────

void EditorApp::saveScene() {
    if (currentFile_.empty()) { saveSceneAs(); return; }
    const auto err = serial::saveScene(scene_, currentFile_);
    if (err.empty())
        setStatus(std::format("Saved → {}", currentFile_.string()));
    else
        setStatus(std::format("Save failed: {}", err));
}

void EditorApp::saveSceneAs() {
    // Simple path construction: <cwd>/<scene_name>.forge
    currentFile_ = std::filesystem::current_path() / (scene_.name + ".forge");
    saveScene();
}

void EditorApp::openScene() {
    // Prompt via ImGui modal on next frame (simple implementation)
    // For now: open <cwd>/<scene_name>.forge
    const auto path = std::filesystem::current_path() / (scene_.name + ".forge");
    auto result = serial::loadScene(path);
    if (result) {
        scene_ = std::move(*result);
        selection_.clear(); faceSelection_.clear(); vertexSelection_.clear();
        commands_.clear(); gpuData_.clear();
        rebuildAllMeshes();
        camera_.frameAABB(scene_.worldBounds());
        currentFile_ = path;
        setStatus(std::format("Opened '{}'", path.string()));
    } else {
        setStatus(std::format("Open failed: {}", result.error()));
    }
}

// ─── GLTF export ─────────────────────────────────────────────────────────────

void EditorApp::exportGLTF() {
    const auto p = std::filesystem::current_path() / scene_.name;
    export_::GLTFExportOptions opts;
    opts.binary = true;
    opts.applyTransforms = true;
    const auto err = export_::exportGLTF(scene_, p, opts);
    if (err.empty())
        setStatus(std::format("GLTF → {}.glb", p.string()));
    else
        setStatus(std::format("GLTF failed: {}", err));
}

} // namespace forge::editor — Phase 6 additions
