#pragma once

#include <expected>
#include <memory>
#include <string>

// Forward-declare SDL types to avoid polluting headers with SDL3 includes
struct SDL_Window;
typedef void* SDL_GLContext;

namespace forge::gfx {

// ─── InputState ───────────────────────────────────────────────────────────────

struct MouseState {
    float x      = 0.f, y      = 0.f;  ///< current position (pixels)
    float dx     = 0.f, dy     = 0.f;  ///< delta this frame
    float scroll = 0.f;                 ///< scroll wheel delta this frame
    bool  left   = false;               ///< left button held
    bool  right  = false;               ///< right button held
    bool  middle = false;               ///< middle button held
};

struct KeyState {
    bool w = false, a = false, s = false, d = false;
    bool q = false, e = false;
    bool shift = false, ctrl = false;
    bool escape = false;
    bool f1 = false;   ///< toggle wireframe
    bool f5 = false;   ///< reload shaders
};

struct InputState {
    MouseState mouse;
    KeyState   keys;
};

// ─── WindowConfig ─────────────────────────────────────────────────────────────

struct WindowConfig {
    std::string title       = "FORGE";
    int         width       = 1280;
    int         height      = 720;
    bool        fullscreen  = false;
    int         msaaSamples = 4;
    int         glMajor     = 4;
    int         glMinor     = 6;
};

// ─── Window ───────────────────────────────────────────────────────────────────

/// SDL3 window with an OpenGL 4.6 Core context.
/// Move-only.  Destroyed when it goes out of scope.
class Window {
public:
    Window() = default;
    ~Window();

    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // ── Factory ───────────────────────────────────────────────────────────────

    /// Create the window, initialise SDL3 and GLEW.
    /// @returns Window on success, error message string on failure.
    [[nodiscard]] static std::expected<Window, std::string>
    create(const WindowConfig& config = {}) noexcept;

    // ── Frame lifecycle ───────────────────────────────────────────────────────

    /// Pump SDL events, update InputState.
    /// Call at the start of each frame.
    void pollEvents() noexcept;

    /// Swap front/back buffers.
    /// Call at the end of each frame.
    void swapBuffers() noexcept;

    // ── State ─────────────────────────────────────────────────────────────────

    [[nodiscard]] bool         shouldClose()  const noexcept;
    [[nodiscard]] int          width()        const noexcept;
    [[nodiscard]] int          height()       const noexcept;
    [[nodiscard]] float        aspectRatio()  const noexcept;
    [[nodiscard]] float        deltaTime()    const noexcept; ///< seconds since last frame
    [[nodiscard]] float        fps()          const noexcept;
    [[nodiscard]] const InputState& input()   const noexcept;

    // ── Raw handles (for ImGui init) ──────────────────────────────────────────
    [[nodiscard]] SDL_Window*  sdlWindow()    const noexcept;
    [[nodiscard]] SDL_GLContext glContext()   const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace forge::gfx
