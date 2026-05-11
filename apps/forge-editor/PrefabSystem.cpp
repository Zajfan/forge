#include "PrefabSystem.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <format>

using json = nlohmann::json;

namespace forge::editor {

// ─── Serialise helpers (same conventions as SceneSerializer) ──────────────────

static json jV3(glm::dvec3 v) { return { v.x, v.y, v.z }; }
static json jV2f(glm::vec2 v) { return { v.x, v.y }; }

static glm::dvec3 rV3(const json& j) {
    return { j[0].get<double>(), j[1].get<double>(), j[2].get<double>() };
}
static glm::vec2 rV2f(const json& j) {
    return { j[0].get<float>(), j[1].get<float>() };
}

static json serialisePlane(const geo::Plane& p) {
    return { {"normal", jV3(p.normal)}, {"distance", p.distance} };
}

static geo::Plane readPlane(const json& j) {
    geo::Plane p;
    const auto& n = j["normal"];
    p.normal   = { n[0].get<double>(), n[1].get<double>(), n[2].get<double>() };
    p.distance = j["distance"].get<double>();
    return p;
}

static json serialiseFace(const geo::BrushFace& f) {
    return {
        {"plane",      serialisePlane(f.plane)},
        {"materialId", f.materialId},
        {"uvOffset",   jV2f(f.uvOffset)},
        {"uvScale",    jV2f(f.uvScale)},
        {"uvRotation", f.uvRotation},
    };
}

static geo::BrushFace readFace(const json& j) {
    geo::BrushFace f;
    f.plane      = readPlane(j["plane"]);
    f.materialId = j.value("materialId", "default");
    f.uvOffset   = rV2f(j["uvOffset"]);
    f.uvScale    = rV2f(j["uvScale"]);
    f.uvRotation = j["uvRotation"].get<float>();
    return f;
}

static json serialiseBrush(const geo::Brush& b) {
    json faces = json::array();
    for (const auto& f : b.faces) faces.push_back(serialiseFace(f));
    return { {"id", b.id}, {"faces", faces} };
}

static geo::Brush readBrush(const json& j) {
    geo::Brush b;
    b.id = j.value("id", "brush");
    for (const auto& fj : j["faces"]) b.faces.push_back(readFace(fj));
    return b;
}

// ─── savePrefab ───────────────────────────────────────────────────────────────

std::string savePrefab(
    const Prefab&                prefab,
    const std::filesystem::path& path) noexcept
{
    try {
        json root;
        root["forge_prefab_version"] = "0.1.0";
        root["name"]        = prefab.name;
        root["description"] = prefab.description;
        root["author"]      = prefab.author;
        root["version"]     = prefab.version;

        json brushes = json::array();
        for (const auto& b : prefab.brushes)
            brushes.push_back(serialiseBrush(b));
        root["brushes"] = brushes;

        std::filesystem::path outPath = path;
        if (outPath.extension() != ".fprefab")
            outPath.replace_extension(".fprefab");

        std::ofstream out(outPath);
        if (!out) return std::format("Cannot write '{}'", outPath.string());
        out << root.dump(2);
        return {};

    } catch (const std::exception& ex) {
        return std::format("Prefab save error: {}", ex.what());
    }
}

// ─── loadPrefab ───────────────────────────────────────────────────────────────

std::expected<Prefab, std::string>
loadPrefab(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path);
        if (!in) return std::unexpected(std::format("Cannot open '{}'", path.string()));

        const json root = json::parse(in, nullptr, true);

        Prefab p;
        p.name        = root.value("name",        path.stem().string());
        p.description = root.value("description", "");
        p.author      = root.value("author",      "");
        p.version     = root.value("version",     "0.1.0");

        for (const auto& bj : root["brushes"])
            p.brushes.push_back(readBrush(bj));

        if (p.brushes.empty())
            return std::unexpected("Prefab contains no brushes");

        return p;

    } catch (const json::exception& ex) {
        return std::unexpected(std::format("Prefab JSON error: {}", ex.what()));
    } catch (const std::exception& ex) {
        return std::unexpected(std::format("Prefab load error: {}", ex.what()));
    }
}

// ─── instantiatePrefab ────────────────────────────────────────────────────────

scene::BrushEntity instantiatePrefab(
    const Prefab& prefab,
    glm::dvec3    worldPosition) noexcept
{
    scene::BrushEntity e;
    e.name      = prefab.name;
    e.brushes   = prefab.brushes;
    e.transform = scene::Transform::fromTranslation(worldPosition);
    for (auto& b : e.brushes) b.invalidate();
    return e;
}

} // namespace forge::editor
