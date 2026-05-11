#include "forge/bsp/BSPTree.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <span>

namespace forge::bsp {

// ─── Constants ────────────────────────────────────────────────────────────────

static constexpr double kOnEpsilon   = 1.0;   // units; vertex within this = on plane
static constexpr float  kBrushEpsilon = 0.01f;

// ─── Build-time node ─────────────────────────────────────────────────────────
// Used during construction; flattened into BSPTree arrays at the end.

struct BuildNode {
    geo::Plane   splitter;
    geo::AABB    bounds;
    bool         isLeaf = false;
    bool         solid  = false;

    std::vector<BSPFace>           faces;    // leaf faces
    std::unique_ptr<BuildNode>     front;
    std::unique_ptr<BuildNode>     back;
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

enum class FaceSide { Front, Back, On, Split };

static FaceSide classifyFace(const BSPFace& face, const geo::Plane& plane,
                               int penalty) noexcept
{
    int frontCount = 0, backCount = 0;
    for (const auto& v : face.verts) {
        const double d = plane.eval(glm::dvec3(v));
        if      (d >  kOnEpsilon) ++frontCount;
        else if (d < -kOnEpsilon) ++backCount;
    }
    if (frontCount && backCount) return FaceSide::Split;
    if (frontCount)              return FaceSide::Front;
    if (backCount)               return FaceSide::Back;
    return FaceSide::On;
}

/// Score a candidate splitter plane.  Lower = better.
static int scoreSplitter(const geo::Plane& plane,
                          std::span<const BSPFace> faces,
                          int splitPenalty) noexcept
{
    int front = 0, back = 0, splits = 0;
    for (const auto& f : faces) {
        switch (classifyFace(f, plane, splitPenalty)) {
        case FaceSide::Front: ++front; break;
        case FaceSide::Back:  ++back;  break;
        case FaceSide::Split: ++splits; ++front; ++back; break;
        case FaceSide::On:    ++front; ++back; break;
        }
    }
    return std::abs(front - back) + splits * splitPenalty;
}

/// Choose the best splitter from the face list.
/// Returns index into faces, or -1 if faces is empty.
static int chooseSplitter(std::span<const BSPFace> faces,
                           int splitPenalty) noexcept
{
    if (faces.empty()) return -1;

    // Sample every Nth face to cap complexity on large face sets
    const int stride = std::max(1, (int)faces.size() / 20);
    int bestIdx   = 0;
    int bestScore = std::numeric_limits<int>::max();

    for (int i = 0; i < (int)faces.size(); i += stride) {
        const int score = scoreSplitter(faces[i].plane, faces, splitPenalty);
        if (score < bestScore) {
            bestScore = score;
            bestIdx   = i;
        }
    }
    return bestIdx;
}

/// Clip a face polygon to the front (positive) side of a plane.
static BSPFace clipToFront(const BSPFace& face, const geo::Plane& plane) {
    // clipPolygonByPlane keeps the BACK side.
    // "Front of plane" = back of flipped plane.
    const geo::Plane flipped = plane.flipped();
    std::vector<glm::dvec3> d(face.verts.size());
    for (std::size_t i = 0; i < face.verts.size(); ++i)
        d[i] = glm::dvec3(face.verts[i]);

    const auto clipped = geo::clipPolygonByPlane(d, flipped);
    BSPFace out = face;
    out.verts.clear();
    out.isSplit = true;
    for (const auto& v : clipped) out.verts.push_back(glm::vec3(v));
    return out;
}

/// Clip a face polygon to the back (negative) side of a plane.
static BSPFace clipToBack(const BSPFace& face, const geo::Plane& plane) {
    std::vector<glm::dvec3> d(face.verts.size());
    for (std::size_t i = 0; i < face.verts.size(); ++i)
        d[i] = glm::dvec3(face.verts[i]);

    const auto clipped = geo::clipPolygonByPlane(d, plane);
    BSPFace out = face;
    out.verts.clear();
    out.isSplit = true;
    for (const auto& v : clipped) out.verts.push_back(glm::vec3(v));
    return out;
}

// ─── Recursive builder ────────────────────────────────────────────────────────

static geo::AABB computeBounds(std::span<const BSPFace> faces) noexcept {
    geo::AABB b;
    for (const auto& f : faces)
        for (const auto& v : f.verts)
            b.expand(geo::AABB{ glm::dvec3(v), glm::dvec3(v) });
    return b;
}

static std::unique_ptr<BuildNode> buildRecursive(
    std::vector<BSPFace> faces,
    int depth, int maxDepth,
    int splitPenalty,
    BSPTree::Stats& stats) noexcept
{
    stats.maxDepth = std::max(stats.maxDepth, depth);

    auto node = std::make_unique<BuildNode>();
    node->bounds = computeBounds(faces);

    // ── Leaf conditions ───────────────────────────────────────────────────────
    if (faces.empty() || depth >= maxDepth) {
        node->isLeaf = true;
        node->solid  = faces.empty();
        node->faces  = std::move(faces);
        ++stats.leafCount;
        if (node->solid) ++stats.solidLeaves;
        return node;
    }

    // ── Choose splitter ───────────────────────────────────────────────────────
    const int splitterIdx = chooseSplitter(faces, splitPenalty);
    node->splitter = faces[splitterIdx].plane;

    // ── Partition ─────────────────────────────────────────────────────────────
    std::vector<BSPFace> frontFaces, backFaces;
    frontFaces.reserve(faces.size());
    backFaces.reserve(faces.size());

    for (const auto& face : faces) {
        FaceSide side = classifyFace(face, node->splitter, splitPenalty);

        switch (side) {
        case FaceSide::Front:
            frontFaces.push_back(face);
            break;
        case FaceSide::Back:
            backFaces.push_back(face);
            break;
        case FaceSide::On:
            // Place on-plane faces on the front side (arbitrary but consistent)
            frontFaces.push_back(face);
            break;
        case FaceSide::Split: {
            ++stats.splitCount;
            ++stats.outputFaces;
            auto front = clipToFront(face, node->splitter);
            auto back  = clipToBack (face, node->splitter);
            if (front.verts.size() >= 3) frontFaces.push_back(std::move(front));
            if (back.verts.size()  >= 3) backFaces.push_back (std::move(back));
            break;
        }
        }
    }

    ++stats.nodeCount;

    // ── Recurse ───────────────────────────────────────────────────────────────
    node->front = buildRecursive(std::move(frontFaces), depth + 1,
                                  maxDepth, splitPenalty, stats);
    node->back  = buildRecursive(std::move(backFaces),  depth + 1,
                                  maxDepth, splitPenalty, stats);

    return node;
}

// ─── Flatten into BSPTree arrays ─────────────────────────────────────────────

static int flattenNode(const BuildNode& bn, BSPTree& tree) {
    if (bn.isLeaf) {
        // Leaf: encode as ~leafIdx in child slot
        const int leafIdx = static_cast<int>(tree.leaves.size());
        BSPLeaf leaf;
        leaf.solid     = bn.solid;
        leaf.bounds    = bn.bounds;
        leaf.firstFace = static_cast<int>(tree.faces.size());
        leaf.faceCount = static_cast<int>(bn.faces.size());
        for (const auto& f : bn.faces) tree.faces.push_back(f);
        tree.leaves.push_back(leaf);
        return ~leafIdx;  // negative encoding: -(idx+1)
    }

    // Internal node: insert placeholder, recurse, then fill back in
    const int nodeIdx = static_cast<int>(tree.nodes.size());
    tree.nodes.emplace_back();

    const int frontChild = flattenNode(*bn.front, tree);
    const int backChild  = flattenNode(*bn.back,  tree);

    // Use index (stable even after vector realloc in recursion)
    tree.nodes[nodeIdx].splitter      = bn.splitter;
    tree.nodes[nodeIdx].bounds        = bn.bounds;
    tree.nodes[nodeIdx].children[0]   = frontChild;
    tree.nodes[nodeIdx].children[1]   = backChild;

    return nodeIdx;
}

// ─── Build ────────────────────────────────────────────────────────────────────

BSPTree BSPTree::build(const scene::Scene& scene, int maxDepth) noexcept {
    return buildBSP(scene, { .maxDepth = maxDepth });
}

BSPTree buildBSP(const scene::Scene& scene, const BSPBuildOptions& opts) noexcept {
    BSPTree tree;
    const auto t0 = std::chrono::steady_clock::now();

    // ── Collect faces from all solid BrushEntities ───────────────────────────
    std::vector<BSPFace> allFaces;
    for (const auto& [id, entity] : scene.entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&entity);
        if (!be || !be->solid) continue;

        for (const auto& brush : be->brushes) {
            const auto& polys = brush.allFacePolygons();

            for (std::size_t fi = 0; fi < brush.faces.size(); ++fi) {
                if (polys[fi].size() < 3) continue;

                BSPFace face;
                face.plane      = brush.faces[fi].plane;
                face.materialId = brush.faces[fi].materialId;
                face.verts.reserve(polys[fi].size());

                for (const auto& lp : polys[fi]) {
                    const glm::dvec3 wp = be->transform.transformPoint(lp);
                    face.verts.push_back(glm::vec3(wp));
                }

                tree.worldBounds.expand(computeBounds({ &face, 1 }));
                allFaces.push_back(std::move(face));
            }
        }
    }

    tree.stats.inputFaces  = static_cast<int>(allFaces.size());
    tree.stats.outputFaces = static_cast<int>(allFaces.size()); // will grow with splits

    if (allFaces.empty()) {
        if (opts.verbose) std::cout << "[BSP] No faces to compile.\n";
        return tree;
    }

    if (opts.verbose)
        std::cout << "[BSP] Compiling " << allFaces.size() << " faces"
                  << " (maxDepth=" << opts.maxDepth << ")...\n";

    // ── Build recursively ─────────────────────────────────────────────────────
    auto root = buildRecursive(std::move(allFaces), 0,
                                opts.maxDepth, opts.splitPenalty,
                                tree.stats);

    // ── Flatten into arrays ───────────────────────────────────────────────────
    tree.nodes.reserve(tree.stats.nodeCount);
    tree.leaves.reserve(tree.stats.leafCount);
    flattenNode(*root, tree);

    // ── Finalise stats ────────────────────────────────────────────────────────
    tree.stats.outputFaces = static_cast<int>(tree.faces.size());

    const int depth = tree.stats.maxDepth;
    const int perfectNodes = (1 << std::min(depth, 20)) - 1; // 2^depth - 1
    tree.stats.balanceRatio = perfectNodes > 0
        ? static_cast<float>(tree.stats.nodeCount) / static_cast<float>(perfectNodes)
        : 1.f;

    const auto t1 = std::chrono::steady_clock::now();
    tree.stats.buildTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    if (opts.verbose) {
        std::cout << "[BSP] Done in " << tree.stats.buildTimeMs << " ms\n"
                  << "      nodes=" << tree.stats.nodeCount
                  << " leaves=" << tree.stats.leafCount
                  << " solid=" << tree.stats.solidLeaves
                  << " splits=" << tree.stats.splitCount
                  << " depth=" << tree.stats.maxDepth
                  << " faces=" << tree.stats.outputFaces << "\n";
    }

    return tree;
}

// ─── Traversal ────────────────────────────────────────────────────────────────

int BSPTree::leafAt(glm::vec3 point) const noexcept {
    if (nodes.empty()) return -1;

    int nodeIdx = 0;
    while (true) {
        const auto& node = nodes[nodeIdx];
        const float d = static_cast<float>(node.splitter.eval(glm::dvec3(point)));
        const int child = node.children[d >= 0.f ? 0 : 1]; // front or back

        if (child >= 0) {
            nodeIdx = child;    // internal node — keep traversing
        } else {
            return ~child;      // leaf — decode and return
        }
    }
}

} // namespace forge::bsp
