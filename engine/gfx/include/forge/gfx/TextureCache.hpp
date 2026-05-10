#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace forge::gfx {

/// Loads PNG/JPG/TGA/BMP textures from disk using STB and caches them
/// as OpenGL texture objects.
///
/// Lifetime: create after a GL context exists.  Call evictAll() before
/// context destruction (or let the destructor do it).
///
/// Thread safety: none — call only from the render thread.
///
/// Texture look-up order for a materialId:
///   1. Direct path (if materialId contains '/')
///   2. <textureRoot>/<materialId>               (exact)
///   3. <textureRoot>/<materialId>.png           (add extension)
///   4. Fallback: 4×4 magenta/black checkerboard
class TextureCache {
public:
    TextureCache();
    ~TextureCache();

    TextureCache(const TextureCache&)            = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // ── Configuration ─────────────────────────────────────────────────────────

    /// Root directory searched when materialId is a relative path.
    void setTextureRoot(const std::filesystem::path& root) noexcept;

    [[nodiscard]] const std::filesystem::path& textureRoot() const noexcept {
        return root_;
    }

    // ── Load ──────────────────────────────────────────────────────────────────

    /// Return the GL texture ID for materialId, loading from disk if needed.
    /// Always returns a valid ID (fallback checkerboard on error).
    [[nodiscard]] uint32_t load(const std::string& materialId) noexcept;

    /// Pre-warm: load a texture and discard the result (useful in init).
    void prefetch(const std::string& materialId) noexcept { (void)load(materialId); }

    // ── Cache management ─────────────────────────────────────────────────────

    /// Delete all cached GL textures.  Must be called while GL context is active.
    void evictAll() noexcept;

    /// True if materialId is already loaded.
    [[nodiscard]] bool isCached(const std::string& materialId) const noexcept {
        return cache_.contains(materialId);
    }

    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }

    // ── Fallback texture ──────────────────────────────────────────────────────

    /// The checkerboard texture returned when an asset cannot be found.
    [[nodiscard]] uint32_t fallbackId() const noexcept { return fallbackId_; }

private:
    std::filesystem::path                    root_;
    std::unordered_map<std::string, uint32_t> cache_;
    uint32_t                                 fallbackId_ = 0;

    void   createFallback() noexcept;
    uint32_t uploadTexture(const std::filesystem::path& path) noexcept;

    [[nodiscard]] std::filesystem::path resolve(const std::string& id) const noexcept;
};

} // namespace forge::gfx
