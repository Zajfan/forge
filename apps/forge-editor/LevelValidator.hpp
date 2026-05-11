#pragma once

#include <forge/scene.hpp>
#include <string>
#include <vector>

namespace forge::editor {

// ─── ValidationIssue ─────────────────────────────────────────────────────────

struct ValidationIssue {
    enum class Level { Info, Warning, Error };

    Level       level;
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
///
///   Warnings:
///     • BrushFace has empty materialId
///     • Duplicate entity names
///     • Entity translation outside world bounds (±8192 units)
///     • Total brush count > 2048 (performance warning)
///     • BrushEntity with > 64 brushes (consider splitting)
///
///   Info:
///     • Total entity/brush/face statistics
[[nodiscard]] ValidationReport validateScene(const scene::Scene& scene) noexcept;

} // namespace forge::editor
