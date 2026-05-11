#include "LevelValidator.hpp"

#include <algorithm>
#include <format>
#include <set>

namespace forge::editor {

// ─── ValidationReport helpers ─────────────────────────────────────────────────

bool ValidationReport::hasErrors() const noexcept {
    return std::ranges::any_of(issues,
        [](const auto& i){ return i.level == ValidationIssue::Level::Error; });
}

bool ValidationReport::hasWarnings() const noexcept {
    return std::ranges::any_of(issues,
        [](const auto& i){ return i.level == ValidationIssue::Level::Warning; });
}

std::size_t ValidationReport::errorCount() const noexcept {
    return std::ranges::count_if(issues,
        [](const auto& i){ return i.level == ValidationIssue::Level::Error; });
}

std::size_t ValidationReport::warningCount() const noexcept {
    return std::ranges::count_if(issues,
        [](const auto& i){ return i.level == ValidationIssue::Level::Warning; });
}

std::size_t ValidationReport::infoCount() const noexcept {
    return std::ranges::count_if(issues,
        [](const auto& i){ return i.level == ValidationIssue::Level::Info; });
}

// ─── Internal helpers ─────────────────────────────────────────────────────────

using Level = ValidationIssue::Level;

static void addError  (ValidationReport& r, std::string ent, std::string msg) {
    r.issues.push_back({ Level::Error,   std::move(ent), std::move(msg) });
}
static void addWarning(ValidationReport& r, std::string ent, std::string msg) {
    r.issues.push_back({ Level::Warning, std::move(ent), std::move(msg) });
}
static void addInfo   (ValidationReport& r, std::string ent, std::string msg) {
    r.issues.push_back({ Level::Info,    std::move(ent), std::move(msg) });
}

// ─── validateScene ────────────────────────────────────────────────────────────

ValidationReport validateScene(const scene::Scene& scene) noexcept {
    ValidationReport report;

    constexpr double kWorldBound     = 8192.0;
    constexpr std::size_t kBrushWarn = 2048;
    constexpr std::size_t kBrushSplitWarn = 64;

    bool      hasPlayerStart = false;
    std::set<std::string> names;
    std::size_t totalBrushes = 0;
    std::size_t totalFaces   = 0;
    std::size_t totalEntities = 0;

    for (const auto& [id, entity] : scene.entities) {
        ++totalEntities;
        const std::string& name = scene::entityName(entity);

        // ── Duplicate names (warning) ────────────────────────────────────────
        if (!name.empty()) {
            if (names.contains(name)) {
                addWarning(report, name,
                    std::format("Duplicate entity name '{}'.", name));
            } else {
                names.insert(name);
            }
        }

        // ── BrushEntity checks ───────────────────────────────────────────────
        if (const auto* be = std::get_if<scene::BrushEntity>(&entity)) {

            // World bounds check
            const geo::AABB wb = be->worldBounds();
            if (wb.isValid()) {
                const bool outX = wb.maxs.x >  kWorldBound || wb.mins.x < -kWorldBound;
                const bool outY = wb.maxs.y >  kWorldBound || wb.mins.y < -kWorldBound;
                const bool outZ = wb.maxs.z >  kWorldBound || wb.mins.z < -kWorldBound;
                if (outX || outY || outZ)
                    addWarning(report, name,
                        std::format("Entity extends outside world bounds (±{} units).",
                            (int)kWorldBound));
            }

            // Empty entity
            if (be->brushes.empty()) {
                addError(report, name, "BrushEntity has no brushes.");
                continue;
            }

            // Large entity warning
            if (be->brushes.size() > kBrushSplitWarn)
                addWarning(report, name,
                    std::format("Entity has {} brushes — consider splitting for performance.",
                        be->brushes.size()));

            // Per-brush validation
            for (const auto& brush : be->brushes) {
                ++totalBrushes;
                totalFaces += brush.faces.size();

                const auto result = brush.validate();
                if (!result.valid) {
                    for (const auto& err : result.errors)
                        addError(report, name,
                            std::format("Brush '{}': {}", brush.id, err));
                }

                // Empty material IDs
                for (const auto& face : brush.faces) {
                    if (face.materialId.empty())
                        addWarning(report, name,
                            std::format("Brush '{}' has a face with empty materialId.",
                                brush.id));
                }
            }
        }

        // ── PointEntity checks ───────────────────────────────────────────────
        if (const auto* pe = std::get_if<scene::PointEntity>(&entity)) {

            if (pe->classname == "info_player_start")
                hasPlayerStart = true;

            // Out-of-bounds check
            const glm::dvec3& t = pe->transform.translation;
            if (std::abs(t.x) > kWorldBound ||
                std::abs(t.y) > kWorldBound ||
                std::abs(t.z) > kWorldBound) {
                addWarning(report, name,
                    std::format("Point entity '{}' is outside world bounds.",
                        pe->classname));
            }
        }
    }

    // ── Global checks ────────────────────────────────────────────────────────

    if (!hasPlayerStart) {
        addError(report, "scene",
            "No 'info_player_start' entity found — player has no spawn point.");
    }

    if (totalBrushes > kBrushWarn) {
        addWarning(report, "scene",
            std::format("{} total brushes — consider reducing geometry complexity.",
                totalBrushes));
    }

    if (scene.entityCount() == 0) {
        addWarning(report, "scene", "Scene is empty.");
    }

    // ── Statistics (info) ────────────────────────────────────────────────────

    addInfo(report, "scene",
        std::format("{} entities, {} brushes, {} faces.",
            totalEntities, totalBrushes, totalFaces));

    const auto brushStats = scene.stats();
    if (brushStats.pointEntityCount > 0)
        addInfo(report, "scene",
            std::format("{} point entities, {} brush entities, {} mesh entities.",
                brushStats.pointEntityCount,
                brushStats.brushEntityCount,
                brushStats.meshEntityCount));

    return report;
}

} // namespace forge::editor
