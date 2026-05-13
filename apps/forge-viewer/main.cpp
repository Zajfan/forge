#include <forge/geo.hpp>
#include <forge/scene.hpp>
#include <forge/build.hpp>
#include <forge/gfx.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <format>
#include <iostream>
#include <vector>

// ─── Scene construction ───────────────────────────────────────────────────────

static forge::scene::Scene buildScene() {
    using namespace forge;
    namespace fgeo = forge::geo;
    namespace fscn = forge::scene;

    fscn::Scene scene;
    scene.name = "forge_viewer";

    // Floor
    fscn::BrushEntity floor;
    floor.name    = "floor";
    floor.brushes = { fgeo::makeBox({-256, -16, -256}, {256, 0, 256}) };
    scene.addEntity(std::move(floor));

    // North wall with doorway
    auto northWall   = fgeo::makeBox({-256, 0, 240}, {256, 256, 256});
    auto doorCutter  = fgeo::makeBox({-32,  0, 239}, {32, 192, 257});
    auto wallFrags   = fgeo::csgSubtract(northWall, doorCutter);

    fscn::BrushEntity north;
    north.name    = "north_wall";
    north.brushes = std::move(wallFrags);
    scene.addEntity(std::move(north));

    // South / East / West walls
    for (auto& [name, mins, maxs] : std::vector<std::tuple<const char*, glm::dvec3, glm::dvec3>>{
        {"south_wall", {-256, 0, -256}, {256, 256, -240}},
        {"east_wall",  { 240, 0, -256}, {256, 256,  256}},
        {"west_wall",  {-256, 0, -256}, {-240,256,  256}},
    }) {
        fscn::BrushEntity e;
        e.name    = name;
        e.brushes = { fgeo::makeBox(mins, maxs) };
        scene.addEntity(std::move(e));
    }

    // Ceiling
    fscn::BrushEntity ceiling;
    ceiling.name    = "ceiling";
    ceiling.brushes = { fgeo::makeBox({-256, 256, -256}, {256, 272, 256}) };
    scene.addEntity(std::move(ceiling));

    // Octagonal pillar
    fscn::BrushEntity pillar;
    pillar.name    = "pillar";
    pillar.brushes = { fgeo::makePrism({64, 0, 64}, 24.0, 256.0, 8) };
    scene.addEntity(std::move(pillar));

    // Ramp
    fscn::BrushEntity ramp;
    ramp.name    = "ramp";
    ramp.brushes = { fgeo::makeWedge({-200, 0, -200}, {-100, 64, -100}) };
    scene.addEntity(std::move(ramp));

    return scene;
}

// ─── Material colour table ────────────────────────────────────────────────────
// Assign a pleasing flat colour based on material name hash.

static glm::vec3 materialColour(const std::string& matId) {
    static const std::vector<glm::vec3> kPalette = {
        { 0.72f, 0.70f, 0.65f },  // concrete grey
        { 0.55f, 0.50f, 0.45f },  // dark stone
        { 0.80f, 0.75f, 0.60f },  // warm plaster
        { 0.40f, 0.45f, 0.55f },  // slate blue
        { 0.65f, 0.58f, 0.50f },  // brown brick
        { 0.50f, 0.60f, 0.50f },  // mossy green
    };
    const std::size_t h = std::hash<std::string>{}(matId);
    return kPalette[h % kPalette.size()];
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    using namespace forge;

    // ── Window + GL context ───────────────────────────────────────────────────
    auto windowResult = gfx::Window::create({
        .title       = "FORGE Viewer",
        .width       = 1280,
        .height      = 720,
        .msaaSamples = 4,
        .glMajor     = 4,
        .glMinor     = 6,
    });

    if (!windowResult) {
        std::cerr << "[error] " << windowResult.error() << '\n';
        return 1;
    }

    gfx::Window window = std::move(*windowResult);

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window.sdlWindow(), window.glContext());
    ImGui_ImplOpenGL3_Init("#version 450");

    // ── Renderer ──────────────────────────────────────────────────────────────
    gfx::Renderer renderer;
    if (!renderer.init()) {
        std::cerr << "[error] Renderer init failed\n";
        return 1;
    }

    // ── Build scene ───────────────────────────────────────────────────────────
    std::cout << "Building scene...\n";
    const scene::Scene scene = buildScene();
    const auto sceneStats    = scene.stats();

    // ── Build + upload GPU meshes ─────────────────────────────────────────────
    std::cout << "Building and uploading meshes...\n";

    struct RenderEntity {
        scene::EntityId       id;
        gfx::GPUEntityMesh    gpuMesh;
        glm::mat4             model = glm::mat4(1.f);
    };

    std::vector<RenderEntity> renderEntities;

    for (const auto& [id, cpuMesh] : build::buildSceneMeshes(scene)) {
        // Model matrix: identity for now (all geometry already in world space)
        const glm::mat4 model = glm::mat4(1.f);

        RenderEntity re;
        re.id       = id;
        re.gpuMesh  = gfx::GPUEntityMesh::upload(cpuMesh);
        re.model    = model;
        renderEntities.push_back(std::move(re));
    }

    std::cout << std::format("  {} entities, {} render objects\n",
        sceneStats.brushEntityCount, renderEntities.size());

    // ── Camera ────────────────────────────────────────────────────────────────
    gfx::OrbitCamera camera;
    camera.frameAABB(scene.worldBounds());

    // ── UI state ──────────────────────────────────────────────────────────────
    bool wireframe    = false;
    bool showStats    = true;
    bool showHelp     = true;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!window.shouldClose()) {
        window.pollEvents();

        const auto& input = window.input();
        const float dt    = window.deltaTime();

        // ── Camera input ──────────────────────────────────────────────────────
        if (input.mouse.left && !ImGui::GetIO().WantCaptureMouse) {
            camera.orbit(input.mouse.dx, input.mouse.dy);
        }
        if ((input.mouse.right || input.mouse.middle) && !ImGui::GetIO().WantCaptureMouse) {
            camera.pan(input.mouse.dx, input.mouse.dy);
        }
        if (input.mouse.scroll != 0.f && !ImGui::GetIO().WantCaptureMouse) {
            camera.zoom(input.mouse.scroll);
        }

        // F1: toggle wireframe
        if (input.keys.f1) wireframe = !wireframe;

        // ── Render frame ──────────────────────────────────────────────────────
        const float aspect = window.aspectRatio();

        gfx::RenderFrame frame;
        frame.view       = camera.viewMatrix();
        frame.proj       = camera.projMatrix(aspect);
        frame.cameraPos  = camera.position();
        frame.wireframe  = wireframe;

        renderer.beginFrame(frame);

        for (const auto& re : renderEntities) {
            for (const auto& submesh : re.gpuMesh.submeshes) {
                auto material = std::make_shared<gfx::Material>(submesh.materialId());
                material->albedoColor = materialColour(submesh.materialId());
                renderer.submit({
                    .mesh        = &submesh,
                    .modelMatrix = re.model,
                    .material    = std::move(material),
                });
            }
        }

        renderer.endFrame();

        // ── ImGui ─────────────────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Stats overlay
        if (showStats) {
            ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.6f);
            ImGui::Begin("##stats", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

            ImGui::Text("FORGE Viewer");
            ImGui::Separator();
            ImGui::Text("FPS:        %.1f", window.fps());
            ImGui::Text("Draw calls: %u",  renderer.drawCallCount());
            ImGui::Text("Triangles:  %u",  renderer.triangleCount());
            ImGui::Separator();
            ImGui::Text("Entities:   %zu", sceneStats.brushEntityCount);
            ImGui::Text("Brushes:    %zu", sceneStats.totalBrushCount);
            ImGui::Text("Faces:      %zu", sceneStats.totalFaceCount);
            ImGui::Separator();
            ImGui::Text("Camera dist: %.1f", camera.distance);
            ImGui::Text("Wireframe:  %s",  wireframe ? "ON" : "off");

            ImGui::End();
        }

        // Help overlay
        if (showHelp) {
            const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos({ displaySize.x - 220.f, 8.f }, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::Begin("##help", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

            ImGui::Text("Controls");
            ImGui::Separator();
            ImGui::Text("Left drag    Orbit");
            ImGui::Text("Right drag   Pan");
            ImGui::Text("Scroll       Zoom");
            ImGui::Text("F1           Wireframe");
            ImGui::Text("ESC          Quit");

            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        window.swapBuffers();
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    return 0;
}
