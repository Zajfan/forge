#pragma once

#include <forge/scene.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace forge::bsp {

// ─── BSPFace ─────────────────────────────────────────────────────────────────

/// A convex polygon stored in the BSP tree.
/// Produced either from original brush face polygons or as a split fragment.
struct BSPFace {
    std::vector<glm::vec3> verts;      ///< CCW winding viewed from front of plane
    geo::Plane             plane;      ///< Face plane (outward normal)
    std::string            materialId;
    bool                   isSplit = false; ///< Was this face produced by a BSP split?
};

// ─── BSPNode ─────────────────────────────────────────────────────────────────

/// An internal BSP node (has a splitting plane and two children).
struct BSPNode {
    geo::Plane splitter;
    int        children[2] = { -1, -1 }; ///< [front, back] — see child encoding below
    geo::AABB  bounds;

    // Child encoding:
    //   >= 0  →  nodes[child]   (internal node)
    //   <  0  →  leaves[~child] (leaf; ~child = -(child+1))
};

// ─── BSPLeaf ─────────────────────────────────────────────────────────────────

/// A BSP leaf — convex region of space bounded by the ancestor split planes.
struct BSPLeaf {
    bool solid     = false; ///< True = void/solid space (outside the playable world)
    int  firstFace = -1;    ///< Index into BSPTree::faces
    int  faceCount = 0;
    geo::AABB bounds;
};

// ─── BSPTree ─────────────────────────────────────────────────────────────────

struct BSPTree {
    std::vector<BSPNode> nodes;     ///< nodes[0] = root
    std::vector<BSPLeaf> leaves;
    std::vector<BSPFace> faces;     ///< All face polygons (owned by leaves)
    geo::AABB            worldBounds;

    // ── Build statistics ──────────────────────────────────────────────────────
    struct Stats {
        int   nodeCount      = 0;
        int   leafCount      = 0;
        int   solidLeaves    = 0;
        int   maxDepth       = 0;
        int   splitCount     = 0;   ///< Polygons split during construction
        int   inputFaces     = 0;
        int   outputFaces    = 0;
        float balanceRatio   = 0.f; ///< 1.0 = perfect balance; >1 = skewed
        float buildTimeMs    = 0.f;
    };
    Stats stats;

    // ── Construction ──────────────────────────────────────────────────────────

    /// Build a BSP tree from all solid BrushEntities in a scene.
    ///
    /// @param scene     Source scene (only solid BrushEntities are processed).
    /// @param maxDepth  Maximum tree depth.  Deeper = finer spatial subdivision
    ///                  at the cost of more splits and a larger tree.
    ///                  Typical values: 16–24.
    [[nodiscard]] static BSPTree build(const scene::Scene& scene,
                                       int maxDepth = 20) noexcept;

    // ── Traversal ─────────────────────────────────────────────────────────────

    /// Return the leaf index that contains `point`.
    /// Returns -1 if the tree is empty.
    [[nodiscard]] int leafAt(glm::vec3 point) const noexcept;

    /// True if `point` is in solid space (inside a wall / outside the world).
    [[nodiscard]] bool isSolid(glm::vec3 point) const noexcept {
        const int idx = leafAt(point);
        return idx >= 0 && leaves[idx].solid;
    }

    // ── Info ──────────────────────────────────────────────────────────────────

    [[nodiscard]] bool        empty()      const noexcept { return nodes.empty(); }
    [[nodiscard]] std::size_t nodeCount()  const noexcept { return nodes.size(); }
    [[nodiscard]] std::size_t leafCount()  const noexcept { return leaves.size(); }
    [[nodiscard]] std::size_t faceCount()  const noexcept { return faces.size(); }
};

// ─── Build options ────────────────────────────────────────────────────────────

struct BSPBuildOptions {
    int   maxDepth    = 20;
    int   splitPenalty = 8;    ///< k in: score = |front-back| + k * splits
    int   maxFacesPerLeaf = 0; ///< 0 = no limit (split until depth or empty)
    bool  verbose     = false;
};

[[nodiscard]] BSPTree buildBSP(const scene::Scene& scene,
                                const BSPBuildOptions& opts = {}) noexcept;

} // namespace forge::bsp
