#include "LevelValidator.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <chrono>

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

static void addError(ValidationReport& r, scene::EntityId id, std::string ent, std::string msg) {
    r.issues.push_back({ Level::Error, id, std::move(ent), std::move(msg) });
}
static void addWarning(ValidationReport& r, scene::EntityId id, std::string ent, std::string msg) {
    r.issues.push_back({ Level::Warning, id, std::move(ent), std::move(msg) });
}
static void addInfo(ValidationReport& r, scene::EntityId id, std::string ent, std::string msg) {
    r.issues.push_back({ Level::Info, id, std::move(ent), std::move(msg) });
}

// ─── validateScene ────────────────────────────────────────────────────────────

ValidationReport validateScene(const scene::Scene& scene,
                                 gfx::MaterialLibrary& materials) noexcept {
    const auto start = std::chrono::high_resolution_clock::now();
    ValidationReport report;

    constexpr double kWorldBound     = 8192.0;
    constexpr std::size_t kBrushWarn = 2048;
    constexpr std::size_t kBrushSplitWarn = 64;

    bool      hasPlayerStart = false;
    std::set<std::string> names;
    std::size_t totalBrushes = 0;
    std::size_t totalFaces   = 0;
    std::size_t totalEntities = 0;
    std::size_t orphanedFaceCount = 0;
    std::size_t disjointBrushCount = 0;

    for (const auto& [id, entity] : scene.entities) {
        ++totalEntities;
        const std::string& name = scene::entityName(entity);

        // ── Duplicate names (warning) ────────────────────────────────────────
        if (!name.empty()) {
            if (names.contains(name)) {
                addWarning(report, id, name,
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
                    addWarning(report, id, name,
                        std::format("Entity extends outside world bounds (±{} units).",
                            (int)kWorldBound));
            }

            // Empty entity
            if (be->brushes.empty()) {
                addError(report, id, name, "BrushEntity has no brushes.");
                continue;
            }

            // Large entity warning
            if (be->brushes.size() > kBrushSplitWarn)
                addWarning(report, id, name,
                    std::format("Entity has {} brushes — consider splitting for performance.",
                        be->brushes.size()));

            // Per-brush validation
            for (const auto& brush : be->brushes) {
                ++totalBrushes;
                totalFaces += brush.faces.size();

                const auto result = brush.validate();
                if (!result.valid) {
                    for (const auto& err : result.errors)
                        addError(report, id, name,
                            std::format("Brush '{}': {}", brush.id, err));
                }

                // ── Material existence checks ────────────────────────────────
                std::set<std::string> brushMaterials;
                for (const auto& face : brush.faces) {
                    if (face.materialId.empty()) {
                        addWarning(report, id, name,
                            std::format("Brush '{}' has a face with empty materialId.",
                                brush.id));
                    } else {
                        brushMaterials.insert(face.materialId);
                    }
                }

                // Check if all materials exist in library
                for (const auto& matId : brushMaterials) {
                    if (!materials.getMaterial(matId)) {
                        addError(report, id, name,
                            std::format("Brush '{}' references missing material '{}'.",
                                brush.id, matId));
                    }
                }

                // ── Orphaned faces detection ─────────────────────────────────
                // Check for faces with degenerate polygons
                for (std::size_t faceIdx = 0; faceIdx < brush.faces.size(); ++faceIdx) {
                    const auto& poly = brush.facePolygon(faceIdx);
                    if (poly.size() < 3) {
                        ++orphanedFaceCount;
                        addWarning(report, id, name,
                            std::format("Brush '{}' face {} is degenerate (< 3 vertices).",
                                brush.id, faceIdx));
                    }
                }

                // ── Connectivity detection ───────────────────────────────────
                // Simple check: brush with no shared vertices with others in entity
                if (be->brushes.size() > 1) {
                    const auto& brushVerts = brush.vertices();
                    bool connected = false;

                    for (const auto& other : be->brushes) {
                        if (other.id == brush.id) continue;
                        const auto& otherVerts = other.vertices();

                        // Check for shared vertices
                        for (const auto& v : brushVerts) {
                            for (const auto& ov : otherVerts) {
                                if (glm::distance(v, ov) < 0.01) {
                                    connected = true;
                                    break;
                                }
                            }
                            if (connected) break;
                        }
                        if (connected) break;
                    }

                    if (!connected) {
                        ++disjointBrushCount;
                        addWarning(report, id, name,
                            std::format("Brush '{}' is disconnected from other brushes in entity.",
                                brush.id));
                    }
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
                addWarning(report, id, name,
                    std::format("Point entity '{}' is outside world bounds.",
                        pe->classname));
            }
        }
    }

    // ── Global checks ────────────────────────────────────────────────────────

    if (!hasPlayerStart) {
        addError(report, scene::kInvalidEntityId, "scene",
            "No 'info_player_start' entity found — player has no spawn point.");
    }

    if (totalBrushes > kBrushWarn) {
        addWarning(report, scene::kInvalidEntityId, "scene",
            std::format("{} total brushes — consider reducing geometry complexity.",
                totalBrushes));
    }

    if (scene.entityCount() == 0) {
        addWarning(report, scene::kInvalidEntityId, "scene", "Scene is empty.");
    }

    // ── Statistics (info) ────────────────────────────────────────────────────

    addInfo(report, scene::kInvalidEntityId, "scene",
        std::format("{} entities, {} brushes, {} faces.",
            totalEntities, totalBrushes, totalFaces));

    if (orphanedFaceCount > 0 || disjointBrushCount > 0) {
        addInfo(report, scene::kInvalidEntityId, "scene",
            std::format("{} orphaned faces, {} disjoint brushes detected.",
                orphanedFaceCount, disjointBrushCount));
    }

    const auto brushStats = scene.stats();
    if (brushStats.pointEntityCount > 0)
        addInfo(report, scene::kInvalidEntityId, "scene",
            std::format("{} point entities, {} brush entities, {} mesh entities.",
                brushStats.pointEntityCount,
                brushStats.brushEntityCount,
                brushStats.meshEntityCount));

    // ── Performance metrics ──────────────────────────────────────────────────
    const auto end = std::chrono::high_resolution_clock::now();
    report.validationTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    addInfo(report, scene::kInvalidEntityId, "scene",
        std::format("Validation completed in {}ms.", report.validationTime.count()));

    return report;
}

} // namespace forge::editor
