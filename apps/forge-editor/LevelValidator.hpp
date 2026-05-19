#pragma once

#include <forge/scene.hpp>
#include <forge/gfx/Material.hpp>
#include <string>
#include <vector>
#include <chrono>

namespace forge::editor {

// ─── ValidationIssue ─────────────────────────────────────────────────────────

struct ValidationIssue {
    enum class Level { Info, Warning, Error };

    Level       level;
    scene::EntityId entityId = scene::kInvalidEntityId;
    std::string entity;    ///< Entity name / id  ("scene" for global issues)
    std::string message;

    [[nodiscard]] const char* levelIcon() const noexcept {
        switch (level) {
        case Level::Info:    return "[i]";
        case Level::Warning: return "[!]";
        case Level::Error:   return "[X]";
        }
        return "[ ]";
    }
};

// ─── ValidationReport ────────────────────────────────────────────────────────

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    std::chrono::milliseconds     validationTime{0};  ///< Time taken to validate

    [[nodiscard]] bool hasErrors()   const noexcept;
    [[nodiscard]] bool hasWarnings() const noexcept;
    [[nodiscard]] bool clean()       const noexcept { return issues.empty(); }

    [[nodiscard]] std::size_t errorCount()   const noexcept;
    [[nodiscard]] std::size_t warningCount() const noexcept;
    [[nodiscard]] std::size_t infoCount()    const noexcept;
};

// ─── Validator ────────────────────────────────────────────────────────────────

/// Run all built-in validation checks against a scene.
///
/// Checks performed:
///   Errors:
///     • BrushEntity has no brushes
///     • geo::Brush::validate() fails (degenerate geometry)
///     • Scene has no info_player_start
///     • Material ID referenced but not in library
///
///   Warnings:
///     • BrushFace has empty materialId
///     • Duplicate entity names
///     • Entity translation outside world bounds (±8192 units)
///     • Total brush count > 2048 (performance warning)
///     • BrushEntity with > 64 brushes (consider splitting)
///     • Orphaned/isolated faces detected
///     • Disjoint brush fragments (no connectivity)
///
///   Info:
///     • Total entity/brush/face statistics
///     • Validation performance metrics
[[nodiscard]] ValidationReport validateScene(
    const scene::Scene& scene,
    gfx::MaterialLibrary& materials) noexcept;

} // namespace forge::editor
