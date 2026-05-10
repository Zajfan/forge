#include "forge/geo/CSG.hpp"

#include <format>

namespace forge::geo {

// ─── splitBrushByPlane ───────────────────────────────────────────────────────

std::pair<std::optional<Brush>, std::optional<Brush>>
splitBrushByPlane(const Brush& brush, const Plane& cuttingPlane) noexcept {

    // Classify all brush vertices against the cutting plane.
    const auto& verts = brush.vertices();

    int frontCount = 0;
    int backCount  = 0;

    for (const auto& v : verts) {
        switch (classifyPoint(cuttingPlane, v)) {
            case Side::Front: ++frontCount; break;
            case Side::Back:  ++backCount;  break;
            case Side::On:                  break;
        }
    }

    // ── Trivial cases ────────────────────────────────────────────────────────

    // All vertices behind or on the plane → brush is entirely on the back side.
    if (frontCount == 0)
        return { std::nullopt, brush };

    // All vertices in front of or on the plane → brush is entirely on the front.
    if (backCount == 0)
        return { brush, std::nullopt };

    // ── Split ────────────────────────────────────────────────────────────────
    //
    // Splitting a brush by a plane = adding one new face to each half:
    //
    //   front brush = original faces + cuttingPlane.flipped()
    //   back  brush = original faces + cuttingPlane

    Brush frontBrush;
    frontBrush.id    = brush.id + "_front";
    frontBrush.faces = brush.faces;
    frontBrush.faces.push_back({ cuttingPlane.flipped() });

    Brush backBrush;
    backBrush.id    = brush.id + "_back";
    backBrush.faces = brush.faces;
    backBrush.faces.push_back({ cuttingPlane });

    std::optional<Brush> front;
    std::optional<Brush> back;

    if (frontBrush.validate().valid) front = std::move(frontBrush);
    if (backBrush.validate().valid)  back  = std::move(backBrush);

    return { std::move(front), std::move(back) };
}

// ─── Internal helper ─────────────────────────────────────────────────────────

static std::vector<Brush>
subtractOne(std::vector<Brush> subjects, const Brush& cutter) noexcept {

    std::vector<Brush> result;
    result.reserve(subjects.size() * 2);

    const AABB cutterBounds = cutter.bounds();

    for (const Brush& subject : subjects) {

        // Fast AABB rejection
        if (!subject.bounds().overlaps(cutterBounds)) {
            result.push_back(subject);
            continue;
        }

        // Split by each cutter face plane.
        // Front fragments escape the cutter → keep.
        // Back  fragments are still potentially inside → carry forward.
        // Whatever survives all faces is inside the cutter → discard.

        std::vector<Brush> inside = { subject };

        for (const auto& cutterFace : cutter.faces) {
            std::vector<Brush> stillInside;
            stillInside.reserve(inside.size());

            for (const Brush& piece : inside) {
                auto [front, back] = splitBrushByPlane(piece, cutterFace.plane);
                if (front) result.push_back(std::move(*front));
                if (back)  stillInside.push_back(std::move(*back));
            }

            inside = std::move(stillInside);
            if (inside.empty()) break;
        }
        // Remaining 'inside' fragments are fully carved — discarded.
    }

    return result;
}

// ─── csgSubtract ─────────────────────────────────────────────────────────────

std::vector<Brush>
csgSubtract(const Brush& subject, const Brush& cutter) noexcept {
    return subtractOne({ subject }, cutter);
}

std::vector<Brush>
csgSubtract(const Brush& subject, std::span<const Brush> cutters) noexcept {
    std::vector<Brush> fragments = { subject };
    for (const auto& cutter : cutters) {
        fragments = subtractOne(std::move(fragments), cutter);
        if (fragments.empty()) break;
    }
    return fragments;
}

// ─── hollowBrush ─────────────────────────────────────────────────────────────

std::vector<Brush>
hollowBrush(const Brush& brush, double wallThickness) noexcept {
    std::vector<Brush> walls;
    walls.reserve(brush.faces.size());

    for (std::size_t i = 0; i < brush.faces.size(); ++i) {
        // Inset plane:  normal = -N,  distance = wallThickness - D
        const Plane& outer = brush.faces[i].plane;
        const Plane  inset = { -outer.normal, wallThickness - outer.distance };

        Brush wall;
        wall.id    = std::format("{}_wall_{}", brush.id, i);
        wall.faces = brush.faces;
        wall.faces.push_back({ inset, brush.faces[i].materialId });

        if (wall.validate().valid)
            walls.push_back(std::move(wall));
    }

    return walls;
}

} // namespace forge::geo
