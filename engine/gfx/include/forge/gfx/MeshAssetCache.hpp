#pragma once

#include "GPUMesh.hpp"
#include <forge/build/MeshData.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <optional>

namespace forge::gfx {

/// Loads mesh files (GLTF/GLB) and caches them as GPU meshes.
///
/// Used by the editor to display MeshEntity objects placed in the scene.
///
/// Thread safety: none — call only from the render thread.
/// All assets must be loaded while a GL context is active.
class MeshAssetCache {
public:
    MeshAssetCache();
    ~MeshAssetCache();

    MeshAssetCache(const MeshAssetCache&)            = delete;
    MeshAssetCache& operator=(const MeshAssetCache&) = delete;

    // ── Configuration ─────────────────────────────────────────────────────────

    /// Set the root directory searched for relative asset paths.
    void setAssetRoot(const std::filesystem::path& root) noexcept;

    // ── Load ──────────────────────────────────────────────────────────────────

    /// Return GPU mesh for the given asset path, loading from disk if needed.
    /// @returns Pointer to the cached GPUEntityMesh, or nullptr on failure.
    [[nodiscard]] const GPUEntityMesh* load(const std::string& assetPath) noexcept;

    // ── Cache management ─────────────────────────────────────────────────────

    void evictAll() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }

private:
    std::filesystem::path                         root_;
    std::unordered_map<std::string, GPUEntityMesh> cache_;

    [[nodiscard]] std::filesystem::path resolve(const std::string& path) const noexcept;

    [[nodiscard]] std::optional<build::EntityMesh>
    loadFromDisk(const std::filesystem::path& path) noexcept;

    [[nodiscard]] std::optional<build::EntityMesh>
    loadGLTF(const std::filesystem::path& path) noexcept;
};

} // namespace forge::gfx
