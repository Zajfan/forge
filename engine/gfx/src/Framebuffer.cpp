#include "forge/gfx/Framebuffer.hpp"

#include <GL/glew.h>
#include <iostream>

namespace forge::gfx {

Framebuffer Framebuffer::create(int w, int h) noexcept {
    if (w <= 0 || h <= 0) return {};

    Framebuffer fb;

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    // ── Color attachment — RGBA8 texture (will be bound as ImGui image) ────────
    glGenTextures(1, &fb.colorTexture);
    glBindTexture(GL_TEXTURE_2D, fb.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fb.colorTexture, 0);

    // ── Depth+stencil attachment — renderbuffer (not sampled) ─────────────────
    glGenRenderbuffers(1, &fb.depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, fb.depthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[forge::gfx] Framebuffer incomplete\n";
        fb.destroy();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return {};
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    fb.width  = w;
    fb.height = h;
    return fb;
}

void Framebuffer::resize(int w, int h) noexcept {
    if (w == width && h == height && valid()) return;
    destroy();
    *this = create(w, h);
}

void Framebuffer::bind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void Framebuffer::unbind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::destroy() noexcept {
    if (colorTexture) { glDeleteTextures(1, &colorTexture);      colorTexture = 0; }
    if (depthRbo)     { glDeleteRenderbuffers(1, &depthRbo);     depthRbo     = 0; }
    if (fbo)          { glDeleteFramebuffers(1, &fbo);            fbo          = 0; }
    width = height = 0;
}

} // namespace forge::gfx
