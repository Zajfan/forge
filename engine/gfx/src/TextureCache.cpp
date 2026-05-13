#include "forge/gfx/TextureCache.hpp"

// STB implementation lives in stb_impl.cpp — only include headers here
#include <stb_image.h>

#include <GL/glew.h>
#include <iostream>
#include <array>

namespace forge::gfx {

// ─── Helpers ──────────────────────────────────────────────────────────────────

static uint32_t uploadRGBA(const uint8_t* pixels, int w, int h) noexcept {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

    // Anisotropic filtering (OpenGL 4.6 core)
    GLfloat maxAniso = 0.f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, maxAniso);

    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

// ─── TextureCache ─────────────────────────────────────────────────────────────

TextureCache::TextureCache() {
    stbi_set_flip_vertically_on_load(true); // OpenGL Y-up convention
}

TextureCache::~TextureCache() { evictAll(); }

void TextureCache::setTextureRoot(const std::filesystem::path& root) noexcept {
    root_ = root;
}

void TextureCache::createFallback() noexcept {
    // 8×8 magenta/black checkerboard
    constexpr int S = 8;
    std::array<uint8_t, S * S * 4> px;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const bool checker = (x + y) % 2;
            const int  i       = (y * S + x) * 4;
            px[i + 0] = checker ? 255 : 20;   // R
            px[i + 1] = checker ? 0   : 20;   // G
            px[i + 2] = checker ? 255 : 20;   // B
            px[i + 3] = 255;                   // A
        }
    fallbackId_ = uploadRGBA(px.data(), S, S);
}

std::filesystem::path TextureCache::resolve(const std::string& id) const noexcept {
    // 1. Absolute / explicit path
    {
        std::filesystem::path p(id);
        if (p.is_absolute() && std::filesystem::exists(p)) return p;
    }

    if (!root_.empty()) {
        // 2. <root>/<id>
        {
            auto p = root_ / id;
            if (std::filesystem::exists(p)) return p;
        }
        // 3. <root>/<id>.png
        for (const char* ext : { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }) {
            auto p = root_ / (id + ext);
            if (std::filesystem::exists(p)) return p;
        }
    }
    return {}; // not found
}

uint32_t TextureCache::uploadTexture(const std::filesystem::path& path) noexcept {
    int w = 0, h = 0, ch = 0;
    uint8_t* data = stbi_load(path.string().c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!data) {
        std::cerr << "[TextureCache] Failed to load: " << path << '\n';
        return fallbackId_;
    }
    const uint32_t id = uploadRGBA(data, w, h);
    stbi_image_free(data);
    std::cout << "[TextureCache] Loaded " << path.filename().string()
              << " (" << w << "×" << h << ")\n";
    return id;
}

uint32_t TextureCache::load(const std::string& materialId) noexcept {
    // Create fallback lazily once a valid GL context is expected to exist.
    if (fallbackId_ == 0) createFallback();

    // Return cached entry
    if (const auto it = cache_.find(materialId); it != cache_.end())
        return it->second;

    // Resolve file path
    const auto path = resolve(materialId);
    const uint32_t id = path.empty() ? fallbackId_ : uploadTexture(path);

    cache_[materialId] = id;
    return id;
}

void TextureCache::evictAll() noexcept {
    for (auto& [k, id] : cache_) {
        GLuint gid = id;
        glDeleteTextures(1, &gid);
    }
    cache_.clear();
    if (fallbackId_) {
        GLuint gid = fallbackId_;
        glDeleteTextures(1, &gid);
        fallbackId_ = 0;
    }
}

} // namespace forge::gfx
