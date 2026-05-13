#pragma once

#include <forge/scene.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace forge::runtime { struct PhysicsWorld; }

// Forward-declare sol/Lua types
namespace sol { class state; }

namespace forge::script {

// ─── ConsoleEntry ─────────────────────────────────────────────────────────────

struct ConsoleEntry {
    enum class Kind { Input, Output, Error };
    Kind        kind;
    std::string text;
};

// ─── ScriptEnv ────────────────────────────────────────────────────────────────

/// A Lua 5.4 scripting environment with forge API bindings.
///
/// Provides:
///   - Interactive script console (type + execute Lua in the editor)
///   - Per-entity script execution in play mode (update() called each frame)
///   - forge.scene API (query/mutate entities)
///   - forge.physics API (apply_impulse, get_position, raycast)
///   - forge.log (print to console)
///   - forge.input (key state in play mode)
///   - Game event callbacks: on_start, on_collision
class ScriptEnv {
public:
    ScriptEnv();
    ~ScriptEnv();

    ScriptEnv(const ScriptEnv&)            = delete;
    ScriptEnv& operator=(const ScriptEnv&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    [[nodiscard]] bool init() noexcept;
    void               shutdown() noexcept;
    [[nodiscard]] bool valid() const noexcept { return lua_ != nullptr; }

    // ── API binding ───────────────────────────────────────────────────────────

    /// Bind the forge scene API for the current play/edit session.
    /// Must be called before exec() or updateEntity().
    void bindScene(scene::Scene* scene) noexcept;

    /// Physics callbacks — registered by GameRuntime (which owns PhysicsWorld).
    struct PhysicsCallbacks {
        /// Returns {hit, x, y, z, nx, ny, nz, t} or nullopt if no hit.
        struct RaycastResult { bool hit=false; float x,y,z,nx,ny,nz,t; };
        std::function<RaycastResult(float ox,float oy,float oz,
                                    float dx,float dy,float dz,
                                    float maxDist)>       raycast;
        std::function<void(uint32_t handle,
                           float ix,float iy,float iz)>   applyImpulse;
        std::function<std::tuple<float,float,float>
                      (uint32_t handle)>                  getBodyPosition;
    };

    /// Wire up the physics Lua API using pre-bound callbacks.
    /// Call during play mode init after GameRuntime::init().
    void bindPhysicsCallbacks(PhysicsCallbacks cbs) noexcept;

    // ── Execution ────────────────────────────────────────────────────────────────

    /// Execute a Lua code snippet.  Output captured in consoleLog().
    /// @returns true on success, false on Lua error.
    bool exec(const std::string& code) noexcept;

    /// Run the per-entity update function.
    /// Expects `code` to define a global function `update(dt)`.
    /// Called every frame for entities that have a "script" property.
    bool updateEntity(scene::EntityId id, const std::string& code, float dt) noexcept;

    /// Update forge.time fields exposed to Lua.
    void setTime(float dt, float elapsed) noexcept;

    /// Fire the on_start event for all entities with scripts.
    /// Call once when entering play mode.
    void fireOnStart() noexcept;

    /// Fire a named global event function (e.g. on_collision, on_trigger).
    /// Calls matching function in each scripted point entity, if present.
    void fireEvent(const std::string& name,
                   scene::EntityId source = scene::kInvalidEntityId) noexcept;

    // ── Script file loading ───────────────────────────────────────────────────

    [[nodiscard]] bool loadFile(const std::filesystem::path& path) noexcept;

    // ── Console log ───────────────────────────────────────────────────────────

    [[nodiscard]] const std::vector<ConsoleEntry>& consoleLog() const noexcept {
        return log_;
    }
    void clearLog() noexcept { log_.clear(); }

private:
    std::unique_ptr<sol::state>   lua_;
    scene::Scene*                 scene_   = nullptr;
    std::vector<ConsoleEntry>     log_;

    void log(ConsoleEntry::Kind kind, const std::string& text);
    void bindForgeAPI() noexcept;
    void bindPhysicsAPI(PhysicsCallbacks cbs) noexcept;
};

} // namespace forge::script
