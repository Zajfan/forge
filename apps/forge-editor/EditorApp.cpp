#include "EditorApp.hpp"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <array>
#include <algorithm>
#include <format>
#include <iostream>
#include <forge/export/GLTFExporter.hpp>
#include <forge/bsp.hpp>
#include <portable-file-dialogs.h>
#include <filesystem>
#include <chrono>
#include <functional>
#include <cctype>
#include <limits>
#include <sstream>
#include <unordered_set>

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

static std::shared_ptr<gfx::Material> makeEditorMaterial(
    gfx::MaterialLibrary& library,
    const std::string& materialId,
    const glm::vec3& color)
{
    const std::string resolvedId = materialId.empty() ? "default" : materialId;
    auto material = library.getMaterial(resolvedId);
    if (!material) {
        material = library.getOrCreateMaterial(resolvedId);
        material->albedoColor = color;
    }
    return material;
}

static ImTextureID toImTextureID(uint32_t textureId) noexcept {
    return static_cast<ImTextureID>(static_cast<uintptr_t>(textureId));
}

static void drawVertexSeparator() {
#ifdef ImGuiSeparatorFlags_Vertical
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
#else
    ImGui::TextDisabled("|");
#endif
    ImGui::SameLine();
}

static void drawVertexProperties_impl(
    EditorApp& app,
    scene::Scene& scene,
    VertexSelection& vs,
    CommandStack& cmds,
    const std::function<void(scene::EntityId)>& rebuildFn);

static std::optional<glm::vec3> parseVec3Property(const std::string& text) {
    std::stringstream ss(text);
    float x = 0.f, y = 0.f, z = 0.f;
    if (ss >> x >> y >> z) return glm::vec3{x, y, z};
    return std::nullopt;
}

static scene::PropertyValue makePropertyValueFromDef(const PropertyDef& pd) {
    switch (pd.type) {
    case PropType::Int:
        try { return std::stoi(pd.defaultValue); } catch (...) { return 0; }
    case PropType::Float:
        try { return std::stof(pd.defaultValue); } catch (...) { return 0.f; }
    case PropType::Bool:
        return pd.defaultValue == "1" || pd.defaultValue == "true";
    case PropType::Vec3:
    case PropType::Color:
        if (const auto v = parseVec3Property(pd.defaultValue)) return *v;
        return glm::vec3{};
    case PropType::Choices:
    case PropType::String:
    default:
        return pd.defaultValue;
    }
}

template<typename EntityT>
static void ensureClassDefaults(EntityT& entity, const EntityClassDef* def) {
    if (!def) return;
    for (const auto& pd : def->properties) {
        if (!entity.properties.contains(pd.name))
            entity.properties[pd.name] = makePropertyValueFromDef(pd);
    }
}

static bool drawEntityPropertyEditor(const PropertyDef* def,
                                     const std::string& key,
                                     scene::PropertyValue& value) {
    bool changed = false;
    ImGui::PushID(key.c_str());

    const PropType type = def ? def->type : PropType::String;
    switch (type) {
    case PropType::Bool: {
        bool v = false;
        if (const auto* p = std::get_if<bool>(&value)) v = *p;
        if (ImGui::Checkbox(key.c_str(), &v)) {
            value = v;
            changed = true;
        }
        break;
    }
    case PropType::Int: {
        int v = 0;
        if (const auto* intVal = std::get_if<int>(&value)) v = *intVal;
        else if (const auto* floatVal = std::get_if<float>(&value)) v = static_cast<int>(*floatVal);
        if (ImGui::DragInt(key.c_str(), &v, 1.f)) {
            value = v;
            changed = true;
        }
        break;
    }
    case PropType::Float: {
        float v = 0.f;
        if (const auto* floatVal = std::get_if<float>(&value)) v = *floatVal;
        else if (const auto* intVal = std::get_if<int>(&value)) v = static_cast<float>(*intVal);
        if (ImGui::DragFloat(key.c_str(), &v, 0.1f)) {
            value = v;
            changed = true;
        }
        break;
    }
    case PropType::Vec3:
    case PropType::Color: {
        glm::vec3 v{};
        if (const auto* p = std::get_if<glm::vec3>(&value)) v = *p;
        const bool edited = (type == PropType::Color)
            ? ImGui::ColorEdit3(key.c_str(), glm::value_ptr(v))
            : ImGui::DragFloat3(key.c_str(), glm::value_ptr(v), 0.1f);
        if (edited) {
            value = v;
            changed = true;
        }
        break;
    }
    case PropType::Choices: {
        std::string current = std::get_if<std::string>(&value) ? *std::get_if<std::string>(&value) : std::string{};
        if (ImGui::BeginCombo(key.c_str(), current.c_str())) {
            for (const auto& choice : def->choices) {
                const bool selected = (current == choice.value);
                if (ImGui::Selectable(choice.label.c_str(), selected)) {
                    value = choice.value;
                    changed = true;
                    current = choice.value;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        break;
    }
    case PropType::String:
    default: {
        std::string s = std::get_if<std::string>(&value) ? *std::get_if<std::string>(&value) : std::string{};
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", s.c_str());
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::InputText(key.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            value = std::string(buf);
            changed = true;
        }
        break;
    }
    }

    ImGui::PopID();
    return changed;
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
#ifdef IMGUI_HAS_DOCK
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    ImGui::StyleColorsDark();
    auto& st = ImGui::GetStyle();
    st.WindowRounding = 4.f; st.FrameRounding = 3.f; st.TabRounding = 3.f;
    st.Colors[ImGuiCol_WindowBg]  = {0.12f,0.12f,0.13f,1.f};
    st.Colors[ImGuiCol_MenuBarBg] = {0.10f,0.10f,0.11f,1.f};
    st.Colors[ImGuiCol_Header]    = {0.22f,0.40f,0.65f,0.6f};
    ImGui_ImplSDL3_InitForOpenGL(window_.sdlWindow(), window_.glContext());
    ImGui_ImplOpenGL3_Init("#version 450");
    window_.setEventCallback([](const SDL_Event& event) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    });

    if (!renderer_.init()) { std::cerr << "[error] Renderer init\n"; return false; }
    if (!gridRenderer_.init()) {
        std::cerr << "[error] Grid renderer init failed\n";
        return false;
    }
    if (!skyboxRenderer_.init()) {
        std::cerr << "[warn] Skybox renderer init failed; disabling skybox\n";
        showSkybox_ = false;
    }
    if (!shadowMap_.init(2048)) {
        std::cerr << "[warn] Shadow map init failed; disabling shadows\n";
        showShadows_ = false;
    }
    if (!bloomRenderer_.init()) {
        std::cerr << "[warn] Bloom init failed; disabling bloom\n";
        bloomEnabled_ = false;
        showBloom_ = false;
    }
    if (!audioEngine_.init()) {
        std::cerr << "[warn] Audio init failed; continuing without audio\n";
        audioInitialised_ = false;
    } else {
        audioInitialised_ = true;
    }
    audioEngine_.setSoundRoot(std::filesystem::current_path() / "sounds");
    audioEngine_.setMasterVolume(masterVolume_);
    if (!scriptEnv_.init()) {
        std::cerr << "[warn] Script environment init failed; script console disabled\n";
        showScriptConsole_ = false;
    }
    scriptEnv_.bindScene(&scene_);
    globalRegistry().loadBuiltins();
    textureCache_.setTextureRoot(std::filesystem::current_path() / "textures");
    meshAssetCache_.setAssetRoot(std::filesystem::current_path());
    viewportFbo_ = gfx::Framebuffer::create(1024, 600);
    for (auto& fbo : quadFbos_) fbo = gfx::Framebuffer::create(512, 300);
    orthoCams_[0].dir = gfx::OrthoCamera::Dir::Top;
    orthoCams_[1].dir = gfx::OrthoCamera::Dir::Front;
    orthoCams_[2].dir = gfx::OrthoCamera::Dir::Right;
    materialEditor_.setMaterialLibrary(&materialLibrary_);
    prefabLibraryRoot_ = std::filesystem::current_path();
    std::snprintf(prefabRootBuffer_, sizeof(prefabRootBuffer_), "%s", prefabLibraryRoot_.string().c_str());
    refreshPrefabLibrary();
    buildDefaultScene();
    return true;
}

// ─── Default scene ────────────────────────────────────────────────────────────

void EditorApp::buildDefaultScene() {
    scene_ = {}; scene_.name = "untitled";
    selection_.clear(); faceSelection_.clear();
    commands_.clear(); gpuData_.clear();
    entityLayers_.clear();
    entityGroups_.clear();
    hiddenEntities_.clear();
    isolatedEntities_.clear();
    layerVisibility_.clear();
    layerLocked_.clear();
    layerTint_.clear();
    soloLayer_.clear();
    groupFilter_.clear();
    layerVisibility_["Default"] = true;
    layerLocked_["Default"] = false;
    layerTint_["Default"] = {1.f, 1.f, 1.f};

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

    for (const auto& [id, _] : scene_.entities) {
        entityLayers_[id] = "Default";
        entityGroups_[id] = "";
    }

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

void EditorApp::updateEntityTransform(scene::EntityId id) {
    auto* e = scene_.getEntity(id);
    if (!e) return;
    if (auto it = gpuData_.find(id); it != gpuData_.end())
        it->second.model = glm::mat4(scene::entityTransform(*e).matrix());
}

void EditorApp::rebuildAllMeshes() {
    gpuData_.clear();
    for (const auto& [id, _] : scene_.entities) rebuildEntityMesh(id);
}

scene::EntityId EditorApp::addPrimitiveEntity(std::string name, geo::Brush brush, const glm::dvec3& worldPos) {
    scene::BrushEntity e;
    e.name = std::move(name);
    e.brushes = { std::move(brush) };
    e.transform = scene::Transform::fromTranslation(worldPos);
    const auto id = scene_.addEntity(std::move(e));
    entityLayers_[id] = "Default";
    entityGroups_[id] = "";
    rebuildEntityMesh(id);
    selection_.set(id);
    faceSelection_.clear();
    vertexSelection_.clear();
    setStatus(std::format("Added '{}'", scene::entityName(*scene_.getEntity(id))));
    return id;
}

glm::dvec3 EditorApp::viewportSpawnPoint(ImVec2 vpPos, ImVec2 vpSz) const {
    const ImVec2 mouse = ImGui::GetMousePos();
    const ImVec2 rel = { mouse.x - vpPos.x, mouse.y - vpPos.y };
    const glm::vec2 ndc = {
        (rel.x / vpSz.x) * 2.f - 1.f,
        -((rel.y / vpSz.y) * 2.f - 1.f)
    };

    const glm::vec3 rayOrigin = camera_.position();
    const glm::vec3 rayDir = camera_.unproject(ndc, vpSz.x / vpSz.y);

    glm::vec3 spawn = camera_.target;
    if (std::abs(rayDir.y) > 1e-5f) {
        const float t = -rayOrigin.y / rayDir.y;
        if (t > 0.0f) {
            spawn = rayOrigin + rayDir * t;
        }
    }

    if (snapEnabled_) {
        spawn = snapVec3(spawn, gridSize_);
    }

    return glm::dvec3(spawn);
}

void EditorApp::addDraggedBox(const glm::dvec3& a, const glm::dvec3& b, double height) {
    const double minX = std::min(a.x, b.x);
    const double maxX = std::max(a.x, b.x);
    const double minZ = std::min(a.z, b.z);
    const double maxZ = std::max(a.z, b.z);
    const double baseY = std::min(a.y, b.y);

    const double minSpan = snapEnabled_ ? static_cast<double>(gridSize_) : 16.0;
    if ((maxX - minX) < minSpan || (maxZ - minZ) < minSpan) {
        setStatus("Drag box too small. Increase footprint.");
        return;
    }

    const double finalHeight = std::max(height, minSpan);
    addPrimitiveEntity(
        "box",
        geo::makeBox({minX, baseY, minZ}, {maxX, baseY + finalHeight, maxZ}),
        glm::dvec3(0.0));
}

void EditorApp::extrudeSelectedFace(double distance) {
    if (!faceSelection_.valid()) return;
    if (std::abs(distance) < 1e-4) return;

    auto* entity = scene_.getEntity(faceSelection_.entityId);
    auto* be = entity ? std::get_if<scene::BrushEntity>(entity) : nullptr;
    if (!be || faceSelection_.brushIdx >= be->brushes.size()) return;

    const auto& brush = be->brushes[faceSelection_.brushIdx];
    if (faceSelection_.faceIdx >= brush.faces.size()) return;

    const auto& localPoly = brush.facePolygon(faceSelection_.faceIdx);
    if (localPoly.size() < 3) return;

    std::vector<glm::dvec3> base;
    base.reserve(localPoly.size());
    for (const auto& p : localPoly) {
        base.push_back(be->transform.transformPoint(p));
    }

    const glm::dvec3 normal = glm::normalize(
        be->transform.transformNormal(brush.faces[faceSelection_.faceIdx].plane.normal));
    const glm::dvec3 dir = normal * distance;

    std::vector<glm::dvec3> cap;
    cap.reserve(base.size());
    for (const auto& p : base) cap.push_back(p + dir);

    geo::Brush extruded;
    extruded.faces.reserve(base.size() + 2);

    extruded.faces.push_back({ geo::Plane::fromNormalPoint(-normal, base[0]), "default" });
    extruded.faces.push_back({ geo::Plane::fromNormalPoint( normal, cap[0]),  "default" });

    glm::dvec3 interior{};
    for (const auto& p : base) interior += p;
    interior /= static_cast<double>(base.size());
    interior += dir * 0.5;

    for (std::size_t i = 0; i < base.size(); ++i) {
        const std::size_t j = (i + 1) % base.size();
        const glm::dvec3 edge = base[j] - base[i];
        glm::dvec3 sideN = glm::normalize(glm::cross(edge, normal));
        geo::Plane side = geo::Plane::fromNormalPoint(sideN, base[i]);
        if (side.eval(interior) > 0.0) {
            side = side.flipped();
        }
        extruded.faces.push_back({ side, brush.faces[faceSelection_.faceIdx].materialId });
    }

    commands_.push(std::make_unique<AddBrushToEntityCommand>(faceSelection_.entityId, std::move(extruded)), scene_);
    rebuildEntityMesh(faceSelection_.entityId);
    selection_.set(faceSelection_.entityId);
    faceSelection_.clear();
    vertexSelection_.clear();
    setStatus(std::format("Extruded face by {:.0f} (in-place)", distance));
}

void EditorApp::insetSelectedFace(double insetAmount, double depth) {
    if (!faceSelection_.valid()) return;
    if (insetAmount <= 0.0 || depth <= 0.0) return;

    auto* entity = scene_.getEntity(faceSelection_.entityId);
    auto* be = entity ? std::get_if<scene::BrushEntity>(entity) : nullptr;
    if (!be || faceSelection_.brushIdx >= be->brushes.size()) return;

    const auto& brush = be->brushes[faceSelection_.brushIdx];
    if (faceSelection_.faceIdx >= brush.faces.size()) return;

    const auto& localPoly = brush.facePolygon(faceSelection_.faceIdx);
    if (localPoly.size() < 3) return;

    std::vector<glm::dvec3> base;
    base.reserve(localPoly.size());
    for (const auto& p : localPoly) base.push_back(be->transform.transformPoint(p));

    glm::dvec3 centroid{};
    for (const auto& p : base) centroid += p;
    centroid /= static_cast<double>(base.size());

    double rMax = 0.0;
    for (const auto& p : base) rMax = std::max(rMax, glm::length(p - centroid));
    if (rMax < 1e-4) return;

    const double factor = std::clamp(1.0 - (insetAmount / rMax), 0.05, 0.98);
    std::vector<glm::dvec3> inset;
    inset.reserve(base.size());
    for (const auto& p : base) inset.push_back(centroid + (p - centroid) * factor);

    const glm::dvec3 normal = glm::normalize(
        be->transform.transformNormal(brush.faces[faceSelection_.faceIdx].plane.normal));
    const glm::dvec3 dir = -normal * depth;

    std::vector<glm::dvec3> cap;
    cap.reserve(inset.size());
    for (const auto& p : inset) cap.push_back(p + dir);

    geo::Brush insetBrush;
    insetBrush.faces.reserve(inset.size() + 2);

    std::vector<geo::Plane> planes;
    planes.reserve(inset.size() + 2);
    planes.push_back(geo::Plane::fromNormalPoint( normal, inset[0]));
    planes.push_back(geo::Plane::fromNormalPoint(-normal, cap[0]));
    for (std::size_t i = 0; i < inset.size(); ++i) {
        const std::size_t j = (i + 1) % inset.size();
        planes.push_back(geo::Plane::fromPoints(inset[i], inset[j], cap[j]));
    }

    glm::dvec3 interior{};
    for (const auto& p : inset) interior += p;
    interior /= static_cast<double>(inset.size());
    interior += dir * 0.5;

    for (auto& pl : planes) {
        if (pl.eval(interior) > 0.0) pl = pl.flipped();
        insetBrush.faces.push_back({pl, brush.faces[faceSelection_.faceIdx].materialId});
    }

    const auto insetVerts = insetBrush.vertices();
    if (insetVerts.size() < 4) {
        setStatus("Inset failed: invalid resulting brush.");
        return;
    }

    commands_.push(std::make_unique<AddBrushToEntityCommand>(faceSelection_.entityId, std::move(insetBrush)), scene_);
    rebuildEntityMesh(faceSelection_.entityId);
    selection_.set(faceSelection_.entityId);
    faceSelection_.clear();
    vertexSelection_.clear();
    setStatus(std::format("Inset face {:.0f}, depth {:.0f} (in-place)", insetAmount, depth));
}

void EditorApp::bridgeSelectedFaces() {
    if (!faceSelection_.valid() || !bridgeSourceFace_.has_value()) return;
    const FaceSelection aSel = *bridgeSourceFace_;
    const FaceSelection bSel = faceSelection_;
    if (aSel.entityId == bSel.entityId && aSel.brushIdx == bSel.brushIdx && aSel.faceIdx == bSel.faceIdx) {
        setStatus("Bridge requires two different faces.");
        return;
    }

    struct FaceData {
        std::vector<glm::dvec3> poly;
        glm::dvec3 normal{};
        std::string material;
        scene::EntityId entityId = scene::kInvalidEntityId;
    };

    auto getFaceData = [&](const FaceSelection& sel) -> std::optional<FaceData> {
        auto* entity = scene_.getEntity(sel.entityId);
        auto* be = entity ? std::get_if<scene::BrushEntity>(entity) : nullptr;
        if (!be || sel.brushIdx >= be->brushes.size()) return std::nullopt;
        const auto& br = be->brushes[sel.brushIdx];
        if (sel.faceIdx >= br.faces.size()) return std::nullopt;
        const auto& local = br.facePolygon(sel.faceIdx);
        if (local.size() < 3) return std::nullopt;
        FaceData d;
        d.entityId = sel.entityId;
        d.material = br.faces[sel.faceIdx].materialId;
        d.normal = glm::normalize(be->transform.transformNormal(br.faces[sel.faceIdx].plane.normal));
        d.poly.reserve(local.size());
        for (const auto& p : local) d.poly.push_back(be->transform.transformPoint(p));
        return d;
    };

    auto a = getFaceData(aSel);
    auto b = getFaceData(bSel);
    if (!a || !b) {
        setStatus("Bridge failed: invalid face selection.");
        return;
    }
    if (a->poly.size() != b->poly.size()) {
        setStatus("Bridge requires matching face vertex counts.");
        return;
    }

    const std::size_t n = a->poly.size();
    std::vector<glm::dvec3> bMatch(n);
    double bestErr = std::numeric_limits<double>::max();
    int bestShift = 0;
    bool bestReverse = false;
    for (int rev = 0; rev < 2; ++rev) {
        for (std::size_t shift = 0; shift < n; ++shift) {
            double err = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                std::size_t idx = (shift + i) % n;
                if (rev == 1) idx = (shift + n - i) % n;
                const glm::dvec3 d = a->poly[i] - b->poly[idx];
                err += glm::dot(d, d);
            }
            if (err < bestErr) {
                bestErr = err;
                bestShift = static_cast<int>(shift);
                bestReverse = (rev == 1);
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        std::size_t idx = (bestShift + static_cast<int>(i)) % static_cast<int>(n);
        if (bestReverse) idx = (bestShift + static_cast<int>(n) - static_cast<int>(i)) % static_cast<int>(n);
        bMatch[i] = b->poly[idx];
    }

    geo::Brush bridge;
    bridge.faces.reserve(n + 2);

    std::vector<geo::Plane> planes;
    planes.reserve(n + 2);
    planes.push_back(geo::Plane::fromNormalPoint(a->normal, a->poly[0]));
    planes.push_back(geo::Plane::fromNormalPoint(b->normal, bMatch[0]));
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        planes.push_back(geo::Plane::fromPoints(a->poly[i], a->poly[j], bMatch[j]));
    }

    glm::dvec3 interior{};
    for (std::size_t i = 0; i < n; ++i) {
        interior += a->poly[i];
        interior += bMatch[i];
    }
    interior /= static_cast<double>(2 * n);

    for (auto& pl : planes) {
        if (pl.eval(interior) > 0.0) pl = pl.flipped();
        bridge.faces.push_back({pl, b->material});
    }

    const auto bridgeVerts = bridge.vertices();
    if (bridgeVerts.size() < 4) {
        setStatus("Bridge failed: generated geometry is degenerate.");
        return;
    }

    scene::BrushEntity e;
    e.name = "bridge";
    e.brushes = { std::move(bridge) };
    auto cmd = std::make_unique<AddBrushEntityCommand>(std::move(e));
    auto* cmdPtr = cmd.get();
    commands_.push(std::move(cmd), scene_);
    if (cmdPtr->addedId != scene::kInvalidEntityId) {
        entityLayers_[cmdPtr->addedId] = entityLayer(bSel.entityId);
        entityGroups_[cmdPtr->addedId] = entityGroup(bSel.entityId);
        rebuildEntityMesh(cmdPtr->addedId);
        selection_.set(cmdPtr->addedId);
        vertexSelection_.clear();
    }
    setStatus("Created bridge brush.");
}

void EditorApp::registerBridgeFaceSelection(const FaceHit& pick) {
    FaceSelection next;
    next.entityId = pick.entityId;
    next.brushIdx = pick.brushIdx;
    next.faceIdx = pick.faceIdx;

    if (faceSelection_.valid()) {
        const bool changed =
            faceSelection_.entityId != next.entityId ||
            faceSelection_.brushIdx != next.brushIdx ||
            faceSelection_.faceIdx != next.faceIdx;
        if (changed) bridgeSourceFace_ = faceSelection_;
    }
    faceSelection_ = next;
}

std::string EditorApp::entityLayer(scene::EntityId id) {
    auto it = entityLayers_.find(id);
    if (it == entityLayers_.end()) {
        it = entityLayers_.emplace(id, "Default").first;
    }
    if (!layerVisibility_.contains(it->second)) layerVisibility_[it->second] = true;
    if (!layerLocked_.contains(it->second)) layerLocked_[it->second] = false;
    if (!layerTint_.contains(it->second)) layerTint_[it->second] = {1.f, 1.f, 1.f};
    return it->second;
}

std::string EditorApp::entityGroup(scene::EntityId id) {
    auto it = entityGroups_.find(id);
    if (it == entityGroups_.end()) {
        it = entityGroups_.emplace(id, "").first;
    }
    return it->second;
}

bool EditorApp::layerVisible(const std::string& layer) const {
    if (auto it = layerVisibility_.find(layer); it != layerVisibility_.end()) return it->second;
    return true;
}

bool EditorApp::layerLocked(const std::string& layer) const {
    if (auto it = layerLocked_.find(layer); it != layerLocked_.end()) return it->second;
    return false;
}

glm::vec3 EditorApp::layerTint(const std::string& layer) const {
    if (auto it = layerTint_.find(layer); it != layerTint_.end()) return it->second;
    return {1.f, 1.f, 1.f};
}

bool EditorApp::entityVisibleByLayer(scene::EntityId id) const {
    if (auto it = entityLayers_.find(id); it != entityLayers_.end()) {
        if (!soloLayer_.empty() && it->second != soloLayer_) return false;
        return layerVisible(it->second);
    }
    return soloLayer_.empty();
}

bool EditorApp::entityVisibleByGroup(scene::EntityId id) const {
    if (groupFilter_.empty()) return true;
    if (auto it = entityGroups_.find(id); it != entityGroups_.end()) {
        return it->second == groupFilter_;
    }
    return false;
}

bool EditorApp::entityVisibleInEditor(scene::EntityId id) const {
    if (hiddenEntities_.contains(id)) return false;
    if (!isolatedEntities_.empty() && !isolatedEntities_.contains(id)) return false;
    return entityVisibleByLayer(id) && entityVisibleByGroup(id);
}

bool EditorApp::entityLockedByLayer(scene::EntityId id) const {
    if (auto it = entityLayers_.find(id); it != entityLayers_.end()) {
        return layerLocked(it->second);
    }
    return false;
}

void EditorApp::reconcileEditorState() {
    std::unordered_set<scene::EntityId> liveIds;
    liveIds.reserve(scene_.entities.size());
    for (const auto& [id, _] : scene_.entities) liveIds.insert(id);

    auto pruneMap = [&](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (!liveIds.contains(it->first)) it = map.erase(it);
            else ++it;
        }
    };
    pruneMap(entityLayers_);
    pruneMap(entityGroups_);
    pruneMap(gpuData_);
    for (auto it = hiddenEntities_.begin(); it != hiddenEntities_.end();) {
        if (!liveIds.contains(*it)) it = hiddenEntities_.erase(it);
        else ++it;
    }
    for (auto it = isolatedEntities_.begin(); it != isolatedEntities_.end();) {
        if (!liveIds.contains(*it)) it = isolatedEntities_.erase(it);
        else ++it;
    }

    for (const auto& [id, _] : scene_.entities) {
        (void)entityLayer(id);
        (void)entityGroup(id);
    }

    for (auto it = selection_.ids.begin(); it != selection_.ids.end();) {
        if (!liveIds.contains(*it) || !entityVisibleInEditor(*it)) it = selection_.ids.erase(it);
        else ++it;
    }

    if (faceSelection_.valid() && !liveIds.contains(faceSelection_.entityId)) faceSelection_.clear();
    if (vertexSelection_.valid() && !liveIds.contains(vertexSelection_.entityId)) vertexSelection_.clear();
    if (bridgeSourceFace_.has_value() && !liveIds.contains(bridgeSourceFace_->entityId)) bridgeSourceFace_.reset();

    if (!selection_.empty() && entityLockedByLayer(selection_.primary())) {
        faceSelection_.clear();
        vertexSelection_.clear();
    }
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
    reconcileEditorState();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
#ifdef IMGUI_HAS_DOCK
    ImGui::SetNextWindowViewport(vp->ID);
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("##host", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus|
        ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);
    drawMainMenuBar();
#ifdef IMGUI_HAS_DOCK
    const ImGuiID dock = ImGui::GetID("ForgeDock");
    dockspaceId_ = dock;
    ImGui::DockSpace(dock, {0.f,0.f});
    if (!layoutReady_) setupDefaultDockLayout(dock);
#else
    dockspaceId_ = 0;
#endif
    ImGui::End();

    drawToolbar();
    drawViewport();
    drawSceneTree();
    drawInspector();
    drawStatusBar();
    if (showMaterialEditor_) materialEditor_.draw();
    if (showUndoHistory_)     drawUndoHistory();
    if (showEntityClasses_)   drawEntityClassBrowser();
    if (showValidation_)      drawValidationPanel();
    if (showBSPStats_)        drawBSPPanel();
    if (showPrefabBrowser_)   drawPrefabBrowser();
    if (showScriptConsole_)   drawScriptConsole();
    if (showBloomSettings_)   drawBloomSettings();
    if (showAudioSettings_)   drawAudioSettings();
    if (csgPreview_.active)   drawCSGPreviewOverlay();
}

// ─── Dock layout ─────────────────────────────────────────────────────────────

void EditorApp::setupDefaultDockLayout(ImGuiID dock) {
#ifndef IMGUI_HAS_DOCK
    (void)dock;
    layoutReady_ = true;
    return;
#else
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
#endif
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
            commands_.undo(scene_); rebuildAllMeshes(); reconcileEditorState();
        }
        if (ImGui::MenuItem("Redo","Ctrl+Y",false,commands_.canRedo())) {
            commands_.redo(scene_); rebuildAllMeshes(); reconcileEditorState();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select All Visible", "Ctrl+A")) {
            selection_.clear();
            for (const auto& [id, _] : scene_.entities) {
                if (entityVisibleInEditor(id) && !entityLockedByLayer(id)) selection_.add(id);
            }
            if (selection_.empty()) setStatus("No selectable entities in current filters.");
        }
        if (ImGui::MenuItem("Select Same Group", "Ctrl+Shift+A", false, !selection_.empty())) {
            const std::string group = entityGroup(selection_.primary());
            selection_.clear();
            for (const auto& [id, _] : scene_.entities) {
                if (!entityVisibleInEditor(id) || entityLockedByLayer(id)) continue;
                if (entityGroup(id) == group) selection_.add(id);
            }
            if (selection_.empty()) setStatus("No matching group entities found.");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Selected","Del",false,!selection_.empty())) {
            const std::vector<scene::EntityId> toDelete = selection_.ids;
            commands_.push(std::make_unique<BatchDeleteCommand>(toDelete), scene_);
            for (auto id : toDelete) gpuData_.erase(id);
            selection_.clear(); faceSelection_.clear();
            vertexSelection_.clear();
            bridgeSourceFace_.reset();
            reconcileEditorState();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("CSG Subtract Preview (selection=cutter)",nullptr,false,!selection_.empty()))
            beginCSGPreviewSubtract();
        if (ImGui::MenuItem("CSG Union Preview (select 2 entities)", nullptr, false, selection_.ids.size() == 2))
            beginCSGPreviewUnion();
        if (ImGui::MenuItem("CSG Intersect Preview (select 2 entities)", nullptr, false, selection_.ids.size() == 2))
            beginCSGPreviewIntersect();
        if (ImGui::MenuItem("CSG XOR Preview (select 2 entities)", nullptr, false, selection_.ids.size() == 2))
            beginCSGPreviewXor();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, !selection_.empty()))
            duplicateSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Hide Selected", nullptr, false, !selection_.empty()))
            hideSelection();
        if (ImGui::MenuItem("Isolate Selected", nullptr, false, !selection_.empty()))
            isolateSelection();
        if (ImGui::MenuItem("Show All Hidden / Clear Isolation", nullptr, false,
            !hiddenEntities_.empty() || !isolatedEntities_.empty()))
            clearHiddenIsolation();
        ImGui::Separator();
        if (ImGui::MenuItem("Run Validation"))
            runValidation();
        ImGui::Separator();
        if (ImGui::MenuItem("Snap All to Grid")) {
            commands_.push(std::make_unique<SnapToGridCommand>(static_cast<double>(gridSize_)), scene_);
            rebuildAllMeshes();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add")) {
        drawAddPrimitivesMenu();
        ImGui::Separator();
        if (ImGui::MenuItem("Insert Prefab...")) insertPrefab();
        ImGui::MenuItem("Prefab Browser", nullptr, &showPrefabBrowser_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid",      nullptr, &showGrid_);
        ImGui::MenuItem("Wireframe", "F1",    &wireframe_);
        ImGui::MenuItem("Skybox",    nullptr, &showSkybox_);
        ImGui::MenuItem("Fog",       nullptr, &showFog_);
        ImGui::MenuItem("Shadows",   nullptr, &showShadows_);
        ImGui::MenuItem("Bloom",     nullptr, &showBloom_);
        ImGui::Separator();
        if (ImGui::MenuItem("Build BSP")) buildBSP();
        ImGui::MenuItem("BSP Stats",    nullptr, &showBSPStats_);
        ImGui::Separator();
        ImGui::MenuItem("Bloom",         nullptr, &bloomEnabled_);
        ImGui::MenuItem("Bloom Settings",nullptr, &showBloomSettings_);
        ImGui::Separator();
        ImGui::MenuItem("Audio Settings",nullptr, &showAudioSettings_);
        ImGui::MenuItem("Script Console",nullptr, &showScriptConsole_);
        ImGui::MenuItem("Trigger Debug Overlay", nullptr, &showTriggerDebugOverlay_);
        ImGui::Separator();
        if (ImGui::MenuItem("Frame All","F")) camera_.frameAABB(scene_.worldBounds());
        ImGui::MenuItem("Orthographic Edit Mode", nullptr, &orthoEditMode_);
        if (ImGui::MenuItem("Hide Selected", nullptr, false, !selection_.empty())) hideSelection();
        if (ImGui::MenuItem("Isolate Selected", nullptr, false, !selection_.empty())) isolateSelection();
        if (ImGui::MenuItem("Show All Hidden / Clear Isolation", nullptr, false,
            !hiddenEntities_.empty() || !isolatedEntities_.empty())) clearHiddenIsolation();
        ImGui::Separator();
        ImGui::MenuItem("Material Editor", nullptr, &showMaterialEditor_);
        ImGui::MenuItem("Prefab Browser",  nullptr, &showPrefabBrowser_);
        ImGui::MenuItem("Undo History",     nullptr, &showUndoHistory_);
        ImGui::MenuItem("Entity Classes",   nullptr, &showEntityClasses_);
        ImGui::MenuItem("Validation",       nullptr, &showValidation_);
        ImGui::MenuItem("Script Console",  nullptr, &showScriptConsole_);
        if (ImGui::MenuItem("Frame Selected",nullptr,false,!selection_.empty())) {
            const geo::AABB combined = selection_.combinedBounds(scene_);
            if (combined.isValid()) camera_.frameAABB(combined);
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
    static int cylSides = 32, coneSides = 32;
    static int sphLat = 16, sphLon = 32;
    static int torMajor = 24, torMinor = 16;
    const glm::dvec3 spawn = glm::dvec3(camera_.target);

    if (ImGui::MenuItem("Box (64³)"))
        addPrimitiveEntity("box", geo::makeBox({-32,0,-32},{32,64,32}), spawn);
    if (ImGui::MenuItem("Wedge (64³)"))
        addPrimitiveEntity("wedge", geo::makeWedge({-32,0,-32},{32,64,32}), spawn);
    ImGui::Separator();
    
    // Prism
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##ps",&pSides); pSides=std::clamp(pSides,3,32);
    ImGui::SameLine();
    if (ImGui::MenuItem(std::format("{}-Prism",pSides).c_str()))
        addPrimitiveEntity(std::format("{}_prism",pSides), geo::makePrism({0,0,0},32.,64.,pSides), spawn);
    
    // Pyramid
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##ys",&ySides); ySides=std::clamp(ySides,3,32);
    ImGui::SameLine();
    if (ImGui::MenuItem(std::format("{}-Pyramid",ySides).c_str()))
        addPrimitiveEntity(std::format("{}_pyramid",ySides), geo::makePyramid({0,0,0},32.,64.,ySides), spawn);
    
    ImGui::Separator();
    
    // Cylinder
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##cyl",&cylSides); cylSides=std::clamp(cylSides,3,64);
    ImGui::SameLine();
    if (ImGui::MenuItem("Cylinder"))
        addPrimitiveEntity("cylinder", geo::makeCylinder({0,0,0},32.,64.,cylSides), spawn);
    
    // Cone
    ImGui::SetNextItemWidth(70); ImGui::InputInt("##cone",&coneSides); coneSides=std::clamp(coneSides,3,64);
    ImGui::SameLine();
    if (ImGui::MenuItem("Cone"))
        addPrimitiveEntity("cone", geo::makeCone({0,0,0},32.,64.,coneSides), spawn);
    
    ImGui::Separator();
    
    // Sphere
    ImGui::SetNextItemWidth(50); ImGui::InputInt("##slat",&sphLat); sphLat=std::clamp(sphLat,3,32);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50); ImGui::InputInt("##slon",&sphLon); sphLon=std::clamp(sphLon,3,64);
    ImGui::SameLine();
    if (ImGui::MenuItem("Sphere"))
        addPrimitiveEntity("sphere", geo::makeSphere({0,0,0},32.,sphLat,sphLon), spawn);
    
    // Torus
    ImGui::SetNextItemWidth(50); ImGui::InputInt("##tmaj",&torMajor); torMajor=std::clamp(torMajor,3,64);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50); ImGui::InputInt("##tmin",&torMinor); torMinor=std::clamp(torMinor,3,32);
    ImGui::SameLine();
    if (ImGui::MenuItem("Torus"))
        addPrimitiveEntity("torus", geo::makeTorus({0,0,0},48.,16.,torMajor,torMinor), spawn);
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
            if (t != ActiveTool::FaceSelect && t != ActiveTool::FaceMove)
                bridgeSourceFace_.reset();
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

    drawVertexSeparator();

    // Face / geometry tools
    btn("FSEL", ActiveTool::FaceSelect, "Select face (T)");
    btn("FMOV", ActiveTool::FaceMove,   "Move face along normal (Y)");
    btn("CLIP", ActiveTool::Clip,       "Clip brush with plane (C)");
    btn("PAINT",ActiveTool::Paint,      "Paint material (P)");
    btn("VTEX", ActiveTool::VertexSelect,"Select vertex (V)");
    btn("BCR",  ActiveTool::BoxCreate,  "Box create drag tool");

    drawVertexSeparator();
    ImGui::Checkbox("Grid",&showGrid_); ImGui::SameLine();
    ImGui::Checkbox("Wire",&wireframe_); ImGui::SameLine();

    drawVertexSeparator();
    if (ImGui::Button(viewportLayout_ == ViewportLayout::Single ? "[1]" : "[4]", {30.f, 22.f})) {
        viewportLayout_ = (viewportLayout_ == ViewportLayout::Single)
                        ? ViewportLayout::Quad : ViewportLayout::Single;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle 1/4 viewport layout");
    ImGui::SameLine();

    // Grid snap controls
    drawVertexSeparator();
    ImGui::Checkbox("Snap", &snapEnabled_); ImGui::SameLine();
    ImGui::SetNextItemWidth(56.f);
    if (ImGui::BeginCombo("##grid", gridLabel(gridSize_).c_str())) {
        for (float g : kGridSizes) {
            if (ImGui::Selectable(gridLabel(g).c_str(), gridSize_ == g))
                gridSize_ = g;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grid size");
    ImGui::SameLine();
    ImGui::BeginDisabled(!commands_.canUndo());
    if (ImGui::Button("Undo")) { commands_.undo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!commands_.canRedo());
    if (ImGui::Button("Redo")) { commands_.redo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled();

    // Play / Stop
    ImGui::SameLine();
    drawVertexSeparator();
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
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        const bool shift = ImGui::GetIO().KeyShift;
        if (k.f1) wireframe_ = !wireframe_;
        if (k.q)  activeTool_ = ActiveTool::Select;
        if (k.w)  activeTool_ = ActiveTool::Move;
        if (k.e)  activeTool_ = ActiveTool::Rotate;
        if (k.r)  activeTool_ = ActiveTool::Scale;
        if (k.t)  activeTool_ = ActiveTool::FaceSelect;
        if (k.y)  activeTool_ = ActiveTool::FaceMove;
        if (k.c)  activeTool_ = ActiveTool::Clip;
        if (k.p)  activeTool_ = ActiveTool::Paint;
        if (k.v)  activeTool_ = ActiveTool::VertexSelect;
        if (ctrl && k.a && !shift) {
            selection_.clear();
            for (const auto& [id, _] : scene_.entities) {
                if (entityVisibleInEditor(id) && !entityLockedByLayer(id)) selection_.add(id);
            }
        }
        if (ctrl && shift && k.a && !selection_.empty()) {
            const std::string group = entityGroup(selection_.primary());
            selection_.clear();
            for (const auto& [id, _] : scene_.entities) {
                if (!entityVisibleInEditor(id) || entityLockedByLayer(id)) continue;
                if (entityGroup(id) == group) selection_.add(id);
            }
        }
        // F — frame selected (or all if empty)
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            if (!selection_.empty()) {
                geo::AABB combined;
                for (auto id : selection_.ids)
                    if (auto* e = scene_.getEntity(id))
                        if (auto* be = std::get_if<scene::BrushEntity>(e))
                            combined.expand(be->worldBounds());
                if (combined.isValid()) camera_.frameAABB(combined);
            } else {
                camera_.frameAABB(scene_.worldBounds());
            }
        }
        // Ctrl+Z — undo
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !shift) {
            commands_.undo(scene_); rebuildAllMeshes(); reconcileEditorState();
        }
        // Ctrl+Y — redo
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            commands_.redo(scene_); rebuildAllMeshes(); reconcileEditorState();
        }
        // Ctrl+D — duplicate
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D) && !selection_.empty())
            duplicateSelection();
        // Arrow keys — nudge selection in world space (TrenchBroom-style keyboard move)
        if (!selection_.empty()) {
            const double step = static_cast<double>(gridSize_) * (shift ? 10.0 : 1.0);
            glm::dvec3 delta{0.0, 0.0, 0.0};
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  delta.x -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) delta.x += step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    delta.z -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  delta.z += step;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp))     delta.y += step;
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown))   delta.y -= step;
            if (delta != glm::dvec3{0.0, 0.0, 0.0}) {
                nudgeSelection(delta);
            }
        }
        // F2 — begin inline rename of primary selected entity
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && !selection_.empty()) {
            renamingId_ = selection_.primary();
            if (auto* e = scene_.getEntity(renamingId_))
                std::snprintf(treeRenameBuffer_, sizeof(treeRenameBuffer_),
                              "%s", scene::entityName(*e).c_str());
        }
        // Delete — batch-delete all selected entities
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selection_.empty()) {
            const std::vector<scene::EntityId> toDelete = selection_.ids;
            commands_.push(std::make_unique<BatchDeleteCommand>(toDelete), scene_);
            for (auto id : toDelete) gpuData_.erase(id);
            selection_.clear(); faceSelection_.clear();
            vertexSelection_.clear();
            bridgeSourceFace_.reset();
            reconcileEditorState();
        }
        // Escape — cancel active box-create drag
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (boxCreate_.draggingFootprint || boxCreate_.adjustingHeight) {
                boxCreate_.draggingFootprint = false;
                boxCreate_.adjustingHeight   = false;
                setStatus("Box create cancelled.");
            }
        }
    }
    ImGui::End();
}

// ─── Viewport ─────────────────────────────────────────────────────────────────

void EditorApp::drawViewport() {
    if (viewportLayout_ == ViewportLayout::Quad) {
        drawQuadViewport();
        return;
    }

    ImGuiWindowFlags viewportFlags = 0;
#ifdef IMGUI_HAS_DOCK
    if (dockspaceId_ != 0) {
        ImGui::SetNextWindowDockID(dockspaceId_, ImGuiCond_Always);
        viewportFlags |= ImGuiWindowFlags_NoSavedSettings;
    }
#endif
#ifndef IMGUI_HAS_DOCK
    const ImGuiViewport* mainVp = ImGui::GetMainViewport();
    const ImVec2 pos = { mainVp->WorkPos.x + 250.f, mainVp->WorkPos.y + 56.f };
    const ImVec2 size = {
        std::max(640.0f, mainVp->WorkSize.x - 560.f),
        std::max(420.0f, mainVp->WorkSize.y - 120.f)
    };
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    viewportFlags |= ImGuiWindowFlags_NoSavedSettings;
#endif

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::Begin("Viewport", nullptr, viewportFlags);
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
    frame.wireframe  = wireframe_;
    frame.sunDirection = { -0.5f,-1.f,-0.5f };
    frame.sunColor     = {  1.f, 0.95f, 0.8f };
    frame.sunIntensity = 1.2f;
    frame.ambientColor = { 0.06f, 0.06f, 0.08f };
    frame.fogColor   = scene_.fog.color;
    frame.fogDensity = showFog_ ? scene_.fog.density : 0.f;

    // Shadow pass (before skybox/scene)
    if (showShadows_ && shadowMap_.valid()) {
        const geo::AABB sb = scene_.worldBounds();
        const glm::mat4 lvp = shadowMap_.computeLightVP(frame.sunDirection, sb);
        shadowMap_.setLightVP(lvp);
        shadowMap_.beginPass();
        for (const auto& [id, data] : gpuData_) {
            for (const auto& sub : data.mesh.submeshes)
                if (sub.valid())
                    shadowMap_.submitDepth(sub.vao(), sub.indexCount(), data.model);
        }
        shadowMap_.endPass();
        shadowMap_.bindDepthTexture(1);
        frame.shadowsEnabled   = true;
        frame.lightSpaceMatrix = lvp;
        frame.shadowMapUnit    = 1;
        // Rebind the viewport FBO — endPass() unbinds it back to default
        viewportFbo_.bind();
        glViewport(0, 0, (int)sz.x, (int)sz.y);
    }

    // Skybox (before scene, writes at far depth)
    if (showSkybox_ && skyboxRenderer_.valid()) {
        const glm::mat4 invVP = glm::inverse(frame.proj * frame.view);
        skyboxRenderer_.draw(invVP);
    }

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
        if (!entityVisibleInEditor(id)) continue;
        const bool sel = selection_.contains(id);
        const glm::vec3 tint = layerTint(entityLayer(id));
        for (const auto& sub : data.mesh.submeshes) {
            glm::vec3 col = materialColour(sub.materialId());
            col *= tint;
            if (sel) col = glm::mix(col, glm::vec3(0.3f,0.65f,1.f), 0.4f);
            renderer_.submit({
                .mesh = &sub,
                .modelMatrix = data.model,
                .material = makeEditorMaterial(materialLibrary_, sub.materialId(), col),
            });
        }
    }

    if (csgPreview_.active) {
        for (const auto& pd : csgPreview_.meshes) {
            for (const auto& sub : pd.mesh.submeshes) {
                renderer_.submit({
                    .mesh = &sub,
                    .modelMatrix = pd.model,
                    .material = makeEditorMaterial(materialLibrary_, sub.materialId(), {0.15f, 0.95f, 0.95f}),
                });
            }
        }
    }

    drawMeshEntities(frame);
    renderer_.endFrame();

    if (showGrid_ && gridRenderer_.valid())
        gridRenderer_.draw(frame.view, frame.proj, camera_.nearZ, camera_.farZ);

    viewportFbo_.unbind();
    glViewport(0, 0, window_.width(), window_.height());

    // ── Display FBO ───────────────────────────────────────────────────────────
    // Update audio listener to match camera
    if (audioEngine_.valid()) {
        audioEngine_.setListenerTransform(camera_.position(),
            glm::normalize(camera_.target - camera_.position()), {0.f,1.f,0.f});
        audioEngine_.update();
    }

    // Apply bloom post-process (modifies nothing; returns composited texture)
    const uint32_t displayTex = (bloomEnabled_ && bloomRenderer_.valid())
        ? bloomRenderer_.apply(viewportFbo_, (int)sz.x, (int)sz.y)
        : viewportFbo_.colorTexture;

    const ImVec2 vpPos = ImGui::GetCursorScreenPos();
    ImGui::Image(toImTextureID(displayTex), sz, {0.f,1.f}, {1.f,0.f});

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FORGE_PREFAB_PATH")) {
            const auto* raw = static_cast<const char*>(payload->Data);
            const std::filesystem::path prefabPath(raw ? raw : "");
            const glm::dvec3 spawn = viewportSpawnPoint(vpPos, sz);
            placePrefabFromFile(prefabPath, spawn, false);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FORGE_PREFAB_PATH_LINKED")) {
            const auto* raw = static_cast<const char*>(payload->Data);
            const std::filesystem::path prefabPath(raw ? raw : "");
            const glm::dvec3 spawn = viewportSpawnPoint(vpPos, sz);
            placePrefabFromFile(prefabPath, spawn, true);
        }
        ImGui::EndDragDropTarget();
    }

    // TrenchBroom-style quick creation at cursor: Ctrl + Right Click
    if (ImGui::IsItemHovered() && ImGui::GetIO().KeyCtrl &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("ViewportCreatePopup");
    }

    if (ImGui::BeginPopup("ViewportCreatePopup")) {
        static int popupPrismSides = 6;
        static int popupPyramidSides = 4;
        const glm::dvec3 spawn = viewportSpawnPoint(vpPos, sz);

        ImGui::TextDisabled("Create at cursor (snapped to grid)");
        ImGui::Separator();

        if (ImGui::MenuItem("Box (64^3)")) {
            addPrimitiveEntity("box", geo::makeBox({-32,0,-32},{32,64,32}), spawn);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Wedge (64^3)")) {
            addPrimitiveEntity("wedge", geo::makeWedge({-32,0,-32},{32,64,32}), spawn);
            ImGui::CloseCurrentPopup();
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(80.f);
        ImGui::InputInt("Prism sides", &popupPrismSides);
        popupPrismSides = std::clamp(popupPrismSides, 3, 32);
        if (ImGui::MenuItem(std::format("Create {}-Prism", popupPrismSides).c_str())) {
            addPrimitiveEntity(
                std::format("{}_prism", popupPrismSides),
                geo::makePrism({0,0,0}, 32.0, 64.0, popupPrismSides),
                spawn);
            ImGui::CloseCurrentPopup();
        }

        ImGui::SetNextItemWidth(80.f);
        ImGui::InputInt("Pyramid sides", &popupPyramidSides);
        popupPyramidSides = std::clamp(popupPyramidSides, 3, 32);
        if (ImGui::MenuItem(std::format("Create {}-Pyramid", popupPyramidSides).c_str())) {
            addPrimitiveEntity(
                std::format("{}_pyramid", popupPyramidSides),
                geo::makePyramid({0,0,0}, 32.0, 64.0, popupPyramidSides),
                spawn);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // ── ImGui overlays drawn on top of the image ──────────────────────────────
    const glm::mat4 vp = frame.proj * frame.view;
    if (faceSelection_.valid())
        drawFaceOverlay(vpPos, sz, vp);
    if (vertexSelection_.valid())
        drawVertexOverlay(vpPos, sz, vp);
    if (activeTool_ == ActiveTool::Clip && !selection_.empty())
        drawClipPreview(vpPos, sz, vp);
    if (activeTool_ == ActiveTool::BoxCreate)
        drawBoxCreatePreview(vpPos, sz, vp);
    if (activeTool_ == ActiveTool::Select)
        drawMarqueePreview(vpPos);

    // ── Gizmo ────────────────────────────────────────────────────────────────
    drawGizmo(vpPos, sz);

    // ── Mouse input ───────────────────────────────────────────────────────────
    // Run after drawGizmo() so ImGuizmo hover/using state is current for this frame.
    if (ImGui::IsItemHovered())
        handleViewportMouse(vpPos, sz);

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

void EditorApp::drawBoxCreatePreview(ImVec2 pos, ImVec2 sz, const glm::mat4& vp) {
    if (!boxCreate_.draggingFootprint && !boxCreate_.adjustingHeight) return;

    const double minX = std::min(boxCreate_.start.x, boxCreate_.end.x);
    const double maxX = std::max(boxCreate_.start.x, boxCreate_.end.x);
    const double minZ = std::min(boxCreate_.start.z, boxCreate_.end.z);
    const double maxZ = std::max(boxCreate_.start.z, boxCreate_.end.z);
    const double baseY = std::min(boxCreate_.start.y, boxCreate_.end.y);
    const double height = std::max(
        boxCreate_.height,
        snapEnabled_ ? static_cast<double>(gridSize_) : 8.0);
    const double topY = baseY + height;

    const std::array<glm::vec3, 8> corners = {{
        {static_cast<float>(minX), static_cast<float>(baseY), static_cast<float>(minZ)},
        {static_cast<float>(maxX), static_cast<float>(baseY), static_cast<float>(minZ)},
        {static_cast<float>(maxX), static_cast<float>(baseY), static_cast<float>(maxZ)},
        {static_cast<float>(minX), static_cast<float>(baseY), static_cast<float>(maxZ)},
        {static_cast<float>(minX), static_cast<float>(topY),  static_cast<float>(minZ)},
        {static_cast<float>(maxX), static_cast<float>(topY),  static_cast<float>(minZ)},
        {static_cast<float>(maxX), static_cast<float>(topY),  static_cast<float>(maxZ)},
        {static_cast<float>(minX), static_cast<float>(topY),  static_cast<float>(maxZ)},
    }};

    std::array<ImVec2, 8> pts;
    for (int i = 0; i < 8; ++i) pts[i] = w2s(corners[i], vp, pos, sz);

    auto* dl = ImGui::GetWindowDrawList();
    const ImU32 line = IM_COL32(80, 210, 255, 245);
    const ImU32 fill = IM_COL32(80, 210, 255, 35);
    dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], fill);
    dl->AddQuadFilled(pts[4], pts[5], pts[6], pts[7], fill);
    dl->AddLine(pts[0], pts[1], line, 2.f); dl->AddLine(pts[1], pts[2], line, 2.f);
    dl->AddLine(pts[2], pts[3], line, 2.f); dl->AddLine(pts[3], pts[0], line, 2.f);
    dl->AddLine(pts[4], pts[5], line, 2.f); dl->AddLine(pts[5], pts[6], line, 2.f);
    dl->AddLine(pts[6], pts[7], line, 2.f); dl->AddLine(pts[7], pts[4], line, 2.f);
    dl->AddLine(pts[0], pts[4], line, 2.f); dl->AddLine(pts[1], pts[5], line, 2.f);
    dl->AddLine(pts[2], pts[6], line, 2.f); dl->AddLine(pts[3], pts[7], line, 2.f);

    // Dimension readout overlay
    const float ww = static_cast<float>(maxX - minX);
    const float dw = static_cast<float>(maxZ - minZ);
    const float hw = static_cast<float>(height);
    const std::string dimText = std::format("{:.0f} \xc3\x97 {:.0f} \xc3\x97 {:.0f}", ww, dw, hw);
    const glm::vec3 topCenter{
        static_cast<float>((minX + maxX) * 0.5),
        static_cast<float>(topY),
        static_cast<float>((minZ + maxZ) * 0.5)
    };
    const ImVec2 labelPt = w2s(topCenter, vp, pos, sz);
    dl->AddText({labelPt.x + 6.f, labelPt.y - 16.f},
                IM_COL32(255, 255, 180, 240), dimText.c_str());
}

void EditorApp::drawMarqueePreview(ImVec2 pos) {
    if (!marquee_.dragging) return;
    const ImVec2 a = marquee_.start;
    const ImVec2 b = marquee_.end;
    const ImVec2 minP{std::min(a.x, b.x), std::min(a.y, b.y)};
    const ImVec2 maxP{std::max(a.x, b.x), std::max(a.y, b.y)};
    const bool crossing = (b.x < a.x);

    auto* dl = ImGui::GetWindowDrawList();
    (void)pos;
    const ImU32 fill = crossing ? IM_COL32(255, 170, 80, 35) : IM_COL32(80, 170, 255, 35);
    const ImU32 line = crossing ? IM_COL32(255, 170, 80, 220) : IM_COL32(80, 170, 255, 220);
    dl->AddRectFilled(minP, maxP, fill);
    dl->AddRect(minP, maxP, line, 0.f, 0, 1.8f);
    dl->AddText(
        {minP.x + 6.f, minP.y + 4.f},
        line,
        crossing ? "Crossing" : "Window");
}

// ─── Viewport mouse ──────────────────────────────────────────────────────────

void EditorApp::handleViewportMouse(ImVec2 vpPos, ImVec2 vpSz) {
    const auto& m = window_.input().mouse;
    const auto& io = ImGui::GetIO();
    const bool gizmoOver = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
    const float aspect   = vpSz.x / vpSz.y;

    auto pickGroundPoint = [&]() -> std::optional<glm::dvec3> {
        const ImVec2 rel = { ImGui::GetMousePos().x - vpPos.x,
                             ImGui::GetMousePos().y - vpPos.y };
        const glm::vec2 ndc = {
             (rel.x / vpSz.x) * 2.f - 1.f,
            -((rel.y / vpSz.y) * 2.f - 1.f)
        };
        const glm::dvec3 rayOrigin = glm::dvec3(camera_.position());
        const glm::dvec3 rayDir = glm::dvec3(camera_.unproject(ndc, aspect));
        const double dy = rayDir.y;
        if (std::abs(dy) < 1e-6) return std::nullopt;
        const double t = -rayOrigin.y / dy;
        if (t <= 0.0) return std::nullopt;
        return rayOrigin + rayDir * t;
    };

    auto runMarqueeSelection = [&]() {
        const ImVec2 minP{std::min(marquee_.start.x, marquee_.end.x), std::min(marquee_.start.y, marquee_.end.y)};
        const ImVec2 maxP{std::max(marquee_.start.x, marquee_.end.x), std::max(marquee_.start.y, marquee_.end.y)};
        const bool crossing = marquee_.end.x < marquee_.start.x;
        if ((maxP.x - minP.x) < 2.f || (maxP.y - minP.y) < 2.f) return;

        const bool append = io.KeyShift;
        if (!append) {
            selection_.clear();
            faceSelection_.clear();
            vertexSelection_.clear();
        }

        const glm::mat4 vp = camera_.projMatrix(aspect) * camera_.viewMatrix();

        auto projectPoint = [&](const glm::dvec3& worldPos, ImVec2& out)->bool {
            const glm::vec4 clip = vp * glm::vec4(glm::vec3(worldPos), 1.f);
            if (std::abs(clip.w) < 1e-5f) return false;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.f || ndc.z > 1.f) return false;
            out = {
                vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSz.x,
                vpPos.y + (-ndc.y * 0.5f + 0.5f) * vpSz.y
            };
            return true;
        };

        for (const auto& [id, entity] : scene_.entities) {
            if (!entityVisibleInEditor(id) || entityLockedByLayer(id)) continue;

            geo::AABB worldBounds;
            bool hasBounds = false;
            if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {
                const geo::AABB wb = be->worldBounds();
                if (!wb.isValid()) continue;
                worldBounds = wb;
                hasBounds = true;
            } else if (const auto* pe = std::get_if<scene::PointEntity>(&entity)) {
                worldBounds.expand(pe->transform.translation);
                hasBounds = true;
            } else if (const auto* me = std::get_if<scene::MeshEntity>(&entity)) {
                worldBounds.expand(me->transform.translation);
                hasBounds = true;
            }
            if (!hasBounds || !worldBounds.isValid()) continue;

            ImVec2 entMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            ImVec2 entMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            bool anyProjected = false;

            const std::array<glm::dvec3, 8> corners = {{
                {worldBounds.mins.x, worldBounds.mins.y, worldBounds.mins.z},
                {worldBounds.maxs.x, worldBounds.mins.y, worldBounds.mins.z},
                {worldBounds.maxs.x, worldBounds.maxs.y, worldBounds.mins.z},
                {worldBounds.mins.x, worldBounds.maxs.y, worldBounds.mins.z},
                {worldBounds.mins.x, worldBounds.mins.y, worldBounds.maxs.z},
                {worldBounds.maxs.x, worldBounds.mins.y, worldBounds.maxs.z},
                {worldBounds.maxs.x, worldBounds.maxs.y, worldBounds.maxs.z},
                {worldBounds.mins.x, worldBounds.maxs.y, worldBounds.maxs.z}
            }};

            for (const auto& c : corners) {
                ImVec2 sp{};
                if (!projectPoint(c, sp)) continue;
                anyProjected = true;
                entMin.x = std::min(entMin.x, sp.x);
                entMin.y = std::min(entMin.y, sp.y);
                entMax.x = std::max(entMax.x, sp.x);
                entMax.y = std::max(entMax.y, sp.y);
            }
            if (!anyProjected) {
                ImVec2 centerSp{};
                if (!projectPoint(worldBounds.center(), centerSp)) continue;
                entMin = centerSp;
                entMax = centerSp;
            }

            const bool contains =
                entMin.x >= minP.x && entMax.x <= maxP.x &&
                entMin.y >= minP.y && entMax.y <= maxP.y;
            const bool intersects =
                !(entMax.x < minP.x || entMin.x > maxP.x || entMax.y < minP.y || entMin.y > maxP.y);

            if ((crossing && intersects) || (!crossing && contains)) {
                selection_.add(id);
            }
        }

        setStatus(crossing ? "Marquee: crossing select" : "Marquee: window select", 1.5f);
    };

    // Camera orbit / pan / zoom
    if (!gizmoOver) {
        const bool entityTool =
            activeTool_ == ActiveTool::Select ||
            activeTool_ == ActiveTool::FaceSelect ||
            activeTool_ == ActiveTool::Clip;
        if (m.left && entityTool && !marquee_.dragging && !(activeTool_ == ActiveTool::Select && io.KeyShift))
            camera_.orbit(m.dx, m.dy);
        if (m.right || m.middle)    camera_.pan  (m.dx, m.dy);
    }
    if (m.scroll != 0.f) camera_.zoom(m.scroll);

    if (activeTool_ == ActiveTool::Select) {
        if (!marquee_.dragging && io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver) {
            marquee_.dragging = true;
            marquee_.start = ImGui::GetMousePos();
            marquee_.end = marquee_.start;
            return;
        }
        if (marquee_.dragging) {
            marquee_.end = ImGui::GetMousePos();
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                runMarqueeSelection();
                marquee_.dragging = false;
            }
            return;
        }
    }

    // Paint tool drag-to-paint handling
    if (activeTool_ == ActiveTool::Paint) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            paintDrag_.dragging = false;
            paintDrag_.paintedFaces.clear();
            paintDrag_.origMaterials.clear();
            setStatus("Paint cancelled.");
            return;
        }

        if (paintDrag_.dragging) {
            // Accumulate faces under current mouse position
            const ImVec2 rel = { ImGui::GetMousePos().x - vpPos.x,
                                 ImGui::GetMousePos().y - vpPos.y };
            const glm::vec2 ndc = {
                 (rel.x / vpSz.x) * 2.f - 1.f,
                -((rel.y / vpSz.y) * 2.f - 1.f)
            };
            
            if (const auto hit = pickFace(ndc, scene_, camera_, aspect)) {
                if (entityVisibleInEditor(hit->entityId) && !entityLockedByLayer(hit->entityId)) {
                    // Add to painted faces if not already painted
                    bool already = std::any_of(paintDrag_.paintedFaces.begin(), paintDrag_.paintedFaces.end(),
                        [&](const FaceSelection& f) {
                            return f.entityId == hit->entityId && f.brushIdx == hit->brushIdx && f.faceIdx == hit->faceIdx;
                        });
                    
                    if (!already && paintDrag_.paintedFaces.size() < 100) {  // Limit to 100 faces per drag
                        FaceSelection sel;
                        sel.entityId = hit->entityId;
                        sel.brushIdx = hit->brushIdx;
                        sel.faceIdx = hit->faceIdx;
                        paintDrag_.paintedFaces.push_back(sel);
                        
                        auto* e = scene_.getEntity(hit->entityId);
                        auto* be = std::get_if<scene::BrushEntity>(e);
                        if (be && hit->brushIdx < be->brushes.size() && hit->faceIdx < be->brushes[hit->brushIdx].faces.size()) {
                            paintDrag_.origMaterials.push_back(be->brushes[hit->brushIdx].faces[hit->faceIdx].materialId);
                        } else {
                            paintDrag_.origMaterials.push_back("default");
                        }
                    }
                }
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                // Commit all painted faces
                if (!paintDrag_.paintedFaces.empty()) {
                    std::vector<BatchSetFaceMaterialCommand::FaceRef> faceRefs;
                    for (size_t i = 0; i < paintDrag_.paintedFaces.size(); ++i) {
                        const auto& f = paintDrag_.paintedFaces[i];
                        faceRefs.push_back({
                            f.entityId, f.brushIdx, f.faceIdx,
                            paintDrag_.origMaterials[i],
                            std::string(paintMaterial_)
                        });
                    }
                    
                    commands_.push(std::make_unique<BatchSetFaceMaterialCommand>(faceRefs), scene_);
                    
                    // Rebuild affected entity meshes
                    std::unordered_set<scene::EntityId> affectedEntities;
                    for (const auto& f : paintDrag_.paintedFaces) {
                        affectedEntities.insert(f.entityId);
                        rebuildEntityMesh(f.entityId);
                    }
                    
                    setStatus(std::format("Painted {} faces", paintDrag_.paintedFaces.size()));
                }
                paintDrag_.dragging = false;
                paintDrag_.paintedFaces.clear();
                paintDrag_.origMaterials.clear();
            }
            return;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver) {
            // Start paint drag
            const ImVec2 rel = { ImGui::GetMousePos().x - vpPos.x,
                                 ImGui::GetMousePos().y - vpPos.y };
            const glm::vec2 ndc = {
                 (rel.x / vpSz.x) * 2.f - 1.f,
                -((rel.y / vpSz.y) * 2.f - 1.f)
            };
            
            if (const auto hit = pickFace(ndc, scene_, camera_, aspect)) {
                if (entityVisibleInEditor(hit->entityId) && !entityLockedByLayer(hit->entityId)) {
                    paintDrag_.dragging = true;
                    paintDrag_.startPos = ImGui::GetMousePos();
                    paintDrag_.paintedFaces.clear();
                    paintDrag_.origMaterials.clear();
                    
                    // Add first face
                    FaceSelection sel;
                    sel.entityId = hit->entityId;
                    sel.brushIdx = hit->brushIdx;
                    sel.faceIdx = hit->faceIdx;
                    paintDrag_.paintedFaces.push_back(sel);
                    
                    auto* e = scene_.getEntity(hit->entityId);
                    auto* be = std::get_if<scene::BrushEntity>(e);
                    if (be && hit->brushIdx < be->brushes.size() && hit->faceIdx < be->brushes[hit->brushIdx].faces.size()) {
                        paintDrag_.origMaterials.push_back(be->brushes[hit->brushIdx].faces[hit->faceIdx].materialId);
                    } else {
                        paintDrag_.origMaterials.push_back("default");
                    }
                    setStatus("Painting... drag to paint more faces, release to commit.");
                    selection_.selectEntity(hit->entityId);
                }
            }
        }
        return;
    }

    if (activeTool_ == ActiveTool::BoxCreate) {
        const double minSpan = snapEnabled_ ? static_cast<double>(gridSize_) : 16.0;

        auto snapGroundPoint = [&](glm::dvec3 p) {
            if (snapEnabled_) p = glm::dvec3(snapVec3(glm::vec3(p), gridSize_));
            p.y = 0.0;
            return p;
        };

        if (boxCreate_.draggingFootprint) {
            if (const auto gp = pickGroundPoint()) {
                glm::dvec3 p = snapGroundPoint(*gp);
                p.y = boxCreate_.start.y;
                boxCreate_.end = p;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                boxCreate_.draggingFootprint = false;
                boxCreate_.adjustingHeight = true;
                boxCreate_.heightStartMouseY = ImGui::GetMousePos().y;
                boxCreate_.height = std::max(boxCreate_.height, minSpan);
                setStatus("Drag up/down to set box height, click to confirm.");
            }
            return;
        }

        if (boxCreate_.adjustingHeight) {
            const float dy = boxCreate_.heightStartMouseY - ImGui::GetMousePos().y;
            double h = std::max(minSpan, static_cast<double>(dy));
            if (snapEnabled_) {
                h = std::max(minSpan,
                    std::round(h / static_cast<double>(gridSize_)) * static_cast<double>(gridSize_));
            }
            boxCreate_.height = h;

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver) {
                addDraggedBox(boxCreate_.start, boxCreate_.end, boxCreate_.height);
                boxCreate_.adjustingHeight = false;
                setStatus("Box created.");
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                boxCreate_.adjustingHeight = false;
                setStatus("Box create cancelled.");
            }
            return;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoOver) {
            if (const auto gp = pickGroundPoint()) {
                const glm::dvec3 p = snapGroundPoint(*gp);
                boxCreate_.start = p;
                boxCreate_.end = p;
                boxCreate_.height = minSpan;
                boxCreate_.heightStartMouseY = ImGui::GetMousePos().y;
                boxCreate_.draggingFootprint = true;
                boxCreate_.adjustingHeight = false;
                setStatus("Drag to set box footprint.");
            }
            return;
        }
    }

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
            const bool ctrlAdd = io.KeyCtrl;
            if (id != scene::kInvalidEntityId && entityVisibleInEditor(id) && !entityLockedByLayer(id)) {
                if (ctrlAdd) {
                    if (selection_.contains(id)) selection_.remove(id);
                    else                         selection_.add(id);
                } else {
                    selection_.set(id);
                }
                faceSelection_.clear();
            }
            else if (id != scene::kInvalidEntityId && !entityVisibleInEditor(id)) {
                setStatus("Layer is hidden.");
            }
            else if (id != scene::kInvalidEntityId && entityLockedByLayer(id)) {
                setStatus("Layer is locked.");
            }
            else {
                if (!ctrlAdd) { selection_.clear(); faceSelection_.clear(); }
            }
            break;
        }
        case ActiveTool::FaceSelect:
        case ActiveTool::FaceMove:
        case ActiveTool::Paint: {
            const auto hit = pickFace(ndc, scene_, camera_, aspect);
            if (hit && entityVisibleInEditor(hit->entityId) && !entityLockedByLayer(hit->entityId)) {
                registerBridgeFaceSelection(*hit);
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
            } else if (hit && !entityVisibleInEditor(hit->entityId)) {
                setStatus("Layer is hidden.");
            } else if (hit && entityLockedByLayer(hit->entityId)) {
                setStatus("Layer is locked.");
            } else {
                faceSelection_.clear();
            }
            break;
        }
        case ActiveTool::VertexSelect: {
            // Pick closest vertex to click ray
            const auto hit = pickFace(ndc, scene_, camera_, aspect);
            if (hit && entityVisibleInEditor(hit->entityId) && !entityLockedByLayer(hit->entityId)) {
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
            } else if (hit && !entityVisibleInEditor(hit->entityId)) {
                setStatus("Layer is hidden.");
            } else if (hit && entityLockedByLayer(hit->entityId)) {
                setStatus("Layer is locked.");
            } else {
                vertexSelection_.clear();
            }
            break;
        }
        default: {
            const auto id = pickEntity(ndc, scene_, camera_, aspect);
            if (id != scene::kInvalidEntityId && entityVisibleInEditor(id) && !entityLockedByLayer(id)) selection_.set(id);
            else if (id != scene::kInvalidEntityId && !entityVisibleInEditor(id)) setStatus("Layer is hidden.");
            else if (id != scene::kInvalidEntityId && entityLockedByLayer(id)) setStatus("Layer is locked.");
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
    if (!!selection_.empty()) return;
    if (activeTool_ == ActiveTool::FaceSelect ||
        activeTool_ == ActiveTool::FaceMove ||
        activeTool_ == ActiveTool::Clip ||
        activeTool_ == ActiveTool::Paint ||
        activeTool_ == ActiveTool::BoxCreate) return;

    auto* entity = scene_.getEntity(selection_.primary());
    if (!entity) return;

    const float aspect = vpSz.x / vpSz.y;
    glm::mat4 view = camera_.viewMatrix();
    glm::mat4 proj = camera_.projMatrix(aspect);
    glm::mat4 model = gpuData_.count(selection_.primary())
                    ? gpuData_[selection_.primary()].model
                    : glm::mat4(scene::entityTransform(*entity).matrix());

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(vpPos.x, vpPos.y, vpSz.x, vpSz.y);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (activeTool_ == ActiveTool::Select) op = ImGuizmo::TRANSLATE;
    if (activeTool_ == ActiveTool::Rotate) op = ImGuizmo::ROTATE;
    if (activeTool_ == ActiveTool::Scale)  op = ImGuizmo::SCALE;

    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                         op, ImGuizmo::WORLD, glm::value_ptr(model));

    const bool wasUsing = gizmoDragging_;
    const bool nowUsing = ImGuizmo::IsUsing();

    if (!wasUsing && nowUsing) {
        gizmoDragStartPos_ = glm::vec3(scene::entityTransform(*entity).translation);
        gizmoDragging_ = true;
    }

    if (nowUsing) {
        if (auto it = gpuData_.find(selection_.primary()); it != gpuData_.end())
            it->second.model = model;
        glm::vec3 pos{0.f}, scl{1.f}, skw{0.f};
        glm::vec4 psp{0.f};
        glm::quat rot{1.f, 0.f, 0.f, 0.f};
        if (glm::decompose(model, scl, rot, pos, skw, psp)) {
            std::visit([&](auto& ent) {
                ent.transform.translation = glm::dvec3(pos);
                ent.transform.rotation    = glm::dquat(rot);
                ent.transform.scale       = glm::dvec3(scl);
            }, *entity);
        }
    } else if (wasUsing && !nowUsing) {
        gizmoDragging_ = false;
        const glm::vec3 newPos = glm::vec3(scene::entityTransform(*entity).translation);
        if (glm::length(newPos - gizmoDragStartPos_) > 1e-4f) {
            glm::vec3 snapped = newPos;
            if (snapEnabled_) snapped = snapVec3(snapped, gridSize_);
            commands_.push(std::make_unique<MoveEntityCommand>(
                selection_.primary(),
                glm::dvec3(gizmoDragStartPos_),
                glm::dvec3(snapped)), scene_);
            // Only update transform, don't rebuild mesh (geometry hasn't changed)
            updateEntityTransform(selection_.primary());
            setStatus(std::format("Moved entity to ({:.0f}, {:.0f}, {:.0f})", snapped.x, snapped.y, snapped.z));
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

    ImGui::SeparatorText("Layers");
    std::unordered_set<std::string> layerSet;
    layerSet.insert("Default");
    for (const auto& [_, layer] : entityLayers_) layerSet.insert(layer);
    std::vector<std::string> layers(layerSet.begin(), layerSet.end());
    std::sort(layers.begin(), layers.end());

    for (const auto& layer : layers) {
        bool vis = layerVisible(layer);
        if (ImGui::Checkbox(std::format("##vis_{}", layer).c_str(), &vis)) {
            layerVisibility_[layer] = vis;
            if (!vis && !selection_.empty()) {
                if (!entityVisibleInEditor(selection_.primary())) {
                    selection_.clear();
                    faceSelection_.clear();
                    vertexSelection_.clear();
                }
            }
        }
        ImGui::SameLine();
        const bool solo = (!soloLayer_.empty() && soloLayer_ == layer);
        if (ImGui::SmallButton(std::format("{}##solo_{}", solo ? "S*" : "S", layer).c_str())) {
            soloLayer_ = solo ? std::string{} : layer;
            if (!selection_.empty() && !entityVisibleInEditor(selection_.primary())) {
                selection_.clear();
                faceSelection_.clear();
                vertexSelection_.clear();
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(layer.c_str());
        ImGui::SameLine();
        bool lock = layerLocked(layer);
        if (ImGui::Checkbox(std::format("L##lock_{}", layer).c_str(), &lock)) {
            layerLocked_[layer] = lock;
        }
        ImGui::SameLine();
        glm::vec3 tint = layerTint(layer);
        if (ImGui::ColorEdit3(std::format("##tint_{}", layer).c_str(), glm::value_ptr(tint),
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            layerTint_[layer] = tint;
        }
    }

    if (!soloLayer_.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Solo")) soloLayer_.clear();
    }

    ImGui::SetNextItemWidth(-90.f);
    ImGui::InputTextWithHint("##new_layer", "New layer", newLayerName_, sizeof(newLayerName_));
    ImGui::SameLine();
    if (ImGui::Button("Add Layer") && newLayerName_[0] != '\0') {
        layerVisibility_[newLayerName_] = true;
        layerLocked_[newLayerName_] = false;
            layerTint_[newLayerName_] = {1.f, 1.f, 1.f};
        newLayerName_[0] = '\0';
    }

    ImGui::Separator();

    std::unordered_set<std::string> groupSet;
    for (const auto& [_, g] : entityGroups_) {
        if (!g.empty()) groupSet.insert(g);
    }
    std::vector<std::string> groups(groupSet.begin(), groupSet.end());
    std::sort(groups.begin(), groups.end());
    std::string groupLabel = groupFilter_.empty() ? "All Groups" : groupFilter_;
    if (ImGui::BeginCombo("Group Filter", groupLabel.c_str())) {
        if (ImGui::Selectable("All Groups", groupFilter_.empty())) groupFilter_.clear();
        for (const auto& group : groups) {
            const bool selected = (group == groupFilter_);
            if (ImGui::Selectable(group.c_str(), selected)) groupFilter_ = group;
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    for (const auto& [id, entity] : scene_.entities) {
        const bool sel  = (selection_.primary() == id);
        const bool isBE = std::holds_alternative<scene::BrushEntity>(entity);
        const bool isPE = std::holds_alternative<scene::PointEntity>(entity);
        const char* ico = isBE ? "[B]" : isPE ? "[P]" : "[M]";
        const std::string layer = entityLayer(id);
        const std::string group = entityGroup(id);
        const bool hidden = !layerVisible(layer);
        const bool groupHidden = !entityVisibleByGroup(id);
        const bool entityHidden = hiddenEntities_.contains(id);
        const bool entityIsolatedOut = !isolatedEntities_.empty() && !isolatedEntities_.contains(id);
        const bool locked = layerLocked(layer);
        ImGui::TextDisabled("%s",ico); ImGui::SameLine();

        if (renamingId_ == id) {
            // ── Inline rename InputText ──────────────────────────────────────
            ImGui::SetNextItemWidth(-1.f);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##treeRename", treeRenameBuffer_, sizeof(treeRenameBuffer_),
                                  ImGuiInputTextFlags_EnterReturnsTrue |
                                  ImGuiInputTextFlags_AutoSelectAll)) {
                const std::string& cur = scene::entityName(entity);
                if (treeRenameBuffer_[0] && cur != treeRenameBuffer_)
                    commands_.push(std::make_unique<RenameEntityCommand>(
                        id, cur, treeRenameBuffer_), scene_);
                renamingId_ = scene::kInvalidEntityId;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))      renamingId_ = scene::kInvalidEntityId;
            if (!ImGui::IsItemActive() && renamingId_ == id) renamingId_ = scene::kInvalidEntityId;
        } else {
            // ── Normal tree-node row ─────────────────────────────────────────
            ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_SpanFullWidth;
            if (sel) fl |= ImGuiTreeNodeFlags_Selected;
            const std::string displayName = std::format(
                "{}{}{}{}{}{}{}##{}",
                hidden ? "[H] " : "",
                groupHidden ? "[G] " : "",
                entityHidden ? "[X] " : entityIsolatedOut ? "[I] " : "",
                locked ? "[L] " : "",
                scene::entityName(entity),
                group.empty() ? "" : "  {",
                group.empty() ? "" : (group + "}"),
                id);
            const bool open = ImGui::TreeNodeEx(
                displayName.c_str(), fl);
            if (ImGui::IsItemClicked()) {
                if (!entityVisibleInEditor(id)) {
                    setStatus("Entity hidden by layer/group filter.");
                } else if (locked) {
                    setStatus("Layer is locked.");
                } else if (sel) {
                    selection_.clear(); faceSelection_.clear();
                } else {
                    selection_.set(id); faceSelection_.clear();
                }
            }
            // Double-click to rename inline
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                !locked && entityVisibleInEditor(id)) {
                selection_.set(id);
                renamingId_ = id;
                std::snprintf(treeRenameBuffer_, sizeof(treeRenameBuffer_),
                              "%s", scene::entityName(entity).c_str());
            }
            if (ImGui::BeginPopupContextItem()) {
                selection_.set(id);
                if (ImGui::MenuItem("Rename")) {
                    renamingId_ = id;
                    std::snprintf(treeRenameBuffer_, sizeof(treeRenameBuffer_),
                                  "%s", scene::entityName(entity).c_str());
                }
                if (ImGui::MenuItem("Hide")) {
                    selection_.set(id);
                    hideSelection();
                }
                if (ImGui::MenuItem("Isolate")) {
                    selection_.set(id);
                    isolateSelection();
                }
                if (ImGui::MenuItem("Show All Hidden / Clear Isolation", nullptr, false,
                    !hiddenEntities_.empty() || !isolatedEntities_.empty())) {
                    clearHiddenIsolation();
                }
                if (ImGui::MenuItem("Delete")) {
                    commands_.push(std::make_unique<DeleteEntityCommand>(id), scene_);
                    gpuData_.erase(id); selection_.remove(id); faceSelection_.clear();
                    entityLayers_.erase(id);
                    entityGroups_.erase(id);
                }
                if (isBE && ImGui::MenuItem("Save as Prefab...")) {
                    selection_.set(id);
                    savePrefabFromSelection();
                }
                if (isBE && ImGui::MenuItem("Frame Camera")) {
                    const auto* be = std::get_if<scene::BrushEntity>(&entity);
                    camera_.frameAABB(be->worldBounds());
                }
                if (isBE && ImGui::MenuItem("CSG Subtract Preview (this=cutter)")) {
                    selection_.set(id);
                    beginCSGPreviewSubtract();
                }
                ImGui::EndPopup();
            }
            if (open) ImGui::TreePop();
        }
    }
    ImGui::End();
}

// ─── Properties ───────────────────────────────────────────────────────────────

void EditorApp::drawInspector() {
    drawProperties();
}

void EditorApp::drawProperties() {
    ImGui::Begin("Inspector");

    ImGui::TextColored({0.6f,0.8f,1.f,1.f}, "Entity / Brush Inspector");
    if (!selection_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu selected", selection_.size());
    }
    ImGui::Separator();

    if (!selection_.empty()) {
        if (ImGui::Button("Frame Selected", {-1.f, 0.f})) {
            const geo::AABB combined = selection_.combinedBounds(scene_);
            if (combined.isValid()) camera_.frameAABB(combined);
        }
        if (ImGui::Button("Hide Selected", {-1.f, 0.f})) hideSelection();
        if (ImGui::Button("Isolate Selected", {-1.f, 0.f})) isolateSelection();
        if (ImGui::Button("Show All Hidden / Clear Isolation", {-1.f, 0.f})) clearHiddenIsolation();
        ImGui::Separator();
    }

    // ── Vertex properties ────────────────────────────────────────────────────
    if (vertexSelection_.valid() && activeTool_ == ActiveTool::VertexSelect) {
        drawVertexProperties_impl(*this, scene_, vertexSelection_, commands_,
            [this](scene::EntityId id) { rebuildEntityMesh(id); });
        ImGui::Separator();
    }

    // ── Face properties (takes priority when face selected) ──────────────────
    if (faceSelection_.valid())
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
    if (!!selection_.empty()) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::End(); return;
    }

    auto* entity = scene_.getEntity(selection_.primary());
    if (!entity) { selection_.clear(); ImGui::End(); return; }

    ImGui::TextColored({0.6f,0.8f,1.f,1.f}, "Entity #%llu",
        static_cast<unsigned long long>(selection_.primary()));
    ImGui::Separator();

    ImGui::SeparatorText("Organization");
    {
        const scene::EntityId id = selection_.primary();
        std::string curLayer = entityLayer(id);
        std::unordered_set<std::string> layerSet;
        layerSet.insert("Default");
        for (const auto& [_, layer] : entityLayers_) layerSet.insert(layer);
        std::vector<std::string> layers(layerSet.begin(), layerSet.end());
        std::sort(layers.begin(), layers.end());

        if (ImGui::BeginCombo("Layer", curLayer.c_str())) {
            for (const auto& layer : layers) {
                const bool selected = (layer == curLayer);
                if (ImGui::Selectable(layer.c_str(), selected) && layer != curLayer) {
                    commands_.push(std::make_unique<SetEntityLayerCommand>(
                        id,
                        entityLayers_,
                        layerVisibility_,
                        layerLocked_,
                        layerTint_,
                        curLayer,
                        layer), scene_);
                    setStatus(std::format("Entity moved to layer '{}'", layer), 1.5f);
                    curLayer = layer;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        std::string curGroup = entityGroup(id);
        char groupBuf[64];
        std::snprintf(groupBuf, sizeof(groupBuf), "%s", curGroup.c_str());
        if (ImGui::InputText("Group", groupBuf, sizeof(groupBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            const std::string nextGroup = groupBuf;
            if (nextGroup != curGroup) {
                commands_.push(std::make_unique<SetEntityGroupCommand>(
                    id,
                    entityGroups_,
                    curGroup,
                    nextGroup), scene_);
                setStatus(std::format("Entity group -> '{}'", nextGroup), 1.5f);
            }
        }
    }

    ImGui::Separator();

    // Name
    const std::string& cur = scene::entityName(*entity);
    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", cur.c_str());
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##name", renameBuffer_, sizeof(renameBuffer_),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (renameBuffer_[0] && cur != renameBuffer_)
            commands_.push(std::make_unique<RenameEntityCommand>(
                selection_.primary(), cur, renameBuffer_), scene_);
    }

    // Transform
    ImGui::SeparatorText("Transform");
    const scene::Transform& tf = scene::entityTransform(*entity);
    glm::vec3 pos = glm::vec3(tf.translation);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 1.f)) {
        std::visit([&](auto& e) { e.transform.translation = glm::dvec3(pos); }, *entity);
        if (auto it = gpuData_.find(selection_.primary()); it != gpuData_.end())
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

        auto* beMut = const_cast<scene::BrushEntity*>(be);
        char classBuf[128];
        std::snprintf(classBuf, sizeof(classBuf), "%s", be->classname.c_str());
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::InputText("Classname", classBuf, sizeof(classBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            beMut->classname = classBuf;
            ensureClassDefaults(*beMut, globalRegistry().find(beMut->classname));
        }

        if (const auto* def = globalRegistry().find(beMut->classname)) {
            ImGui::TextDisabled("%s", def->description.c_str());
            if (ImGui::Button("Add Missing Class Defaults"))
                ensureClassDefaults(*beMut, def);

            if (!beMut->properties.empty() || !def->properties.empty()) {
                ImGui::SeparatorText("Properties");
                ensureClassDefaults(*beMut, def);
                for (auto& [k, v] : beMut->properties) {
                    drawEntityPropertyEditor(def->findProp(k), k, v);
                }
            }
        } else if (!beMut->properties.empty()) {
            ImGui::SeparatorText("Properties");
            for (auto& [k, v] : beMut->properties)
                drawEntityPropertyEditor(nullptr, k, v);
        }

        ImGui::SeparatorText("Operations");

        // CSG Subtract
        if (ImGui::Button("CSG Subtract Preview (this=cutter)", {-1.f, 0.f}))
            beginCSGPreviewSubtract();
        ImGui::SetItemTooltip("Previews carving this entity out of overlapping solid brushes. Use Apply/Cancel in the preview bar.");

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
        auto* peMut = const_cast<scene::PointEntity*>(pe);
        ImGui::LabelText("Class", "%s", peMut->classname.c_str());
        if (const auto* def = globalRegistry().find(peMut->classname)) {
            ImGui::TextDisabled("%s", def->description.c_str());
            if (ImGui::Button("Add Missing Class Defaults"))
                ensureClassDefaults(*peMut, def);
            ensureClassDefaults(*peMut, def);
        }
        if (!peMut->properties.empty()) {
            ImGui::SeparatorText("Properties");
            for (auto& [k, v] : peMut->properties)
                drawEntityPropertyEditor(globalRegistry().find(peMut->classname)
                    ? globalRegistry().find(peMut->classname)->findProp(k)
                    : nullptr, k, v);
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
            if (snapEnabled_) dist = snapF(dist, gridSize_);
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
    ImGui::Spacing();

    // UV controls
    ImGui::SeparatorText("UV Mapping");

    // Helpers for drag-based UV undo coalescing
    auto snapshotUV = [&]() {
        if (!uvDragState_.active)
            uvDragState_ = { face.uvOffset, face.uvScale, face.uvRotation, true };
    };
    auto commitUVDrag = [&]() {
        if (!uvDragState_.active) return;
        uvDragState_.active = false;
        const SetFaceUVCommand::UVState oldState{ uvDragState_.offset, uvDragState_.scale, uvDragState_.rotation };
        const SetFaceUVCommand::UVState newState{ face.uvOffset, face.uvScale, face.uvRotation };
        if (oldState.offset != newState.offset || oldState.scale != newState.scale ||
            oldState.rotation != newState.rotation) {
            commands_.push(std::make_unique<SetFaceUVCommand>(
                faceSelection_.entityId, faceSelection_.brushIdx, faceSelection_.faceIdx,
                oldState, newState), scene_);
        }
    };

    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat2("Offset", glm::value_ptr(face.uvOffset), 0.5f);
    if (ImGui::IsItemActivated())           snapshotUV();
    if (ImGui::IsItemDeactivatedAfterEdit()) { brush.invalidate(); rebuildEntityMesh(faceSelection_.entityId); commitUVDrag(); }

    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat2("Scale",  glm::value_ptr(face.uvScale),  0.01f, 0.01f, 64.f);
    if (ImGui::IsItemActivated())           snapshotUV();
    if (ImGui::IsItemDeactivatedAfterEdit()) { brush.invalidate(); rebuildEntityMesh(faceSelection_.entityId); commitUVDrag(); }

    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragFloat("Rotation",&face.uvRotation, 1.f, -180.f, 180.f);
    if (ImGui::IsItemActivated())           snapshotUV();
    if (ImGui::IsItemDeactivatedAfterEdit()) { brush.invalidate(); rebuildEntityMesh(faceSelection_.entityId); commitUVDrag(); }

    auto applyFaceUv = [&] {
        brush.invalidate();
        rebuildEntityMesh(faceSelection_.entityId);
    };

    // Snapshot UV before any button press so we can make a single undo entry
    const SetFaceUVCommand::UVState uvBeforeButtons{ face.uvOffset, face.uvScale, face.uvRotation };
    bool uvChanged = false;
    if (ImGui::Button("Fit 0..1")) {
        const auto poly = brush.facePolygon(faceSelection_.faceIdx);
        if (poly.size() >= 3) {
            const glm::dvec3 nrm = glm::abs(face.plane.normal);
            glm::dvec3 uAxis{1.0, 0.0, 0.0};
            glm::dvec3 vAxis{0.0, 0.0, 1.0};
            if (nrm.y > nrm.x && nrm.y > nrm.z) {
                uAxis = {1.0, 0.0, 0.0}; vAxis = {0.0, 0.0, 1.0};
            } else if (nrm.x > nrm.z) {
                uAxis = {0.0, 1.0, 0.0}; vAxis = {0.0, 0.0, 1.0};
            } else {
                uAxis = {1.0, 0.0, 0.0}; vAxis = {0.0, 1.0, 0.0};
            }

            double minU = std::numeric_limits<double>::max();
            double maxU = std::numeric_limits<double>::lowest();
            double minV = std::numeric_limits<double>::max();
            double maxV = std::numeric_limits<double>::lowest();
            for (const auto& lp : poly) {
                const glm::dvec3 wp = be->transform.transformPoint(lp);
                const double u = glm::dot(wp, uAxis);
                const double v = glm::dot(wp, vAxis);
                minU = std::min(minU, u); maxU = std::max(maxU, u);
                minV = std::min(minV, v); maxV = std::max(maxV, v);
            }
            const double spanU = std::max(maxU - minU, 1.0);
            const double spanV = std::max(maxV - minV, 1.0);
            face.uvScale = glm::vec2(static_cast<float>(spanU), static_cast<float>(spanV));
            face.uvOffset = glm::vec2(
                static_cast<float>(-minU / spanU),
                static_cast<float>(-minV / spanV));
            face.uvRotation = 0.0f;
            uvChanged = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Align Left"))  { face.uvOffset.x -= 0.25f; uvChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Align Right")) { face.uvOffset.x += 0.25f; uvChanged = true; }
    if (ImGui::Button("Align Up"))    { face.uvOffset.y += 0.25f; uvChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Align Down"))  { face.uvOffset.y -= 0.25f; uvChanged = true; }

    if (ImGui::Button("Rot -15")) { face.uvRotation -= 15.f; uvChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Rot +15")) { face.uvRotation += 15.f; uvChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Rot -45")) { face.uvRotation -= 45.f; uvChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Rot +45")) { face.uvRotation += 45.f; uvChanged = true; }
    if (ImGui::Button("Rot -90")) { face.uvRotation -= 90.f; uvChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Rot +90")) { face.uvRotation += 90.f; uvChanged = true; }

    if (uvChanged) {
        applyFaceUv();
        const SetFaceUVCommand::UVState uvAfterButtons{ face.uvOffset, face.uvScale, face.uvRotation };
        commands_.push(std::make_unique<SetFaceUVCommand>(
            faceSelection_.entityId, faceSelection_.brushIdx, faceSelection_.faceIdx,
            uvBeforeButtons, uvAfterButtons), scene_);
    }

    // Apply UV changes
    if (ImGui::IsItemDeactivatedAfterEdit() ||
        ImGui::IsItemActivated()) // trigger rebuild after any UV drag
    {
        rebuildEntityMesh(faceSelection_.entityId);
    }

    ImGui::SeparatorText("Face Ops");
    static double extrudeDistance = 64.0;
    static double insetAmount = 8.0;
    static double insetDepth = 16.0;
    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragScalar("Extrude Distance", ImGuiDataType_Double, &extrudeDistance, 1.0, nullptr, nullptr, "%.1f");
    if (ImGui::Button("Extrude Face", {-1.f, 0.f})) {
        if (snapEnabled_) extrudeDistance = snapF(extrudeDistance, gridSize_);
        extrudeSelectedFace(extrudeDistance);
    }

    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragScalar("Inset Amount", ImGuiDataType_Double, &insetAmount, 0.5, nullptr, nullptr, "%.1f");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::DragScalar("Inset Depth", ImGuiDataType_Double, &insetDepth, 0.5, nullptr, nullptr, "%.1f");
    if (ImGui::Button("Inset Face", {-1.f, 0.f})) {
        if (snapEnabled_) {
            insetAmount = snapF(insetAmount, gridSize_);
            insetDepth = snapF(insetDepth, gridSize_);
        }
        insetSelectedFace(insetAmount, insetDepth);
    }

    ImGui::BeginDisabled(!bridgeSourceFace_.has_value());
    if (ImGui::Button("Bridge With Previous Face", {-1.f, 0.f})) {
        bridgeSelectedFaces();
    }
    ImGui::EndDisabled();
    if (bridgeSourceFace_.has_value()) {
        ImGui::TextDisabled("Bridge source: #%llu b%zu f%zu",
            static_cast<unsigned long long>(bridgeSourceFace_->entityId),
            bridgeSourceFace_->brushIdx,
            bridgeSourceFace_->faceIdx);
    } else {
        ImGui::TextDisabled("Bridge source: select a first face, then a second one.");
    }
}

// ─── Clip properties ─────────────────────────────────────────────────────────

void EditorApp::drawClipProperties() {
    ImGui::SeparatorText("Clip Tool");

    if (!!selection_.empty()) {
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
    auto* entity = scene_.getEntity(selection_.primary());
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
        selection_.primary(), origBrushes, result), scene_);

    rebuildEntityMesh(selection_.primary());
    faceSelection_.clear();
    setStatus(std::format("Clipped → {} brush(es)", result.size()));
}

// ─── Apply: CSG subtract ─────────────────────────────────────────────────────

void EditorApp::beginCSGPreviewSubtract() {
    csgPreview_.active = true;
    csgPreview_.op = CSGPreviewOp::Subtract;
    rebuildCSGPreview();
}

void EditorApp::beginCSGPreviewUnion() {
    csgPreview_.active = true;
    csgPreview_.op = CSGPreviewOp::Union;
    rebuildCSGPreview();
}

void EditorApp::beginCSGPreviewIntersect() {
    csgPreview_.active = true;
    csgPreview_.op = CSGPreviewOp::Intersect;
    rebuildCSGPreview();
}

void EditorApp::beginCSGPreviewXor() {
    csgPreview_.active = true;
    csgPreview_.op = CSGPreviewOp::Xor;
    rebuildCSGPreview();
}

void EditorApp::cancelCSGPreview() {
    csgPreview_.active = false;
    csgPreview_.valid = false;
    csgPreview_.op = CSGPreviewOp::None;
    csgPreview_.message.clear();
    csgPreview_.meshes.clear();
}

void EditorApp::rebuildCSGPreview() {
    auto opLabel = [&](CSGPreviewOp op) -> const char* {
        switch (op) {
        case CSGPreviewOp::Subtract: return "subtract";
        case CSGPreviewOp::Union: return "union";
        case CSGPreviewOp::Intersect: return "intersect";
        case CSGPreviewOp::Xor: return "xor";
        default: return "none";
        }
    };

    csgPreview_.meshes.clear();
    csgPreview_.valid = false;
    csgPreview_.message.clear();

    auto makePreviewMesh = [&](const scene::BrushEntity& be) {
        if (be.brushes.empty()) return;
        EntityGPUData d;
        d.model = glm::mat4(be.transform.matrix());
        d.mesh = gfx::GPUEntityMesh::upload(build::buildEntityMesh(be, false));
        if (!d.mesh.empty()) csgPreview_.meshes.push_back(std::move(d));
    };

    if (!csgPreview_.active || csgPreview_.op == CSGPreviewOp::None) return;

    if (csgPreview_.op == CSGPreviewOp::Subtract) {
        if (selection_.empty()) {
            csgPreview_.message = "Select a brush entity as cutter.";
            return;
        }
        auto* cutterEnt = scene_.getEntity(selection_.primary());
        auto* cutterBE = cutterEnt ? std::get_if<scene::BrushEntity>(cutterEnt) : nullptr;
        if (!cutterBE) {
            csgPreview_.message = "CSG Subtract requires a brush entity cutter.";
            return;
        }

        std::size_t affected = 0;
        for (const auto& [id, entity] : scene_.entities) {
            if (id == selection_.primary()) continue;
            const auto* be = std::get_if<scene::BrushEntity>(&entity);
            if (!be || !be->solid) continue;
            if (!be->worldBounds().overlaps(cutterBE->worldBounds())) continue;

            std::vector<geo::Brush> fragments;
            for (const auto& subjectBrush : be->brushes) {
                auto result = geo::csgSubtract(subjectBrush, std::span<const geo::Brush>(cutterBE->brushes));
                for (auto& f : result) fragments.push_back(std::move(f));
            }
            if (fragments.empty()) continue;

            scene::BrushEntity previewBE = *be;
            previewBE.brushes = std::move(fragments);
            for (auto& b : previewBE.brushes) b.invalidate();
            makePreviewMesh(previewBE);
            ++affected;
        }

        csgPreview_.valid = !csgPreview_.meshes.empty();
        csgPreview_.message = csgPreview_.valid
            ? std::format("Preview: {} affected entity(ies), {} preview mesh(es)", affected, csgPreview_.meshes.size())
            : "No overlapping solid brushes for subtract.";
        return;
    }

    if (selection_.ids.size() != 2) {
        csgPreview_.message = "Select exactly 2 brush entities.";
        return;
    }

    auto* aEnt = scene_.getEntity(selection_.ids[0]);
    auto* bEnt = scene_.getEntity(selection_.ids[1]);
    auto* aBE = aEnt ? std::get_if<scene::BrushEntity>(aEnt) : nullptr;
    auto* bBE = bEnt ? std::get_if<scene::BrushEntity>(bEnt) : nullptr;
    if (!aBE || !bBE) {
        csgPreview_.message = "CSG preview requires two brush entities.";
        return;
    }

    std::vector<geo::Brush> result;
    for (const auto& aBrush : aBE->brushes) {
        for (const auto& bBrush : bBE->brushes) {
            std::vector<geo::Brush> fragments;
            switch (csgPreview_.op) {
            case CSGPreviewOp::Union:
                fragments = geo::csgUnion(aBrush, bBrush);
                break;
            case CSGPreviewOp::Intersect:
                fragments = geo::csgIntersect(aBrush, bBrush);
                break;
            case CSGPreviewOp::Xor:
                fragments = geo::csgXor(aBrush, bBrush);
                break;
            default:
                break;
            }
            for (auto& f : fragments) result.push_back(std::move(f));
        }
    }

    if (result.empty()) {
        csgPreview_.message = "Preview generated no resulting brushes.";
        return;
    }

    scene::BrushEntity previewBE;
    previewBE.name = std::format("preview_{}", opLabel(csgPreview_.op));
    previewBE.transform = aBE->transform;
    previewBE.brushes = std::move(result);
    for (auto& b : previewBE.brushes) b.invalidate();
    makePreviewMesh(previewBE);

    csgPreview_.valid = !csgPreview_.meshes.empty();
    csgPreview_.message = csgPreview_.valid
        ? std::format("Preview: {} result brush(es)", previewBE.brushes.size())
        : "Preview mesh build failed.";
}

void EditorApp::drawCSGPreviewOverlay() {
    auto opLabel = [&](CSGPreviewOp op) -> const char* {
        switch (op) {
        case CSGPreviewOp::Subtract: return "Subtract";
        case CSGPreviewOp::Union: return "Union";
        case CSGPreviewOp::Intersect: return "Intersect";
        case CSGPreviewOp::Xor: return "XOR";
        default: return "None";
        }
    };

    rebuildCSGPreview();

    ImGui::SetNextWindowSize({460.f, 0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({18.f, 70.f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("CSG Preview", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Mode: %s", opLabel(csgPreview_.op));
    if (!csgPreview_.message.empty()) ImGui::TextDisabled("%s", csgPreview_.message.c_str());
    ImGui::TextDisabled("Preview meshes: %zu", csgPreview_.meshes.size());
    ImGui::Separator();

    ImGui::BeginDisabled(!csgPreview_.valid);
    if (ImGui::Button("Apply", {180.f, 0.f})) applyCSGPreview();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {180.f, 0.f})) cancelCSGPreview();
    ImGui::TextDisabled("Result is rendered in cyan overlay until Apply.");

    ImGui::End();
}

void EditorApp::applyCSGPreview() {
    if (!csgPreview_.active || !csgPreview_.valid) return;
    switch (csgPreview_.op) {
    case CSGPreviewOp::Subtract:
        applyCSGSubtract();
        break;
    case CSGPreviewOp::Union:
        applyCSGUnion();
        break;
    case CSGPreviewOp::Intersect:
        applyCSGIntersect();
        break;
    case CSGPreviewOp::Xor:
        applyCSGXor();
        break;
    default:
        break;
    }
    cancelCSGPreview();
}

void EditorApp::applyCSGSubtract() {
    if (!!selection_.empty()) return;
    auto* entity = scene_.getEntity(selection_.primary());
    if (!entity) return;
    auto* cutterBE = std::get_if<scene::BrushEntity>(entity);
    if (!cutterBE) return;

    auto cmd = std::make_unique<CSGSubtractCommand>(
        selection_.primary(), *cutterBE);
    commands_.push(std::move(cmd), scene_);

    selection_.clear();
    faceSelection_.clear();
    rebuildAllMeshes();
    setStatus("CSG Subtract applied.");
}

// ─── Apply: CSG union ─────────────────────────────────────────────────────────

void EditorApp::applyCSGUnion() {
    if (selection_.ids.size() != 2) return;
    auto* aEnt = scene_.getEntity(selection_.ids[0]);
    auto* bEnt = scene_.getEntity(selection_.ids[1]);
    if (!aEnt || !bEnt) return;
    auto* aBE = std::get_if<scene::BrushEntity>(aEnt);
    auto* bBE = std::get_if<scene::BrushEntity>(bEnt);
    if (!aBE || !bBE) return;

    auto cmd = std::make_unique<CSGUnionCommand>(
        selection_.ids[0], selection_.ids[1], *aBE, *bBE);
    commands_.push(std::move(cmd), scene_);

    selection_.clear();
    faceSelection_.clear();
    rebuildAllMeshes();
    setStatus("CSG Union applied.");
}

// ─── Apply: CSG intersect ─────────────────────────────────────────────────────

void EditorApp::applyCSGIntersect() {
    if (selection_.ids.size() != 2) return;
    auto* aEnt = scene_.getEntity(selection_.ids[0]);
    auto* bEnt = scene_.getEntity(selection_.ids[1]);
    if (!aEnt || !bEnt) return;
    auto* aBE = std::get_if<scene::BrushEntity>(aEnt);
    auto* bBE = std::get_if<scene::BrushEntity>(bEnt);
    if (!aBE || !bBE) return;

    auto cmd = std::make_unique<CSGIntersectCommand>(
        selection_.ids[0], selection_.ids[1], *aBE, *bBE);
    commands_.push(std::move(cmd), scene_);

    selection_.clear();
    faceSelection_.clear();
    rebuildAllMeshes();
    setStatus("CSG Intersect applied.");
}

// ─── Apply: CSG xor ───────────────────────────────────────────────────────────

void EditorApp::applyCSGXor() {
    if (selection_.ids.size() != 2) return;
    auto* aEnt = scene_.getEntity(selection_.ids[0]);
    auto* bEnt = scene_.getEntity(selection_.ids[1]);
    if (!aEnt || !bEnt) return;
    auto* aBE = std::get_if<scene::BrushEntity>(aEnt);
    auto* bBE = std::get_if<scene::BrushEntity>(bEnt);
    if (!aBE || !bBE) return;

    auto cmd = std::make_unique<CSGXorCommand>(
        selection_.ids[0], selection_.ids[1], *aBE, *bBE);
    commands_.push(std::move(cmd), scene_);

    selection_.clear();
    faceSelection_.clear();
    rebuildAllMeshes();
    setStatus("CSG XOR applied.");
}

// ─── Apply: hollow ───────────────────────────────────────────────────────────

void EditorApp::applyHollow() {
    if (!!selection_.empty()) return;
    auto* entity = scene_.getEntity(selection_.primary());
    if (!entity) return;
    auto* be = std::get_if<scene::BrushEntity>(entity);
    if (!be || be->brushes.size() != 1) return;

    auto cmd = std::make_unique<HollowEntityCommand>(selection_.primary(), *be);
    cmd->wallThickness = hollowThickness_;
    const std::string srcLayer = entityLayer(selection_.primary());
    const std::string srcGroup = entityGroup(selection_.primary());
    auto* hCmdPtr = cmd.get();
    commands_.push(std::move(cmd), scene_);
    // Propagate layer/group from source to the generated wall entities
    for (auto wallId : hCmdPtr->wallIds) {
        entityLayers_[wallId] = srcLayer;
        entityGroups_[wallId] = srcGroup;
        if (!layerVisibility_.contains(srcLayer)) layerVisibility_[srcLayer] = true;
        if (!layerLocked_.contains(srcLayer))     layerLocked_[srcLayer]     = false;
        if (!layerTint_.contains(srcLayer))       layerTint_[srcLayer]       = {1.f,1.f,1.f};
    }
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
        ImGuiWindowFlags_NoNav);

    if (statusTimer_ > 0.f)
        ImGui::Text("%s", statusMessage_.c_str());
    else
    {
        if (!selection_.empty())
            ImGui::TextDisabled("FORGE Editor  —  %s  |  %zu selected",
                scene_.name.c_str(), selection_.size());
        else
            ImGui::TextDisabled("FORGE Editor  —  %s", scene_.name.c_str());
    }

    ImGui::SameLine();
    const auto st = scene_.stats();
    const std::string r = std::format("  {:.0f} fps  |  {} DC  |  {} tri  |  {} brushes  |  grid: {}{}",
        window_.fps(), renderer_.drawCallCount(), renderer_.triangleCount(),
        st.totalBrushCount, gridLabel(gridSize_), snapEnabled_ ? " [snap]" : "");
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x
                         - ImGui::CalcTextSize(r.c_str()).x - 4.f);
    ImGui::TextDisabled("%s", r.c_str());
    ImGui::End();
}

// ─── File I/O ─────────────────────────────────────────────────────────────────

void EditorApp::newScene()  { buildDefaultScene(); scriptEnv_.bindScene(&scene_); }

void EditorApp::exportOBJ() {
    auto sel = pfd::save_file("Export OBJ", (std::filesystem::current_path() / (scene_.name)).string(),
        { "OBJ File", "*.obj", "All Files", "*" }).result();
    if (sel.empty()) return;
    const auto p = std::filesystem::path(sel).replace_extension("");
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
    for (auto& fbo : quadFbos_) fbo.destroy();
    textureCache_.evictAll();
    meshAssetCache_.evictAll();
    bloomRenderer_.shutdown();
    audioEngine_.shutdown();
    scriptEnv_.shutdown();
    skyboxRenderer_.shutdown();
    shadowMap_.shutdown();
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

void EditorApp::enterPlayModeAudio() {
    if (!audioInitialised_) {
        if (audioEngine_.init()) {
            audioEngine_.setSoundRoot(std::filesystem::current_path() / "sounds");
            audioInitialised_ = true;
        }
    }
    // Play ambient sounds from target_speaker entities
    for (const auto& [id, entity] : scene_.entities) {
        const auto* pe = std::get_if<scene::PointEntity>(&entity);
        if (!pe || pe->classname != "target_speaker") continue;
        const auto& props = pe->properties;
        std::string noise;
        float vol = 1.f;
        if (props.contains("noise")) {
            if (const auto* s = std::get_if<std::string>(&props.at("noise"))) noise = *s;
        }
        if (props.contains("volume")) {
            if (const auto* f = std::get_if<float>(&props.at("volume"))) vol = *f;
        }
        if (!noise.empty())
            audioEngine_.play2D(noise, vol, true);
    }
}

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

    // Respawn if player falls below kill plane
    {
        static constexpr float kKillPlane = -2048.f;
        if (runtime_->playerPosition().y < kKillPlane) {
            glm::vec3 spawnPos = glm::vec3(camera_.target);
            auto [id, pe] = scene_.findByClassname("info_player_start");
            if (pe) spawnPos = glm::vec3(pe->transform.translation);
            runtime_->teleportPlayer(spawnPos);
        }
    }

    // Crouch toggle (Left Ctrl)
    {
        static bool crouchHeld = false;
        const bool ctrlDown = window_.input().keys.ctrl;
        if (ctrlDown && !crouchHeld) {
            runtime_->toggleCrouch();
            crouchHeld = true;
        } else if (!ctrlDown) {
            crouchHeld = false;
        }
    }

    for (const auto& [id, entity] : scene_.entities) {
        if (std::holds_alternative<scene::BrushEntity>(entity))
            updateEntityTransform(id);
    }

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
        if (auto it = entityLayers_.find(id); it != entityLayers_.end()) {
            if (auto v = layerVisibility_.find(it->second); v != layerVisibility_.end() && !v->second)
                continue;
        }
        for (const auto& sub : data.mesh.submeshes)
            renderer_.submit({
                .mesh = &sub,
                .modelMatrix = data.model,
                .material = makeEditorMaterial(
                    materialLibrary_,
                    sub.materialId(),
                    materialColour(sub.materialId())),
            });
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
        ImGuiWindowFlags_NoNav);

    ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "▶ PLAY MODE");
    ImGui::Separator();
    ImGui::Text("%.0f fps", window_.fps());

    const glm::vec3 pos = runtime_->playerPosition();
    ImGui::Text("pos  %.0f  %.0f  %.0f", pos.x, pos.y, pos.z);
    ImGui::Text("yaw  %.0f°", runtime_->playerYaw());
    ImGui::Text("gnd  %s", runtime_->playerOnGround() ? "yes" : "no");
    ImGui::Text("cro  %s", runtime_->playerCrouched() ? "yes" : "no");

    ImGui::Separator();
    ImGui::TextDisabled("WASD  move    Q/E  jump");
    ImGui::TextDisabled("Shift  sprint  Ctrl  crouch");
    ImGui::TextDisabled("ESC  stop");
    ImGui::Checkbox("Trigger debug overlay", &showTriggerDebugOverlay_);

    if (showTriggerDebugOverlay_) {
        const auto triggers = runtime_->triggerDebugSnapshot();
        ImGui::SeparatorText("Triggers");
        ImGui::Text("player team: %s", runtime_->playerTeam().empty() ? "<unset>" : runtime_->playerTeam().c_str());

        if (triggers.empty()) {
            ImGui::TextDisabled("No runtime triggers");
        } else {
            for (const auto& t : triggers) {
                ImGui::PushID(static_cast<int>(t.entityId));
                ImGui::Text("#%llu  %s", static_cast<unsigned long long>(t.entityId), t.classname.c_str());
                if (!t.target.empty()) ImGui::TextDisabled("target: %s", t.target.c_str());
                ImGui::Text("filters: classname %s (%s) | team %s (%s)",
                            t.filterClassname.empty() ? "*" : t.filterClassname.c_str(),
                            t.classFilterPass ? "pass" : "fail",
                            t.filterTeam.empty() ? "*" : t.filterTeam.c_str(),
                            t.teamFilterPass ? "pass" : "fail");
                ImGui::Text("flags: once=%s once_per_entity=%s fired=%s",
                            t.once ? "yes" : "no",
                            t.oncePerEntity ? "yes" : "no",
                            t.fired ? "yes" : "no");

                if (t.occupants.empty()) {
                    ImGui::TextDisabled("  (no occupants)");
                } else {
                    ImGui::Indent();
                    for (const auto& occ : t.occupants) {
                        const ImVec4 col = occ.firedForThis ? ImVec4{1.0f, 0.8f, 0.2f, 1.f}
                                                           : ImVec4{0.6f, 0.8f, 1.0f, 1.f};
                        ImGui::TextColored(col, "  occupant #%llu (fired=%s)",
                                          static_cast<unsigned long long>(occ.entityId),
                                          occ.firedForThis ? "yes" : "no");
                    }
                    ImGui::Unindent();
                }

                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }

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

    glm::vec3 editPos = glm::vec3(vs.position);
    ImGui::SetNextItemWidth(-1.f);

    const bool changed = ImGui::DragFloat3("World Position",
        glm::value_ptr(editPos), 0.5f);

    if (changed) {
        // Access grid snap through the app reference passed in
        glm::vec3 editPosSnapped = editPos;
        const glm::dvec3 newWorld(editPosSnapped);
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
    auto result = pfd::save_file(
        "Save Scene", (currentFile_.empty()
            ? (std::filesystem::current_path() / (scene_.name + ".forge")).string()
            : currentFile_.string()),
        { "FORGE Scene", "*.forge", "All Files", "*" }).result();
    if (result.empty()) return;
    currentFile_ = std::filesystem::path(result);
    saveScene();
}

void EditorApp::openScene() {
    auto sel = pfd::open_file(
        "Open Scene", std::filesystem::current_path().string(),
        { "FORGE Scene", "*.forge", "All Files", "*" }).result();
    if (sel.empty()) return;
    const auto path = std::filesystem::path(sel[0]);
    auto result = serial::loadScene(path);
    if (result) {
        scene_ = std::move(*result);
        selection_.clear(); faceSelection_.clear(); vertexSelection_.clear();
        commands_.clear(); gpuData_.clear();
        entityLayers_.clear();
        entityGroups_.clear();
        hiddenEntities_.clear();
        isolatedEntities_.clear();
        layerVisibility_.clear();
        layerLocked_.clear();
        layerTint_.clear();
        soloLayer_.clear();
        groupFilter_.clear();
        layerVisibility_["Default"] = true;
        layerLocked_["Default"] = false;
        layerTint_["Default"] = {1.f, 1.f, 1.f};
        for (const auto& [id, _] : scene_.entities) {
            entityLayers_[id] = "Default";
            entityGroups_[id] = "";
        }
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
    auto sel = pfd::save_file("Export GLB", (std::filesystem::current_path() / scene_.name).string(),
        { "GLB File", "*.glb", "All Files", "*" }).result();
    if (sel.empty()) return;
    const auto p = std::filesystem::path(sel).replace_extension("");
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

// ─── Quad viewport ────────────────────────────────────────────────────────────

namespace forge::editor {

/// Helper: render the scene from one camera into an FBO and return the FBO.
static void renderToFbo(
    gfx::Framebuffer&     fbo,
    ImVec2                sz,
    gfx::Renderer&        renderer,
    gfx::GridRenderer&    grid,
    gfx::SkyboxRenderer&  skybox,
    gfx::MaterialLibrary& materialLibrary,
    const std::unordered_map<scene::EntityId, EditorApp::EntityGPUData>& gpuData,
    const std::unordered_map<scene::EntityId, std::string>& entityLayers,
    const std::unordered_map<scene::EntityId, std::string>& entityGroups,
    const std::unordered_map<std::string, bool>& layerVisibility,
    const std::unordered_map<std::string, glm::vec3>& layerTint,
    const std::vector<EditorApp::EntityGPUData>& previewMeshes,
    const std::string& soloLayer,
    const std::string& groupFilter,
    const scene::EntityId selectedId,
    const glm::mat4&      view,
    const glm::mat4&      proj,
    const glm::vec3&      camPos,
    bool                  wireframe,
    bool                  showGrid,
    bool                  showSkybox,
    bool                  isOrtho,
    const gfx::RenderFrame& baseFrame)
{
    fbo.resize((int)sz.x, (int)sz.y);
    fbo.bind();
    glClearColor(0.10f,0.10f,0.12f,1.f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    gfx::RenderFrame frame = baseFrame;
    frame.view      = view;
    frame.proj      = proj;
    frame.cameraPos = camPos;
    frame.wireframe = wireframe;

    if (showSkybox && skybox.valid()) {
        skybox.draw(glm::inverse(proj * view));
    }

    renderer.beginFrame(frame);
    for (const auto& [id, data] : gpuData) {
        const auto layerIt = entityLayers.find(id);
        const std::string layer = (layerIt != entityLayers.end()) ? layerIt->second : "Default";
        const bool vis = [&]() {
            if (!soloLayer.empty() && layer != soloLayer) return false;
            if (auto it = layerVisibility.find(layer); it != layerVisibility.end()) return it->second;
            return true;
        }();
        const bool groupVis = [&]() {
            if (groupFilter.empty()) return true;
            if (auto it = entityGroups.find(id); it != entityGroups.end()) return it->second == groupFilter;
            return false;
        }();
        if (!vis || !groupVis) continue;

        const bool sel = (selectedId == id);
        const glm::vec3 tint = [&]() {
            if (auto it = layerTint.find(layer); it != layerTint.end()) return it->second;
            return glm::vec3(1.f, 1.f, 1.f);
        }();
        for (const auto& sub : data.mesh.submeshes) {
            glm::vec3 col = glm::vec3(0.7f,0.7f,0.72f) * tint;
            if (sel) col = glm::mix(col, glm::vec3(0.3f,0.65f,1.f), 0.4f);
            renderer.submit({
                .mesh = &sub,
                .modelMatrix = data.model,
                .material = makeEditorMaterial(materialLibrary, sub.materialId(), col),
            });
        }
    }

    for (const auto& pd : previewMeshes) {
        for (const auto& sub : pd.mesh.submeshes) {
            renderer.submit({
                .mesh = &sub,
                .modelMatrix = pd.model,
                .material = makeEditorMaterial(materialLibrary, sub.materialId(), {0.15f, 0.95f, 0.95f}),
            });
        }
    }

    renderer.endFrame();

    if (showGrid && grid.valid() && !isOrtho) {
        grid.draw(view, proj, 1.f, 16384.f);
    }

    fbo.unbind();
}

void EditorApp::handleOrthoPaneMouse(int pane, ImVec2 panePos, ImVec2 paneSz) {
    if (!orthoEditMode_ || pane <= 0 || pane > 3) return;

    auto& oc = orthoCams_[pane - 1];
    const ImVec2 mouse = ImGui::GetMousePos();
    const glm::mat4 vp = oc.vpMatrix(paneSz.x, paneSz.y);

    auto projectPoint = [&](const glm::dvec3& worldPos, ImVec2& out) -> bool {
        const glm::vec4 clip = vp * glm::vec4(glm::vec3(worldPos), 1.f);
        if (std::abs(clip.w) < 1e-5f) return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out = {
            panePos.x + (ndc.x * 0.5f + 0.5f) * paneSz.x,
            panePos.y + (-ndc.y * 0.5f + 0.5f) * paneSz.y
        };
        return true;
    };

    auto worldBoundsForEntity = [&](const scene::Entity& entity) -> std::optional<geo::AABB> {
        if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {
            const geo::AABB wb = be->worldBounds();
            if (wb.isValid()) return wb;
            return std::nullopt;
        }
        geo::AABB box;
        const glm::dvec3 p = scene::entityTransform(entity).translation;
        box.expand(p - glm::dvec3{16.0, 16.0, 16.0});
        box.expand(p + glm::dvec3{16.0, 16.0, 16.0});
        return box;
    };

    auto pickEntity2D = [&]() -> scene::EntityId {
        scene::EntityId bestId = scene::kInvalidEntityId;
        float bestArea = std::numeric_limits<float>::max();
        for (const auto& [id, entity] : scene_.entities) {
            if (!entityVisibleInEditor(id) || entityLockedByLayer(id)) continue;
            const auto worldBounds = worldBoundsForEntity(entity);
            if (!worldBounds.has_value() || !worldBounds->isValid()) continue;

            ImVec2 minP{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            ImVec2 maxP{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
            bool anyProjected = false;
            const auto& b = *worldBounds;
            const std::array<glm::dvec3, 8> corners = {{
                {b.mins.x, b.mins.y, b.mins.z}, {b.maxs.x, b.mins.y, b.mins.z},
                {b.maxs.x, b.maxs.y, b.mins.z}, {b.mins.x, b.maxs.y, b.mins.z},
                {b.mins.x, b.mins.y, b.maxs.z}, {b.maxs.x, b.mins.y, b.maxs.z},
                {b.maxs.x, b.maxs.y, b.maxs.z}, {b.mins.x, b.maxs.y, b.maxs.z}
            }};
            for (const auto& c : corners) {
                ImVec2 sp{};
                if (!projectPoint(c, sp)) continue;
                anyProjected = true;
                minP.x = std::min(minP.x, sp.x);
                minP.y = std::min(minP.y, sp.y);
                maxP.x = std::max(maxP.x, sp.x);
                maxP.y = std::max(maxP.y, sp.y);
            }
            if (!anyProjected) continue;
            if (mouse.x < minP.x || mouse.x > maxP.x || mouse.y < minP.y || mouse.y > maxP.y) continue;
            const float area = std::max(1.f, (maxP.x - minP.x) * (maxP.y - minP.y));
            if (area < bestArea) {
                bestArea = area;
                bestId = id;
            }
        }
        return bestId;
    };

    if (ImGui::GetIO().MouseWheel != 0.f) oc.zoomBy(ImGui::GetIO().MouseWheel);
    const auto& m = window_.input().mouse;
    if (m.right || m.middle) {
        oc.pan(m.dx, m.dy);
        return;
    }

    if (orthoDragState_.active && orthoDragState_.pane == pane) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const glm::vec2 curMouse{mouse.x, mouse.y};
            const glm::vec2 deltaPx = curMouse - orthoDragState_.lastMouse;
            orthoDragState_.lastMouse = curMouse;

            glm::dvec3 worldDelta{0.0, 0.0, 0.0};
            switch (oc.dir) {
            case gfx::OrthoCamera::Dir::Top:   worldDelta = { deltaPx.x * oc.zoom, 0.0,  deltaPx.y * oc.zoom }; break;
            case gfx::OrthoCamera::Dir::Front: worldDelta = { deltaPx.x * oc.zoom, -deltaPx.y * oc.zoom, 0.0 }; break;
            case gfx::OrthoCamera::Dir::Right: worldDelta = { 0.0, -deltaPx.y * oc.zoom, -deltaPx.x * oc.zoom }; break;
            }

            for (auto& entry : orthoDragState_.entries) {
                if (auto* entity = scene_.getEntity(entry.id)) {
                    std::visit([&](auto& e) { e.transform.translation += worldDelta; }, *entity);
                    if (auto it = gpuData_.find(entry.id); it != gpuData_.end())
                        it->second.model = glm::mat4(scene::entityTransform(*entity).matrix());
                }
            }
            return;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            glm::dvec3 totalDelta{0.0, 0.0, 0.0};
            for (const auto& entry : orthoDragState_.entries) {
                if (auto* entity = scene_.getEntity(entry.id)) {
                    totalDelta = scene::entityTransform(*entity).translation - entry.startTranslation;
                    break;
                }
            }
            if (snapEnabled_) {
                totalDelta.x = snapF(totalDelta.x, gridSize_);
                totalDelta.y = snapF(totalDelta.y, gridSize_);
                totalDelta.z = snapF(totalDelta.z, gridSize_);
            }

            for (const auto& entry : orthoDragState_.entries)
                if (auto* entity = scene_.getEntity(entry.id))
                    std::visit([&](auto& e) { e.transform.translation = entry.startTranslation; }, *entity);

            if (glm::length(totalDelta) > 1e-4) {
                auto cmd = std::make_unique<BatchMoveCommand>();
                for (const auto& entry : orthoDragState_.entries)
                    cmd->entries.push_back({ entry.id, totalDelta });
                commands_.push(std::move(cmd), scene_);
                rebuildAllMeshes();
                setStatus(std::format("Ortho move ({:.0f}, {:.0f}, {:.0f})", totalDelta.x, totalDelta.y, totalDelta.z));
            } else {
                rebuildAllMeshes();
            }

            orthoDragState_ = {};
            return;
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto picked = pickEntity2D();
        if (picked != scene::kInvalidEntityId) {
            if (ImGui::GetIO().KeyCtrl) selection_.toggle(picked);
            else                        selection_.set(picked);
            faceSelection_.clear();
            vertexSelection_.clear();

            orthoDragState_ = {};
            orthoDragState_.active = true;
            orthoDragState_.pane = pane;
            orthoDragState_.entityId = picked;
            orthoDragState_.startMouse = { mouse.x, mouse.y };
            orthoDragState_.lastMouse = orthoDragState_.startMouse;
            for (auto id : selection_.ids)
                if (auto* entity = scene_.getEntity(id))
                    orthoDragState_.entries.push_back({ id, scene::entityTransform(*entity).translation });
            return;
        }
        selection_.clear();
        faceSelection_.clear();
        vertexSelection_.clear();
    }
}

void EditorApp::drawQuadViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    const ImVec2 total = ImGui::GetContentRegionAvail();
    if (total.x < 8.f || total.y < 8.f) { ImGui::End(); return; }

    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const ImVec2 cellSz = { total.x * 0.5f, total.y * 0.5f };
    const float  aspect  = cellSz.x / cellSz.y;
    const glm::vec3 camPos = camera_.position();

    // Build base frame (lights, fog, etc.)
    gfx::RenderFrame base;
    base.sunDirection = { -0.5f,-1.f,-0.5f };
    base.sunColor     = {  1.f, 0.95f, 0.8f };
    base.sunIntensity = 1.2f;
    base.ambientColor = { 0.06f, 0.06f, 0.08f };
    base.fogColor     = scene_.fog.color;
    base.fogDensity   = showFog_ ? scene_.fog.density : 0.f;
    {
        auto lights = scene_.findAllByClassname("light");
        for (auto& [lid, pe] : lights) {
            gfx::PointLight pl;
            pl.position  = glm::vec3(pe->transform.translation);
            pl.intensity = pe->property<float>("light", 300.f);
            pl.color     = pe->property<glm::vec3>("_color", glm::vec3{1.f,0.95f,0.8f});
            pl.radius    = pe->property<float>("radius", 512.f);
            base.pointLights.push_back(pl);
        }
    }

    // Pane configs: [0]=Perspective, [1]=Top, [2]=Front, [3]=Right
    static const char* kLabels[] = { "Perspective", "Top (-Y)", "Front (+Z)", "Right (-X)" };

    for (int pane = 0; pane < 4; ++pane) {
        const int col = pane % 2, row = pane / 2;
        ImVec2 panePos = {
            origin.x + col * cellSz.x,
            origin.y + row * cellSz.y
        };

        glm::mat4 view, proj;
        glm::vec3 eye;

        if (pane == 0) {
            // Perspective: main orbit camera
            view  = camera_.viewMatrix();
            proj  = camera_.projMatrix(aspect);
            eye   = camPos;
        } else {
            // Orthographic: one of the three axis views
            auto& oc = orthoCams_[pane - 1];
            view = oc.viewMatrix();
            proj = oc.projMatrix(cellSz.x, cellSz.y);
            eye  = { oc.target.x, oc.target.y + 4096.f, oc.target.z }; // approx
        }

        renderToFbo(quadFbos_[pane], cellSz, renderer_, gridRenderer_,
                    skyboxRenderer_, materialLibrary_, gpuData_,
                    entityLayers_, entityGroups_, layerVisibility_, layerTint_, csgPreview_.meshes,
                    soloLayer_, groupFilter_,
                    selection_.primary(), view, proj, eye,
                    wireframe_, showGrid_, showSkybox_, (pane != 0), base);

        // Display sub-viewport
        ImGui::SetCursorScreenPos(panePos);
        ImGui::Image(toImTextureID(quadFbos_[pane].colorTexture), cellSz, {0.f,1.f}, {1.f,0.f});

        // Active pane highlight
        const bool active = (pane == activeQuadPane_);
        const ImU32 border = active ? IM_COL32(80,180,255,200) : IM_COL32(60,60,60,120);
        ImGui::GetWindowDrawList()->AddRect(panePos,
            {panePos.x + cellSz.x, panePos.y + cellSz.y}, border, 0.f, 0, active ? 2.f : 1.f);

        // Label
        ImGui::GetWindowDrawList()->AddText(
            {panePos.x + 4.f, panePos.y + 4.f},
            IM_COL32(220,220,220,180), kLabels[pane]);

        // Click to activate + input
        if (ImGui::IsItemClicked()) activeQuadPane_ = pane;

        if (ImGui::IsItemHovered() && pane == activeQuadPane_) {
            const auto& m = window_.input().mouse;
            if (pane == 0) {
                // Perspective: orbit camera
                if (m.left)  camera_.orbit(m.dx, m.dy);
                if (m.right || m.middle) camera_.pan(m.dx, m.dy);
                if (m.scroll != 0.f) camera_.zoom(m.scroll);
            } else {
                handleOrthoPaneMouse(pane, panePos, cellSz);
            }
        }
    }

    ImGui::End();
}

} // namespace forge::editor — Quad viewport

// ─── Phase 8 panel implementations ───────────────────────────────────────────

namespace forge::editor {

// ─── Material browser ─────────────────────────────────────────────────────────

void EditorApp::drawMaterialBrowser() {
    ImGui::SetNextWindowSize({300.f, 420.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Browser", &showMaterialBrowser_);

    // Collect unique materialIds from scene
    std::vector<std::string> mats;
    for (const auto& [id, entity] : scene_.entities) {
        if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {
            for (const auto& brush : be->brushes)
                for (const auto& face : brush.faces)
                    if (std::ranges::find(mats, face.materialId) == mats.end())
                        mats.push_back(face.materialId);
        }
    }
    std::ranges::sort(mats);

    static char filter[128] = {};
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##matfilter", filter, sizeof(filter));
    ImGui::Separator();

    const float thumbSz = 48.f;
    const int   cols    = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (thumbSz + 8.f)));
    int         col     = 0;

    for (const auto& matId : mats) {
        if (filter[0] && matId.find(filter) == std::string::npos) continue;

        const uint32_t texId = textureCache_.load(matId);
        const bool     selected = (std::string(paintMaterial_) == matId);

        if (col > 0 && col < cols) ImGui::SameLine();
        else if (col >= cols) col = 0;

        ImGui::BeginGroup();

        // Thumbnail
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, {0.3f,0.6f,1.f,0.5f});

        const bool clicked = ImGui::ImageButton(
            matId.c_str(),
            toImTextureID(texId),
            {thumbSz, thumbSz}, {0.f,1.f}, {1.f,0.f});

        if (selected) ImGui::PopStyleColor();

        if (clicked) {
            std::snprintf(paintMaterial_, sizeof(paintMaterial_), "%s", matId.c_str());
            if (activeTool_ != ActiveTool::Paint) activeTool_ = ActiveTool::Paint;
            setStatus(std::format("Paint material: {}", matId));
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", matId.c_str());

        // Short label (last path component)
        const std::string lbl = [&] {
            const auto pos = matId.find_last_of("/\\");
            return pos == std::string::npos ? matId : matId.substr(pos + 1);
        }();
        ImGui::SetNextItemWidth(thumbSz);
        ImGui::TextUnformatted(lbl.substr(0, 8).c_str());

        ImGui::EndGroup();
        ++col;
    }

    if (mats.empty())
        ImGui::TextDisabled("No materials in scene.");

    ImGui::End();
}

// ─── Undo history panel ───────────────────────────────────────────────────────

void EditorApp::drawUndoHistory() {
    ImGui::SetNextWindowSize({280.f, 320.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Undo History", &showUndoHistory_);

    ImGui::TextDisabled("%zu / %zu", commands_.cursor(), commands_.size());
    ImGui::Separator();

    // Walk command history (CommandStack doesn't expose iteration directly,
    // so we track count via cursor and size)
    const std::size_t total  = commands_.size();
    const std::size_t cursor = commands_.cursor();

    if (total == 0) {
        ImGui::TextDisabled("No history.");
        ImGui::End();
        return;
    }

    // We can't iterate commands directly without exposing them —
    // show a summary of counts with current position highlighted
    for (std::size_t i = 0; i < total; ++i) {
        const bool isCurrent  = (i == cursor);
        const bool isFuture   = (i >= cursor);
        const bool isLastUndo = (i + 1 == cursor);

        if (isFuture)
            ImGui::PushStyleColor(ImGuiCol_Text, {0.45f, 0.45f, 0.45f, 1.f});

        const char* marker = isCurrent ? "► " : "  ";
        ImGui::Text("%s[%02zu]", marker, i + 1);

        if (isFuture)
            ImGui::PopStyleColor();

        if (isLastUndo) {
            ImGui::PushStyleColor(ImGuiCol_Separator, {0.3f,0.6f,1.f,0.8f});
            ImGui::Separator(); // blue line = current position
            ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!commands_.canUndo());
    if (ImGui::Button("Undo", {-1.f, 0.f})) { commands_.undo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!commands_.canRedo());
    if (ImGui::Button("Redo", {-1.f, 0.f})) { commands_.redo(scene_); rebuildAllMeshes(); }
    ImGui::EndDisabled();
    if (ImGui::Button("Clear History", {-1.f, 0.f})) commands_.clear();

    ImGui::End();
}

// ─── Entity class browser ─────────────────────────────────────────────────────

void EditorApp::drawEntityClassBrowser() {
    ImGui::SetNextWindowSize({320.f, 480.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Entity Classes", &showEntityClasses_);

    static char filter[128] = {};
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##ecfilter", filter, sizeof(filter));
    ImGui::Separator();

    for (const auto& cat : globalRegistry().categories()) {
        const auto defs = globalRegistry().inCategory(cat);
        bool anyMatch = false;
        for (const auto* d : defs)
            if (!filter[0] || d->classname.find(filter) != std::string::npos)
                anyMatch = true;
        if (!anyMatch) continue;

        if (!ImGui::CollapsingHeader(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;

        for (const auto* def : defs) {
            if (filter[0] && def->classname.find(filter) == std::string::npos) continue;

            // Coloured dot
            ImGui::ColorButton(def->classname.c_str(),
                { def->editorColor.r, def->editorColor.g, def->editorColor.b, 1.f },
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, {12.f,12.f});
            ImGui::SameLine();

            const bool open = ImGui::TreeNodeEx(def->classname.c_str(),
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanFullWidth);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", def->description.c_str());

            // Double-click to place at camera target
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                scene::EntityId id = scene::kInvalidEntityId;
                if (def->kind == EntityKind::Brush || def->kind == EntityKind::Both) {
                    scene::BrushEntity be;
                    be.name = def->classname;
                    be.classname = def->classname;
                    be.transform = scene::Transform::fromTranslation(glm::dvec3(camera_.target));
                    be.brushes = { geo::makeBox({-32.0, 0.0, -32.0}, {32.0, 64.0, 32.0}) };
                    ensureClassDefaults(be, def);
                    id = scene_.addEntity(std::move(be));
                    entityLayers_[id] = "Default";
                    entityGroups_[id] = "";
                    rebuildEntityMesh(id);
                } else {
                    scene::PointEntity pe;
                    pe.name      = def->classname;
                    pe.classname = def->classname;
                    pe.transform = scene::Transform::fromTranslation(glm::dvec3(camera_.target));
                    ensureClassDefaults(pe, def);
                    id = scene_.addEntity(std::move(pe));
                }
                selection_.set(id);
                setStatus(std::format("Placed '{}'", def->classname));
            }

            if (open) ImGui::TreePop();
        }
    }

    ImGui::End();
}

// ─── Prefab save / insert ─────────────────────────────────────────────────────

void EditorApp::savePrefabFromSelection() {
    if (!!selection_.empty()) return;
    const auto* entity = scene_.getEntity(selection_.primary());
    if (!entity) return;
    const auto* be = std::get_if<scene::BrushEntity>(entity);
    if (!be || be->brushes.empty()) {
        setStatus("Select a brush entity to save as prefab.");
        return;
    }

    Prefab p;
    p.name    = be->name;
    p.brushes = be->brushes;
    
    // Capture entity metadata (NEW in v1.0.0)
    p.solid   = be->solid;
    p.visible = be->visible;
    p.layer   = entityLayer(selection_.primary());  // Get from editor layer map

    const auto path = std::filesystem::current_path() / (be->name + ".fprefab");
    const auto err  = savePrefab(p, path);
    if (err.empty())
        setStatus(std::format("Saved prefab → {} (solid={}, visible={}, layer={})", 
                             path.string(), p.solid, p.visible, p.layer));
    else
        setStatus(std::format("Prefab save failed: {}", err));
}

void EditorApp::insertPrefab() {
    auto sel = pfd::open_file("Open Prefab", std::filesystem::current_path().string(),
        { "FORGE Prefab", "*.fprefab", "All Files", "*" }).result();
    if (sel.empty()) return;
    placePrefabFromFile(sel[0], glm::dvec3(camera_.target), false);
}

scene::EntityId EditorApp::placePrefabFromFile(const std::filesystem::path& prefabPath,
                                               const glm::dvec3& spawnPos,
                                               bool linked) {
    auto result = loadPrefab(prefabPath);
    if (!result) {
        setStatus(std::format("Prefab load failed: {}", result.error()));
        return scene::kInvalidEntityId;
    }

    PrefabInstantiationOptions opts;
    opts.position = spawnPos;

    auto e = instantiatePrefab(*result, opts);
    const auto id = scene_.addEntity(std::move(e));

    entityLayers_[id] = result->layer;
    entityGroups_[id] = "";
    if (linked) linkedPrefabSources_[id] = std::filesystem::absolute(prefabPath);

    if (!layerVisibility_.contains(result->layer)) {
        layerVisibility_[result->layer] = true;
        layerLocked_[result->layer] = false;
        layerTint_[result->layer] = {1.f, 1.f, 1.f};
    }

    rebuildEntityMesh(id);
    selection_.set(id);
    setStatus(std::format("Inserted prefab '{}'{}", result->name, linked ? " (linked)" : ""));
    return id;
}

void EditorApp::refreshPrefabLibrary() {
    prefabLibraryFiles_.clear();
    if (prefabLibraryRoot_.empty() || !std::filesystem::exists(prefabLibraryRoot_)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(prefabLibraryRoot_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".fprefab") prefabLibraryFiles_.push_back(entry.path());
    }
    std::sort(prefabLibraryFiles_.begin(), prefabLibraryFiles_.end());
}

void EditorApp::updateLinkedPrefabInstances() {
    std::size_t updated = 0;
    std::size_t removed = 0;

    for (auto it = linkedPrefabSources_.begin(); it != linkedPrefabSources_.end();) {
        auto* entity = scene_.getEntity(it->first);
        auto* be = entity ? std::get_if<scene::BrushEntity>(entity) : nullptr;
        if (!be) {
            it = linkedPrefabSources_.erase(it);
            ++removed;
            continue;
        }

        auto prefab = loadPrefab(it->second);
        if (!prefab) {
            ++it;
            continue;
        }

        be->brushes = prefab->brushes;
        be->solid = prefab->solid;
        be->visible = prefab->visible;
        entityLayers_[it->first] = prefab->layer;
        rebuildEntityMesh(it->first);
        ++updated;
        ++it;
    }

    setStatus(std::format("Updated {} linked prefabs{}",
                          updated,
                          removed ? std::format(" ({} stale links removed)", removed) : ""));
}

void EditorApp::drawPrefabBrowser() {
    ImGui::SetNextWindowSize({420.f, 520.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Prefab Browser", &showPrefabBrowser_);

    ImGui::SetNextItemWidth(-110.f);
    ImGui::InputText("##prefab_root", prefabRootBuffer_, sizeof(prefabRootBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Set Root")) {
        prefabLibraryRoot_ = prefabRootBuffer_;
        refreshPrefabLibrary();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshPrefabLibrary();

    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##prefab_search", "Search prefabs...", prefabSearch_, sizeof(prefabSearch_));
    ImGui::TextDisabled("%zu prefab(s)", prefabLibraryFiles_.size());
    ImGui::Separator();

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string filter = toLower(prefabSearch_);

    ImGui::BeginChild("##prefab_list", {0.f, -ImGui::GetFrameHeightWithSpacing() * 2.5f}, true);
    for (const auto& path : prefabLibraryFiles_) {
        const std::string name = path.stem().string();
        const std::string rel = std::filesystem::relative(path, prefabLibraryRoot_).string();
        const std::string hay = toLower(name + " " + rel);
        if (!filter.empty() && hay.find(filter) == std::string::npos) continue;

        ImGui::PushID(path.string().c_str());
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Place")) placePrefabFromFile(path, glm::dvec3(camera_.target), false);
        ImGui::SameLine();
        if (ImGui::SmallButton("Link")) placePrefabFromFile(path, glm::dvec3(camera_.target), true);
        ImGui::TextDisabled("%s", rel.c_str());

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const std::string abs = std::filesystem::absolute(path).string();
            const bool linkedDrag = ImGui::GetIO().KeyShift;
            ImGui::SetDragDropPayload(linkedDrag ? "FORGE_PREFAB_PATH_LINKED" : "FORGE_PREFAB_PATH",
                                      abs.c_str(),
                                      abs.size() + 1);
            ImGui::Text("Drag to viewport: %s", name.c_str());
            ImGui::TextDisabled("Hold Shift while dragging to create linked instance");
            ImGui::EndDragDropSource();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("Update Linked Instances")) updateLinkedPrefabInstances();
    ImGui::SameLine();
    if (ImGui::Button("Clear Broken Links")) {
        for (auto it = linkedPrefabSources_.begin(); it != linkedPrefabSources_.end();) {
            if (!scene_.getEntity(it->first)) it = linkedPrefabSources_.erase(it);
            else ++it;
        }
        setStatus(std::format("Linked instances tracked: {}", linkedPrefabSources_.size()));
    }
    ImGui::TextDisabled("Linked instances tracked: %zu", linkedPrefabSources_.size());

    ImGui::End();
}

} // namespace forge::editor — Phase 8 panels

// ─── Phase 9 additions ────────────────────────────────────────────────────────

namespace forge::editor {

// ─── Level validation panel ───────────────────────────────────────────────────

void EditorApp::runValidation() {
    lastValidation_ = validateScene(scene_, materialLibrary_);
    showValidation_ = true;
    const std::size_t errs = lastValidation_.errorCount();
    const std::size_t warn = lastValidation_.warningCount();
    if (errs > 0)
        setStatus(std::format("Validation: {} errors, {} warnings. ({} ms)", 
            errs, warn, lastValidation_.validationTime.count()));
    else if (warn > 0)
        setStatus(std::format("Validation passed with {} warnings. ({} ms)", 
            warn, lastValidation_.validationTime.count()));
    else
        setStatus("Validation passed — scene is clean.");
}

void EditorApp::drawValidationPanel() {
    ImGui::SetNextWindowSize({440.f, 360.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Validation", &showValidation_);

    if (ImGui::Button("Run Validation", {-1.f, 0.f})) runValidation();
    ImGui::Separator();

    if (lastValidation_.issues.empty()) {
        ImGui::TextColored({0.3f,0.8f,0.3f,1.f}, "No issues. Scene is clean.");
        ImGui::End();
        return;
    }

    // Summary
    const std::size_t errs = lastValidation_.errorCount();
    const std::size_t warn = lastValidation_.warningCount();
    const std::size_t info = lastValidation_.infoCount();

    if (errs > 0)
        ImGui::TextColored({1.f,0.3f,0.3f,1.f}, "[X] %zu error(s)", errs);
    ImGui::SameLine();
    if (warn > 0)
        ImGui::TextColored({1.f,0.8f,0.2f,1.f}, "[!] %zu warning(s)", warn);
    ImGui::SameLine();
    if (info > 0)
        ImGui::TextColored({0.6f,0.8f,1.f,1.f}, "[i] %zu info", info);
    ImGui::Separator();

    // Issue list
    ImGui::BeginChild("##issues", {0.f, 0.f}, false);
    for (const auto& issue : lastValidation_.issues) {
        ImVec4 col;
        switch (issue.level) {
        case ValidationIssue::Level::Error:
            col = {1.f,0.4f,0.4f,1.f}; break;
        case ValidationIssue::Level::Warning:
            col = {1.f,0.85f,0.3f,1.f}; break;
        default:
            col = {0.65f,0.85f,1.f,1.f}; break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(issue.levelIcon());
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (!issue.entity.empty() && issue.entity != "scene") {
            ImGui::TextDisabled("[%s]", issue.entity.c_str());
            ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", issue.message.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

// ─── Duplicate selection ─────────────────────────────────────────────────────

void EditorApp::duplicateSelection() {
    if (selection_.empty()) return;

    auto cmd = std::make_unique<DuplicateEntitiesCommand>();
    cmd->offset = { 32.0, 0.0, 32.0 };

    for (auto id : selection_.ids) {
        const auto* e = scene_.getEntity(id);
        if (!e) continue;
        const auto* be = std::get_if<scene::BrushEntity>(e);
        if (be) cmd->originals.push_back(*be);
    }

    if (cmd->originals.empty()) {
        setStatus("Nothing to duplicate (select brush entities).");
        return;
    }

    const auto duplicateCount = cmd->originals.size();
    auto* cmdPtr = cmd.get();
    commands_.push(std::move(cmd), scene_);
    // Select the newly created clones so user can immediately move them
    selection_.clear();
    for (auto id : cmdPtr->cloneIds) selection_.add(id);
    rebuildAllMeshes();
    setStatus(std::format("Duplicated {} entities.", duplicateCount));
}

void EditorApp::nudgeSelection(const glm::dvec3& delta) {
    if (selection_.empty()) return;

    std::vector<BatchMoveCommand::Entry> entries;
    entries.reserve(selection_.size());

    for (auto id : selection_.ids) {
        auto* entity = scene_.getEntity(id);
        if (!entity || !entityVisibleInEditor(id) || entityLockedByLayer(id)) continue;
        entries.push_back({ id, delta });
    }

    if (entries.empty()) return;

    auto cmd = std::make_unique<BatchMoveCommand>();
    cmd->entries = std::move(entries);
    const std::size_t count = cmd->entries.size();
    commands_.push(std::move(cmd), scene_);
    rebuildAllMeshes();
    setStatus(std::format("Moved {} entities by ({:.0f}, {:.0f}, {:.0f})", count, delta.x, delta.y, delta.z));
}

void EditorApp::hideSelection() {
    if (selection_.empty()) return;
    for (auto id : selection_.ids) hiddenEntities_.insert(id);
    selection_.clear();
    faceSelection_.clear();
    vertexSelection_.clear();
    bridgeSourceFace_.reset();
    setStatus("Selected entities hidden.");
}

void EditorApp::isolateSelection() {
    if (selection_.empty()) return;
    isolatedEntities_.clear();
    for (auto id : selection_.ids) {
        isolatedEntities_.insert(id);
        hiddenEntities_.erase(id);
    }
    reconcileEditorState();
    setStatus("Isolated selection.");
}

void EditorApp::clearHiddenIsolation() {
    hiddenEntities_.clear();
    isolatedEntities_.clear();
    reconcileEditorState();
    setStatus("Visibility reset.");
}

} // namespace forge::editor — Phase 9 additions

// ─── Phase 10 additions ───────────────────────────────────────────────────────

namespace forge::editor {

// ─── BSP build + panel ───────────────────────────────────────────────────────

void EditorApp::buildBSP() {
    bsp::BSPBuildOptions opts;
    opts.maxDepth     = 20;
    opts.splitPenalty = 8;
    opts.verbose      = true;

    bspTree_  = bsp::buildBSP(scene_, opts);
    bspBuilt_ = true;
    showBSPStats_ = true;

    const float ms = bspTree_.stats.buildTimeMs;
    setStatus(std::format("BSP built: {} nodes, {} leaves, {} splits, {:.1f}ms",
        bspTree_.stats.nodeCount,
        bspTree_.stats.leafCount,
        bspTree_.stats.splitCount,
        ms));
}

void EditorApp::drawBSPPanel() {
    ImGui::SetNextWindowSize({300.f, 280.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("BSP Statistics", &showBSPStats_);

    if (!bspBuilt_) {
        ImGui::TextDisabled("BSP not yet compiled.");
        if (ImGui::Button("Build BSP", {-1.f,0.f})) buildBSP();
        ImGui::End();
        return;
    }

    const auto& s = bspTree_.stats;

    if (ImGui::Button("Rebuild BSP", {-1.f,0.f})) buildBSP();
    ImGui::Separator();

    ImGui::LabelText("Nodes",       "%d",     s.nodeCount);
    ImGui::LabelText("Leaves",      "%d",     s.leafCount);
    ImGui::LabelText("Solid leaves","%d",     s.solidLeaves);
    ImGui::LabelText("Max depth",   "%d",     s.maxDepth);
    ImGui::LabelText("Splits",      "%d",     s.splitCount);
    ImGui::LabelText("Input faces", "%d",     s.inputFaces);
    ImGui::LabelText("Output faces","%d",     s.outputFaces);
    ImGui::LabelText("Balance",     "%.2f×",  s.balanceRatio);
    ImGui::LabelText("Build time",  "%.2f ms",s.buildTimeMs);

    ImGui::Separator();

    // Point-in-solid test at camera position
    const glm::vec3 camPos = camera_.position();
    const bool solid = bspTree_.isSolid(camPos);
    const int  leaf  = bspTree_.leafAt(camPos);

    ImGui::LabelText("Camera leaf",  "%d", leaf);
    ImGui::LabelText("Camera solid", "%s", solid ? "YES (in wall!)" : "no");

    if (solid)
        ImGui::TextColored({1.f,0.3f,0.3f,1.f}, "Camera is inside solid geometry!");

    ImGui::Separator();

    // BSP quality feedback
    if (s.balanceRatio < 1.5f)
        ImGui::TextColored({0.3f,0.9f,0.3f,1.f}, "Tree balance: excellent");
    else if (s.balanceRatio < 3.f)
        ImGui::TextColored({1.f,0.8f,0.2f,1.f}, "Tree balance: acceptable");
    else
        ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "Tree balance: poor — geometry may be irregular");

    if (s.splitCount > s.inputFaces)
        ImGui::TextColored({1.f,0.8f,0.2f,1.f},
            "High split count — consider simplifying geometry");

    ImGui::End();
}

// ─── MeshEntity rendering ─────────────────────────────────────────────────────

void EditorApp::drawMeshEntities(const gfx::RenderFrame& frame) {
    // Render all MeshEntities in the scene using the mesh asset cache
    for (const auto& [id, entity] : scene_.entities) {
        if (!entityVisibleInEditor(id)) continue;
        const auto* me = std::get_if<scene::MeshEntity>(&entity);
        if (!me || !me->visible || me->assetPath.empty()) continue;

        const auto* gpu = meshAssetCache_.load(me->assetPath);
        if (!gpu) continue;

        const glm::mat4 model = glm::mat4(me->transform.matrix());
        const bool sel = selection_.contains(id);
        const glm::vec3 tint = layerTint(entityLayer(id));

        for (const auto& sub : gpu->submeshes) {
            glm::vec3 col = glm::vec3(0.7f,0.7f,0.72f) * tint;
            if (sel) col = glm::mix(col, glm::vec3(0.3f,0.65f,1.f), 0.4f);
            renderer_.submit({
                .mesh = &sub,
                .modelMatrix = model,
                .material = makeEditorMaterial(materialLibrary_, sub.materialId(), col),
            });
        }
    }
}

} // namespace forge::editor — Phase 10 additions

// ─── Phase 11 additions ───────────────────────────────────────────────────────

namespace forge::editor {

// ─── Bloom settings panel ─────────────────────────────────────────────────────

void EditorApp::drawBloomSettings() {
    ImGui::SetNextWindowSize({280.f, 180.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bloom Settings", &showBloomSettings_);

    ImGui::Checkbox("Enable Bloom", &bloomEnabled_);
    ImGui::Separator();

    ImGui::BeginDisabled(!bloomEnabled_);
    ImGui::SliderFloat("Threshold",  &bloomRenderer_.threshold, 0.f, 2.f);
    ImGui::SliderFloat("Intensity",  &bloomRenderer_.intensity, 0.f, 4.f);
    ImGui::SliderInt  ("Blur Passes",&bloomRenderer_.passes,    1, 8);
    ImGui::SetItemTooltip("More passes = softer, wider bloom (but slower)");
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled("Pipeline: bright-pass → %dx H/V blur → composite",
        bloomRenderer_.passes);
    ImGui::End();
}

// ─── Audio settings panel ─────────────────────────────────────────────────────

void EditorApp::drawAudioSettings() {
    ImGui::SetNextWindowSize({300.f, 200.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Audio Settings", &showAudioSettings_);

    ImGui::TextColored(
        audioEngine_.valid() ? ImVec4{0.3f,0.9f,0.3f,1.f} : ImVec4{1.f,0.3f,0.3f,1.f},
        audioEngine_.valid() ? "Audio: ready" : "Audio: not initialised");
    ImGui::Separator();

    if (ImGui::SliderFloat("Master Volume", &masterVolume_, 0.f, 1.f))
        audioEngine_.setMasterVolume(masterVolume_);

    ImGui::Separator();
    ImGui::TextDisabled("Sound root: sounds/");
    ImGui::TextDisabled("32-slot pool, spatial 3D falloff");
    ImGui::Separator();

    // Quick test buttons
    if (!audioEngine_.valid()) ImGui::BeginDisabled();
    if (ImGui::Button("Test Beep (2D)")) {
        audioEngine_.play2D("test.wav", 0.5f);
        setStatus("Played test.wav (2D)");
    }
    if (!audioEngine_.valid()) ImGui::EndDisabled();

    // List target_speaker entities
    ImGui::SeparatorText("Speaker Entities");
    for (const auto& [id, ent] : scene_.entities) {
        const auto* pe = std::get_if<scene::PointEntity>(&ent);
        if (!pe || pe->classname != "target_speaker") continue;
        const auto it = pe->properties.find("noise");
        if (it == pe->properties.end()) continue;
        const std::string& file = std::get<std::string>(it->second);
        ImGui::TextDisabled("[%s] %s", pe->name.c_str(), file.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(std::format("Play##sp{}", id).c_str())) {
            audioEngine_.play3D(file, glm::vec3(pe->transform.translation));
            setStatus(std::format("Playing '{}'", file));
        }
    }
    ImGui::End();
}

// ─── Script console panel ─────────────────────────────────────────────────────

void EditorApp::drawScriptConsole() {
    ImGui::SetNextWindowSize({560.f, 360.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Script Console", &showScriptConsole_);

    script::ScriptEnv* env = &scriptEnv_;
    if (playMode_ && runtime_) env = &runtime_->scriptEnv();

    // Toolbar
    if (ImGui::Button("Clear")) env->clearLog();
    ImGui::SameLine();
    if (ImGui::Button("Reload Scene Binding")) env->bindScene(&scene_);
    ImGui::SameLine();
    ImGui::TextDisabled("Lua 5.4  |  forge.* API  |  Enter=exec");
    ImGui::Separator();

    // Log area
    ImGui::BeginChild("##log", {0.f, -ImGui::GetFrameHeightWithSpacing() - 4.f},
                       false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& entry : env->consoleLog()) {
        ImVec4 col;
        const char* prefix = "";
        switch (entry.kind) {
        case script::ConsoleEntry::Kind::Input:
            col = {0.7f, 0.9f, 1.f, 1.f};  prefix = ""; break;
        case script::ConsoleEntry::Kind::Output:
            col = {0.9f, 0.9f, 0.9f, 1.f}; prefix = ""; break;
        case script::ConsoleEntry::Kind::Error:
            col = {1.f, 0.4f, 0.4f, 1.f};  prefix = ""; break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted((std::string(prefix) + entry.text).c_str());
        ImGui::PopStyleColor();
    }

    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f)
        ImGui::SetScrollHereY(1.f);

    ImGui::EndChild();

    // Input line
    ImGui::SetNextItemWidth(-60.f);
    const bool execute =
        ImGui::InputText("##script", scriptInput_, sizeof(scriptInput_),
                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Run") || execute) {
        if (scriptInput_[0]) {
            env->exec(scriptInput_);
            scriptInput_[0] = '\0'; // clear after execution
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    ImGui::End();
}

// ─── Per-entity Lua update (play mode) ───────────────────────────────────────

void EditorApp::tickEntityScripts(float dt) {
    if (!scriptEnv_.valid()) return;
    for (const auto& [id, entity] : scene_.entities) {
        const auto* pe = std::get_if<scene::PointEntity>(&entity);
        if (!pe) continue;
        const auto it = pe->properties.find("script");
        if (it == pe->properties.end()) continue;
        const auto* code = std::get_if<std::string>(&it->second);
        if (!code || code->empty()) continue;
        scriptEnv_.updateEntity(id, *code, dt);
    }
}

// ─── Trigger target_speaker entities near the player (play mode) ─────────────

void EditorApp::triggerSpeakerEntities() {
    if (!audioEngine_.valid()) return;
    for (const auto& [id, entity] : scene_.entities) {
        const auto* pe = std::get_if<scene::PointEntity>(&entity);
        if (!pe || pe->classname != "target_speaker") continue;
        const auto noiseIt = pe->properties.find("noise");
        if (noiseIt == pe->properties.end()) continue;
        const auto* file = std::get_if<std::string>(&noiseIt->second);
        if (!file || file->empty()) continue;
        const float vol = pe->property<float>("volume", 1.f);
        audioEngine_.play3D(*file, glm::vec3(pe->transform.translation), vol);
    }
}

} // namespace forge::editor — Phase 11
