#include "forge/gfx/Window.hpp"

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <SDL3/SDL_opengl.h>

#include <chrono>
#include <functional>
#include <format>
#include <iostream>

namespace forge::gfx {

// ─── Impl ─────────────────────────────────────────────────────────────────────

struct Window::Impl {
    SDL_Window*   sdlWindow  = nullptr;
    SDL_GLContext glContext  = nullptr;

    int   width  = 0;
    int   height = 0;
    bool  shouldClose = false;

    InputState input;
    std::function<void(const SDL_Event&)> eventCallback;

    // Timing
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastTime = Clock::now();
    float deltaTime = 0.016f;
    float fps       = 60.f;
    int   frameCount = 0;
    float fpsTimer   = 0.f;
};

void Window::ImplDeleter::operator()(Impl* p) const noexcept {
    delete p;
}

// ─── Factory ─────────────────────────────────────────────────────────────────

std::expected<Window, std::string>
Window::create(const WindowConfig& cfg) noexcept {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return std::unexpected(std::format("SDL_Init failed: {}", SDL_GetError()));
    }

    auto setGLAttributes = [](int major, int minor, int msaaSamples) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8);
        if (msaaSamples > 1) {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaaSamples);
        } else {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        }
    };

    // Try requested profile first; then relax requirements for compatibility.
    setGLAttributes(cfg.glMajor, cfg.glMinor, cfg.msaaSamples);

    // Create window
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    SDL_Window* sdlWin = SDL_CreateWindow(
        cfg.title.c_str(), cfg.width, cfg.height, flags);
    if (!sdlWin) {
        return std::unexpected(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    }

    auto tryCreateContext = [&]() -> SDL_GLContext {
        SDL_GLContext c = SDL_GL_CreateContext(sdlWin);
        if (c) {
            SDL_GL_MakeCurrent(sdlWin, c);
            SDL_GL_SetSwapInterval(1); // vsync
        }
        return c;
    };

    SDL_GLContext ctx = tryCreateContext();
    if (!ctx) {
        setGLAttributes(3, 3, 0);
        ctx = tryCreateContext();
    }
    if (!ctx) {
        setGLAttributes(3, 0, 0);
        ctx = tryCreateContext();
    }
    if (!ctx) {
        SDL_DestroyWindow(sdlWin);
        return std::unexpected(std::format("SDL_GL_CreateContext failed: {}", SDL_GetError()));
    }

    // Initialise GLEW
    glewExperimental = GL_TRUE;
    if (const GLenum err = glewInit(); err != GLEW_OK) {
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
        // Wayland/EGL paths can report this GLX-specific status even with a valid GL context.
        if (err != GLEW_ERROR_NO_GLX_DISPLAY)
#endif
        {
            SDL_GL_DestroyContext(ctx);
            SDL_DestroyWindow(sdlWin);
            return std::unexpected(std::format("glewInit failed: {}",
                reinterpret_cast<const char*>(glewGetErrorString(err))));
        }
    }

    // Initial GL state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    if (cfg.msaaSamples > 1) glEnable(GL_MULTISAMPLE);

    std::cout << std::format("[forge::gfx] OpenGL {}\n",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Window w;
    w.impl_ = std::unique_ptr<Impl, Window::ImplDeleter>(new Impl{});
    w.impl_->sdlWindow = sdlWin;
    w.impl_->glContext = ctx;
    w.impl_->width     = cfg.width;
    w.impl_->height    = cfg.height;
    return w;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

Window::~Window() {
    if (impl_) {
        if (impl_->glContext)  SDL_GL_DestroyContext(impl_->glContext);
        if (impl_->sdlWindow)  SDL_DestroyWindow(impl_->sdlWindow);
        SDL_Quit();
    }
}

Window::Window(Window&& o) noexcept : impl_(std::move(o.impl_)) {}
Window& Window::operator=(Window&& o) noexcept {
    if (this != &o) impl_ = std::move(o.impl_);
    return *this;
}

// ─── Frame lifecycle ──────────────────────────────────────────────────────────

void Window::pollEvents() noexcept {
    auto& s = *impl_;

    // Delta time
    const auto now     = Impl::Clock::now();
    s.deltaTime        = std::chrono::duration<float>(now - s.lastTime).count();
    s.lastTime         = now;
    s.deltaTime        = std::min(s.deltaTime, 0.1f); // cap at 100ms

    // FPS counter (update every second)
    s.fpsTimer += s.deltaTime;
    ++s.frameCount;
    if (s.fpsTimer >= 1.f) {
        s.fps        = static_cast<float>(s.frameCount) / s.fpsTimer;
        s.frameCount = 0;
        s.fpsTimer   = 0.f;
    }

    // Reset per-frame deltas
    s.input.mouse.dx     = 0.f;
    s.input.mouse.dy     = 0.f;
    s.input.mouse.scroll = 0.f;
    s.input.keys.escape  = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (s.eventCallback) s.eventCallback(event);

        switch (event.type) {
        case SDL_EVENT_QUIT:
            s.shouldClose = true;
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            SDL_GetWindowSize(s.sdlWindow, &s.width, &s.height);
            glViewport(0, 0, s.width, s.height);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            s.input.mouse.x  = event.motion.x;
            s.input.mouse.y  = event.motion.y;
            s.input.mouse.dx += event.motion.xrel;
            s.input.mouse.dy += event.motion.yrel;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            if (event.button.button == SDL_BUTTON_LEFT)   s.input.mouse.left   = down;
            if (event.button.button == SDL_BUTTON_RIGHT)  s.input.mouse.right  = down;
            if (event.button.button == SDL_BUTTON_MIDDLE) s.input.mouse.middle = down;
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
            s.input.mouse.scroll += event.wheel.y;
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const bool down = (event.type == SDL_EVENT_KEY_DOWN);
            switch (event.key.key) {
            case SDLK_W:      s.input.keys.w      = down; break;
            case SDLK_A:      s.input.keys.a      = down; break;
            case SDLK_S:      s.input.keys.s      = down; break;
            case SDLK_D:      s.input.keys.d      = down; break;
            case SDLK_Q:      s.input.keys.q      = down; break;
            case SDLK_E:      s.input.keys.e      = down; break;
            case SDLK_R:      s.input.keys.r      = down; break;
            case SDLK_T:      s.input.keys.t      = down; break;
            case SDLK_Y:      s.input.keys.y      = down; break;
            case SDLK_C:      s.input.keys.c      = down; break;
            case SDLK_V:      s.input.keys.v      = down; break;
            case SDLK_P:      s.input.keys.p      = down; break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT: s.input.keys.shift  = down; break;
            case SDLK_LCTRL:
            case SDLK_RCTRL:  s.input.keys.ctrl   = down; break;
            case SDLK_ESCAPE:
                s.input.keys.escape = down;
                if (down) s.shouldClose = true;
                break;
            case SDLK_F1:     s.input.keys.f1      = down; break;
            case SDLK_F5:     s.input.keys.f5      = down; break;
            default: break;
            }
            break;
        }

        default: break;
        }
    }
}

void Window::setEventCallback(std::function<void(const SDL_Event&)> callback) noexcept {
    impl_->eventCallback = std::move(callback);
}

void Window::swapBuffers() noexcept {
    SDL_GL_SwapWindow(impl_->sdlWindow);
}

// ─── State accessors ─────────────────────────────────────────────────────────

bool          Window::shouldClose()  const noexcept { return impl_->shouldClose; }
int           Window::width()        const noexcept { return impl_->width; }
int           Window::height()       const noexcept { return impl_->height; }
float         Window::aspectRatio()  const noexcept { return static_cast<float>(impl_->width) / static_cast<float>(impl_->height); }
float         Window::deltaTime()    const noexcept { return impl_->deltaTime; }
float         Window::fps()          const noexcept { return impl_->fps; }
const InputState& Window::input()    const noexcept { return impl_->input; }
SDL_Window*   Window::sdlWindow()    const noexcept { return impl_->sdlWindow; }
SDL_GLContext  Window::glContext()   const noexcept { return impl_->glContext; }

} // namespace forge::gfx
