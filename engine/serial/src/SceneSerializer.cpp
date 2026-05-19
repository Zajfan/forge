#include "forge/serial/SceneSerializer.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <format>
#include <memory>
#include <unordered_map>

using json = nlohmann::json;

namespace forge::serial {

// ─── Helpers: write ───────────────────────────────────────────────────────────

static json jVec3(glm::dvec3 v) { return { v.x, v.y, v.z }; }
static json jVec3f(glm::vec3 v) { return { v.x, v.y, v.z }; }
static json jVec2f(glm::vec2 v) { return { v.x, v.y }; }
static json jQuat(glm::dquat q) { return { q.x, q.y, q.z, q.w }; }

static json serialisePlane(const geo::Plane& p) {
    return { {"normal", jVec3(p.normal)}, {"distance", p.distance} };
}

static json serialiseFace(const geo::BrushFace& f) {
    return {
        {"plane",      serialisePlane(f.plane)},
        {"materialId", f.materialId},
        {"uvOffset",   jVec2f(f.uvOffset)},
        {"uvScale",    jVec2f(f.uvScale)},
        {"uvRotation", f.uvRotation},
    };
}

static json serialiseBrush(const geo::Brush& b) {
    json faces = json::array();
    for (const auto& f : b.faces) faces.push_back(serialiseFace(f));
    return { {"id", b.id}, {"faces", faces} };
}

static json serialiseTransform(const scene::Transform& t) {
    return {
        {"translation", jVec3(t.translation)},
        {"rotation",    jQuat(t.rotation)},
        {"scale",       jVec3(t.scale)},
    };
}

static json serialiseProperty(const scene::PropertyValue& v) {
    return std::visit([](const auto& val) -> json {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string>)
            return { {"t","s"}, {"v", val} };
        else if constexpr (std::is_same_v<T, int>)
            return { {"t","i"}, {"v", val} };
        else if constexpr (std::is_same_v<T, float>)
            return { {"t","f"}, {"v", val} };
        else if constexpr (std::is_same_v<T, bool>)
            return { {"t","b"}, {"v", val} };
        else  // glm::vec3
            return { {"t","v3"}, {"v", {val.x, val.y, val.z}} };
    }, v);
}

// ─── Helpers: read ────────────────────────────────────────────────────────────

static glm::dvec3 rVec3(const json& j) {
    return { j[0].get<double>(), j[1].get<double>(), j[2].get<double>() };
}
static glm::vec3  rVec3f(const json& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}
static glm::vec2  rVec2f(const json& j) {
    return { j[0].get<float>(), j[1].get<float>() };
}
static glm::dquat rQuat(const json& j) {
    return { j[3].get<double>(), j[0].get<double>(), j[1].get<double>(), j[2].get<double>() };
}

// Actually store plane directly (normal + distance)
static geo::Plane readPlane2(const json& j) {
    geo::Plane p;
    const auto& n = j["normal"];
    p.normal   = { n[0].get<double>(), n[1].get<double>(), n[2].get<double>() };
    p.distance = j["distance"].get<double>();
    return p;
}

static geo::BrushFace readFace(const json& j) {
    geo::BrushFace f;
    f.plane      = readPlane2(j["plane"]);
    f.materialId = j["materialId"].get<std::string>();
    f.uvOffset   = rVec2f(j["uvOffset"]);
    f.uvScale    = rVec2f(j["uvScale"]);
    f.uvRotation = j["uvRotation"].get<float>();
    return f;
}

static geo::Brush readBrush(const json& j) {
    geo::Brush b;
    b.id = j["id"].get<std::string>();
    for (const auto& fj : j["faces"]) b.faces.push_back(readFace(fj));
    return b;
}

static scene::Transform readTransform(const json& j) {
    scene::Transform t;
    t.translation = rVec3(j["translation"]);
    t.rotation    = rQuat(j["rotation"]);
    t.scale       = rVec3(j["scale"]);
    return t;
}

static scene::PropertyValue readProperty(const json& j) {
    const std::string t = j["t"].get<std::string>();
    const auto& v = j["v"];
    if (t == "s")  return v.get<std::string>();
    if (t == "i")  return v.get<int>();
    if (t == "f")  return v.get<float>();
    if (t == "b")  return v.get<bool>();
    // v3
    return glm::vec3{ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
}

static json serialiseMaterials(const gfx::MaterialLibrary& materials) {
    json out = json::object();
    for (const auto& [name, mat] : materials.materials()) {
        if (!mat) continue;
        json mj = json::object();
        for (const auto& [k, v] : mat->toJson()) mj[k] = v;
        out[name] = std::move(mj);
    }
    return out;
}

static gfx::MaterialLibrary readMaterials(const json& root) {
    gfx::MaterialLibrary library;
    if (!root.contains("materials") || !root["materials"].is_object()) return library;

    for (const auto& [name, mj] : root["materials"].items()) {
        if (!mj.is_object()) continue;
        std::unordered_map<std::string, std::string> data;
        for (const auto& [k, v] : mj.items()) {
            if (v.is_string()) data[k] = v.get<std::string>();
            else data[k] = v.dump();
        }
        if (!data.contains("name") || data["name"].empty()) data["name"] = name;
        auto mat = std::make_shared<gfx::Material>(gfx::Material::fromJson(data));
        if (mat->name.empty()) mat->name = name;
        library.addMaterial(std::move(mat));
    }
    return library;
}

static json serialiseEditorMetadata(const EditorMetadata& editor) {
    json j;
    json layersByEntity = json::object();
    for (const auto& [id, layer] : editor.entityLayers)
        layersByEntity[std::to_string(id)] = layer;

    json groupsByEntity = json::object();
    for (const auto& [id, group] : editor.entityGroups)
        groupsByEntity[std::to_string(id)] = group;

    json layerVis = json::object();
    for (const auto& [name, visible] : editor.layerVisibility)
        layerVis[name] = visible;

    json layerLock = json::object();
    for (const auto& [name, locked] : editor.layerLocked)
        layerLock[name] = locked;

    json layerTint = json::object();
    for (const auto& [name, tint] : editor.layerTint)
        layerTint[name] = jVec3f(tint);

    j["entityLayers"] = std::move(layersByEntity);
    j["entityGroups"] = std::move(groupsByEntity);
    j["layerVisibility"] = std::move(layerVis);
    j["layerLocked"] = std::move(layerLock);
    j["layerTint"] = std::move(layerTint);
    j["soloLayer"] = editor.soloLayer;
    j["groupFilter"] = editor.groupFilter;
    return j;
}

static EditorMetadata readEditorMetadata(const json& root) {
    EditorMetadata editor;
    if (!root.contains("editor") || !root["editor"].is_object()) return editor;
    const auto& j = root["editor"];

    if (j.contains("entityLayers") && j["entityLayers"].is_object()) {
        for (const auto& [idStr, layer] : j["entityLayers"].items()) {
            if (!layer.is_string()) continue;
            try {
                const scene::EntityId id = static_cast<scene::EntityId>(std::stoull(idStr));
                editor.entityLayers[id] = layer.get<std::string>();
            } catch (...) {}
        }
    }

    if (j.contains("entityGroups") && j["entityGroups"].is_object()) {
        for (const auto& [idStr, group] : j["entityGroups"].items()) {
            if (!group.is_string()) continue;
            try {
                const scene::EntityId id = static_cast<scene::EntityId>(std::stoull(idStr));
                editor.entityGroups[id] = group.get<std::string>();
            } catch (...) {}
        }
    }

    if (j.contains("layerVisibility") && j["layerVisibility"].is_object())
        for (const auto& [name, visible] : j["layerVisibility"].items())
            if (visible.is_boolean()) editor.layerVisibility[name] = visible.get<bool>();

    if (j.contains("layerLocked") && j["layerLocked"].is_object())
        for (const auto& [name, locked] : j["layerLocked"].items())
            if (locked.is_boolean()) editor.layerLocked[name] = locked.get<bool>();

    if (j.contains("layerTint") && j["layerTint"].is_object())
        for (const auto& [name, tint] : j["layerTint"].items())
            if (tint.is_array() && tint.size() == 3) editor.layerTint[name] = rVec3f(tint);

    if (j.contains("soloLayer") && j["soloLayer"].is_string())
        editor.soloLayer = j["soloLayer"].get<std::string>();
    if (j.contains("groupFilter") && j["groupFilter"].is_string())
        editor.groupFilter = j["groupFilter"].get<std::string>();

    return editor;
}

static json serialiseSceneJson(const scene::Scene& scene) {
    json root;
    root["forge_version"] = kForgeSchemaVersion;
    root["name"]          = scene.name;
    root["author"]        = scene.author;

    // Entities
    json entities = json::object();
    for (const auto& [id, entity] : scene.entities) {
        const std::string key = std::to_string(id);
        json ej;

        std::visit([&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            ej["name"]      = e.name;
            ej["transform"] = serialiseTransform(e.transform);

            if constexpr (std::is_same_v<T, scene::BrushEntity>) {
                ej["type"]    = "brush";
                ej["classname"] = e.classname;
                ej["solid"]   = e.solid;
                ej["visible"] = e.visible;
                ej["layer"]   = e.layer;
                json props = json::object();
                for (const auto& [k, v] : e.properties)
                    props[k] = serialiseProperty(v);
                ej["properties"] = props;
                json brushes = json::array();
                for (const auto& b : e.brushes) brushes.push_back(serialiseBrush(b));
                ej["brushes"] = brushes;

            } else if constexpr (std::is_same_v<T, scene::PointEntity>) {
                ej["type"]      = "point";
                ej["classname"] = e.classname;
                json props = json::object();
                for (const auto& [k, v] : e.properties)
                    props[k] = serialiseProperty(v);
                ej["properties"] = props;

            } else {
                ej["type"]      = "mesh";
                ej["assetPath"] = e.assetPath;
            }
        }, entity);

        entities[key] = ej;
    }
    root["entities"] = entities;

    // Lighting
    root["lighting"] = {
        {"ambientColor",  jVec3f(scene.lighting.ambientColor)},
        {"sunDirection",  jVec3f(scene.lighting.sunDirection)},
        {"sunColor",      jVec3f(scene.lighting.sunColor)},
        {"sunIntensity",  scene.lighting.sunIntensity},
    };

    // Fog
    root["fog"] = {
        {"density", scene.fog.density},
        {"color",   jVec3f(scene.fog.color)},
    };

    return root;
}

static scene::Scene readSceneJson(const json& root) {
    scene::Scene scene;
    scene.name   = root.value("name", "untitled");
    scene.author = root.value("author", "");

    // Lighting
    if (root.contains("lighting")) {
        const auto& l = root["lighting"];
        if (l.contains("ambientColor"))
            scene.lighting.ambientColor = rVec3f(l["ambientColor"]);
        if (l.contains("sunDirection"))
            scene.lighting.sunDirection = rVec3f(l["sunDirection"]);
        if (l.contains("sunColor"))
            scene.lighting.sunColor = rVec3f(l["sunColor"]);
        if (l.contains("sunIntensity"))
            scene.lighting.sunIntensity = l["sunIntensity"].get<float>();
    }

    // Fog
    if (root.contains("fog")) {
        const auto& f = root["fog"];
        if (f.contains("density")) scene.fog.density = f["density"].get<float>();
        if (f.contains("color"))   scene.fog.color   = rVec3f(f["color"]);
    }

    // Entities
    if (root.contains("entities")) {
        for (const auto& [key, ej] : root["entities"].items()) {
            const std::string type = ej.value("type", "brush");

            if (type == "brush") {
                scene::BrushEntity e;
                e.name      = ej.value("name",    "brush");
                e.classname = ej.value("classname", "");
                e.solid     = ej.value("solid",   true);
                e.visible   = ej.value("visible", true);
                e.layer     = ej.value("layer",   "default");
                e.transform = readTransform(ej["transform"]);
                if (ej.contains("properties"))
                    for (const auto& [pk, pv] : ej["properties"].items())
                        e.properties[pk] = readProperty(pv);
                for (const auto& bj : ej["brushes"])
                    e.brushes.push_back(readBrush(bj));
                scene.addEntity(std::move(e));

            } else if (type == "point") {
                scene::PointEntity e;
                e.name      = ej.value("name",      "");
                e.classname = ej.value("classname", "info_null");
                e.transform = readTransform(ej["transform"]);
                if (ej.contains("properties"))
                    for (const auto& [pk, pv] : ej["properties"].items())
                        e.properties[pk] = readProperty(pv);
                scene.addEntity(std::move(e));

            } else if (type == "mesh") {
                scene::MeshEntity e;
                e.name      = ej.value("name",      "");
                e.assetPath = ej.value("assetPath", "");
                e.transform = readTransform(ej["transform"]);
                scene.addEntity(std::move(e));
            }
        }
    }

    return scene;
}

// ─── saveScene ────────────────────────────────────────────────────────────────

std::string saveScene(const scene::Scene& scene,
                       const std::filesystem::path& path) noexcept {
    gfx::MaterialLibrary empty;
    EditorMetadata editor;
    return saveProject(scene, empty, editor, path);
}

// ─── loadScene ────────────────────────────────────────────────────────────────

std::expected<scene::Scene, std::string>
loadScene(const std::filesystem::path& path) noexcept {
    const auto project = loadProject(path);
    if (!project) return std::unexpected(project.error());
    return project->scene;
}

std::string saveProject(const scene::Scene& scene,
                        const gfx::MaterialLibrary& materials,
                        const EditorMetadata& editor,
                        const std::filesystem::path& path) noexcept {
    try {
        json root = serialiseSceneJson(scene);
        root["materials"] = serialiseMaterials(materials);
        root["editor"] = serialiseEditorMetadata(editor);

        std::ofstream out(path);
        if (!out) return std::format("Cannot open '{}' for writing", path.string());
        out << root.dump(2);
        if (!out.good()) return std::format("Write error on '{}'", path.string());
        return {};

    } catch (const std::exception& ex) {
        return std::format("Serialisation error: {}", ex.what());
    }
}

std::expected<ProjectData, std::string>
loadProject(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path);
        if (!in) return std::unexpected(std::format("Cannot open '{}'", path.string()));

        const json root = json::parse(in, nullptr, /*exceptions=*/true);
        const std::string fileVersion = root.value("forge_version", std::string{"0.1.0"});

        ProjectData project;
        project.scene = readSceneJson(root);
        project.materials = readMaterials(root);
        project.editor = readEditorMetadata(root);
        if (fileVersion != kForgeSchemaVersion) {
            const int versionCompare = compareVersions(fileVersion, kForgeSchemaVersion);
            if (versionCompare < 0) {
                project.schemaNote = std::format(
                    "Loaded legacy schema version {} (current {}). Applied backward-compatible parsing.",
                    fileVersion, kForgeSchemaVersion);
            } else if (versionCompare > 0) {
                project.schemaNote = std::format(
                    "Loaded future schema version {} (current {}). Ignored unknown forward fields.",
                    fileVersion, kForgeSchemaVersion);
            } else {
                project.schemaNote = std::format(
                    "Loaded schema version {} (current {}). Applied compatibility parsing.",
                    fileVersion, kForgeSchemaVersion);
            }
        }
        return project;

    } catch (const json::exception& ex) {
        return std::unexpected(std::format("JSON parse error: {}", ex.what()));
    } catch (const std::exception& ex) {
        return std::unexpected(std::format("Load error: {}", ex.what()));
    }
}

} // namespace forge::serial
