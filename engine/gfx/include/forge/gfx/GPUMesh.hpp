#pragma once

#include <forge/build/MeshData.hpp>
#include <string>
#include <vector>

namespace forge::gfx {

// ─── GPUMesh ─────────────────────────────────────────────────────────────────

/// A single-material mesh resident on the GPU.
/// Owns OpenGL VAO, VBO, and EBO.  Move-only.
class GPUMesh {
public:
    GPUMesh() = default;
    ~GPUMesh();

    GPUMesh(GPUMesh&&) noexcept;
    GPUMesh& operator=(GPUMesh&&) noexcept;
    GPUMesh(const GPUMesh&)            = delete;
    GPUMesh& operator=(const GPUMesh&) = delete;

    // ── Upload ────────────────────────────────────────────────────────────────

    /// Upload a MeshData to the GPU and return a GPUMesh.
    /// Must be called on a thread with an active GL context.
    [[nodiscard]] static GPUMesh upload(const build::MeshData& data) noexcept;

    // ── Draw ──────────────────────────────────────────────────────────────────

    /// Issue a glDrawElements call. Binds own VAO first.
    void draw() const noexcept;

    // ── State ─────────────────────────────────────────────────────────────────

    [[nodiscard]] bool        valid()      const noexcept { return vao_ != 0; }
    [[nodiscard]] uint32_t    indexCount() const noexcept { return indexCount_; }
    [[nodiscard]] const std::string& materialId() const noexcept { return materialId_; }
    [[nodiscard]] const geo::AABB&   bounds()     const noexcept { return bounds_; }

private:
    uint32_t    vao_        = 0;
    uint32_t    vbo_        = 0;
    uint32_t    ebo_        = 0;
    uint32_t    indexCount_ = 0;
    std::string materialId_;
    geo::AABB   bounds_;

    void release() noexcept;
};

// ─── GPUEntityMesh ───────────────────────────────────────────────────────────

/// All GPU submeshes for one scene entity.
struct GPUEntityMesh {
    std::vector<GPUMesh> submeshes;
    geo::AABB            bounds;

    [[nodiscard]] bool empty() const noexcept { return submeshes.empty(); }

    /// Upload an EntityMesh to the GPU.
    [[nodiscard]] static GPUEntityMesh upload(const build::EntityMesh& data) noexcept;
};

} // namespace forge::gfx
