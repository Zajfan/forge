#pragma once

#include <cstdint>

namespace forge::gfx {

/// An OpenGL framebuffer object for off-screen rendering.
///
/// Used by the editor viewport — scene is rendered here, then displayed
/// as an ImGui::Image. This decouples scene resolution from window size
/// and allows the viewport to live inside a dockable ImGui panel.
///
/// Attachments:
///   Color → GL_TEXTURE_2D (RGBA8)  — displayed as ImGui image
///   Depth → GL_RENDERBUFFER (DEPTH24_STENCIL8)
struct Framebuffer {
    uint32_t fbo          = 0;
    uint32_t colorTexture = 0;   ///< Bind as ImGui ImageID: (ImTextureID)(intptr_t)colorTexture
    uint32_t depthRbo     = 0;
    int      width        = 0;
    int      height       = 0;

    // ── Factory ───────────────────────────────────────────────────────────────

    /// Create a framebuffer at the given size.
    /// Returns an invalid (fbo==0) Framebuffer on GL error.
    [[nodiscard]] static Framebuffer create(int w, int h) noexcept;

    // ── Usage ─────────────────────────────────────────────────────────────────

    /// Resize: destroy and recreate at new dimensions if size changed.
    void resize(int w, int h) noexcept;

    /// Bind for rendering. Also sets glViewport(0,0,width,height).
    void bind()   const noexcept;

    /// Restore default framebuffer.
    void unbind() const noexcept;

    /// Free GL resources.
    void destroy() noexcept;

    [[nodiscard]] bool valid() const noexcept { return fbo != 0; }
};

} // namespace forge::gfx
