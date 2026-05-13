#include "forge/script/ScriptEnv.hpp"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <format>
#include <iostream>

namespace forge::script {

static std::string luaValueToString(const sol::object& value) {
    switch (value.get_type()) {
    case sol::type::string:
        return value.as<std::string>();
    case sol::type::number:
        return std::format("{}", value.as<double>());
    case sol::type::boolean:
        return value.as<bool>() ? "true" : "false";
    case sol::type::nil:
    case sol::type::none:
        return "nil";
    default:
        return "<object>";
    }
}

ScriptEnv::ScriptEnv()  = default;
ScriptEnv::~ScriptEnv() { shutdown(); }

// ─── Lifecycle ────────────────────────────────────────────────────────────────

bool ScriptEnv::init() noexcept {
    lua_ = std::make_unique<sol::state>();
    lua_->open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::table,
        sol::lib::string,
        sol::lib::io
    );

    bindForgeAPI();
    log(ConsoleEntry::Kind::Output, "Lua 5.4 scripting engine ready.");
    return true;
}

void ScriptEnv::shutdown() noexcept {
    lua_.reset();
    scene_ = nullptr;
}

void ScriptEnv::log(ConsoleEntry::Kind kind, const std::string& text) {
    log_.push_back({ kind, text });
    if (log_.size() > 512) log_.erase(log_.begin()); // circular buffer
}

// ─── forge API ────────────────────────────────────────────────────────────────

void ScriptEnv::bindForgeAPI() noexcept {
    if (!lua_) return;
    auto& L = *lua_;

    // ── forge.log ────────────────────────────────────────────────────────────
    auto forge = L.create_named_table("forge");
    forge.set_function("log", [this](const std::string& msg) {
        log(ConsoleEntry::Kind::Output, msg);
    });

    // ── print → forge.log ────────────────────────────────────────────────────
    L.set_function("print", [this](sol::variadic_args va) {
        std::string line;
        for (auto v : va) {
            if (!line.empty()) line += "\t";
            line += luaValueToString(v.get<sol::object>());
        }
        log(ConsoleEntry::Kind::Output, line);
    });

    // ── forge.vec3 ───────────────────────────────────────────────────────────
    L.new_usertype<glm::vec3>("Vec3",
        sol::constructors<glm::vec3(), glm::vec3(float,float,float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,
        "__tostring", [](const glm::vec3& v) {
            return std::format("Vec3({:.2f}, {:.2f}, {:.2f})", v.x, v.y, v.z);
        },
        "__add", [](glm::vec3 a, glm::vec3 b) { return a + b; },
        "__sub", [](glm::vec3 a, glm::vec3 b) { return a - b; },
        "__mul", sol::overload(
            [](glm::vec3 v, float s)  { return v * s; },
            [](float s,  glm::vec3 v) { return s * v; })
    );

    forge.set_function("vec3", [](float x, float y, float z) {
        return glm::vec3{x, y, z};
    });

    // ── forge.scene ──────────────────────────────────────────────────────────
    // Lazily queries scene_ so binding can be done before scene is assigned
    auto scene_tbl = forge.create_named("scene");

    scene_tbl.set_function("entity_count", [this]() -> int {
        return scene_ ? (int)scene_->entityCount() : 0;
    });

    scene_tbl.set_function("get_position", [this](const std::string& name) -> glm::vec3 {
        if (!scene_) return {};
        for (const auto& [id, ent] : scene_->entities)
            if (scene::entityName(ent) == name)
                return glm::vec3(scene::entityTransform(ent).translation);
        return {};
    });

    scene_tbl.set_function("set_position", [this](const std::string& name, float x, float y, float z) {
        if (!scene_) return;
        for (auto& [id, ent] : scene_->entities)
            if (scene::entityName(ent) == name)
                std::visit([&](auto& e){ e.transform.translation = glm::dvec3(x,y,z); }, ent);
    });

    scene_tbl.set_function("find_by_class", [this](const std::string& cls) -> std::string {
        if (!scene_) return "";
        auto [id, pe] = scene_->findByClassname(cls);
        return pe ? pe->name : "";
    });

    scene_tbl.set_function("list_entities", [this]() -> std::vector<std::string> {
        std::vector<std::string> names;
        if (!scene_) return names;
        for (const auto& [id, ent] : scene_->entities)
            names.push_back(scene::entityName(ent));
        return names;
    });

    scene_tbl.set_function("get_property", [this](const std::string& entityName,
                                                    const std::string& propKey) -> std::string {
        if (!scene_) return "";
        for (const auto& [id, ent] : scene_->entities) {
            if (scene::entityName(ent) != entityName) continue;
            if (const auto* pe = std::get_if<scene::PointEntity>(&ent)) {
                const auto it = pe->properties.find(propKey);
                if (it != pe->properties.end()) {
                    return std::visit([](const auto& v) -> std::string {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T,std::string>) return v;
                        else if constexpr (std::is_same_v<T,float>)  return std::to_string(v);
                        else if constexpr (std::is_same_v<T,int>)    return std::to_string(v);
                        else if constexpr (std::is_same_v<T,bool>)   return v ? "true":"false";
                        else return "";
                    }, it->second);
                }
            }
        }
        return "";
    });

    // ── forge.time ───────────────────────────────────────────────────────────
    auto time_tbl = forge.create_named("time");
    time_tbl["dt"]      = 0.016f; // updated each frame before calling update()
    time_tbl["elapsed"] = 0.f;

    // ── Utility ──────────────────────────────────────────────────────────────
    forge.set_function("clamp", [](float v, float lo, float hi) {
        return std::clamp(v, lo, hi);
    });
    forge.set_function("lerp", [](float a, float b, float t) {
        return a + (b - a) * t;
    });
    forge.set_function("distance", [](glm::vec3 a, glm::vec3 b) {
        return glm::length(b - a);
    });
}

void ScriptEnv::bindScene(scene::Scene* scene) noexcept {
    scene_ = scene;
}

void ScriptEnv::bindPhysicsCallbacks(PhysicsCallbacks cbs) noexcept {
    bindPhysicsAPI(std::move(cbs));
}

// ─── Physics API ──────────────────────────────────────────────────────────────

void ScriptEnv::bindPhysicsAPI(PhysicsCallbacks cbs) noexcept {
    if (!lua_) return;
    auto& L = *lua_;

    auto forge    = L["forge"].get_or_create<sol::table>();
    auto phys_tbl = forge.create_named("physics");

    phys_tbl.set_function("raycast",
        [cb = cbs.raycast](float ox, float oy, float oz,
                            float dx, float dy, float dz,
                            float maxDist, sol::this_state s) -> sol::table {
            sol::state_view L2(s);
            sol::table result = L2.create_table();
            if (!cb) { result["hit"] = false; return result; }
            const auto hit = cb(ox, oy, oz, dx, dy, dz, maxDist);
            result["hit"] = hit.hit;
            if (hit.hit) {
                result["x"] = hit.x; result["y"] = hit.y; result["z"] = hit.z;
                result["nx"] = hit.nx; result["ny"] = hit.ny; result["nz"] = hit.nz;
                result["t"] = hit.t;
            }
            return result;
        });

    phys_tbl.set_function("apply_impulse",
        [cb = cbs.applyImpulse](uint32_t handle, float ix, float iy, float iz) {
            if (cb) cb(handle, ix, iy, iz);
        });

    phys_tbl.set_function("get_body_position",
        [cb = cbs.getBodyPosition](uint32_t handle, sol::this_state s) -> sol::table {
            sol::state_view L2(s);
            sol::table result = L2.create_table();
            if (!cb) return result;
            const auto [x, y, z] = cb(handle);
            result["x"] = x; result["y"] = y; result["z"] = z;
            return result;
        });
}

// ─── Event hooks ─────────────────────────────────────────────────────────────

void ScriptEnv::fireOnStart() noexcept {
    if (!lua_ || !scene_) return;
    for (const auto& [id, ent] : scene_->entities) {
        if (const auto* pe = std::get_if<scene::PointEntity>(&ent)) {
            const auto it = pe->properties.find("script");
            if (it == pe->properties.end()) continue;
            const auto* code = std::get_if<std::string>(&it->second);
            if (!code || code->empty()) continue;

            // Load the script
            auto load = lua_->safe_script(*code, sol::script_pass_on_error);
            if (!load.valid()) continue;

            // Call on_start() if defined
            const sol::optional<sol::function> fn = (*lua_)["on_start"];
            if (fn) {
                auto call = fn.value()();
                if (!call.valid()) {
                    const sol::error err = call;
                    log(ConsoleEntry::Kind::Error,
                        std::format("[on_start entity {}] {}", id, err.what()));
                }
            }
        }
    }
}

// ─── Execution ────────────────────────────────────────────────────────────────

bool ScriptEnv::exec(const std::string& code) noexcept {
    if (!lua_) return false;
    log(ConsoleEntry::Kind::Input, "> " + code);

    auto result = lua_->safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        const sol::error err = result;
        log(ConsoleEntry::Kind::Error, std::string("[error] ") + err.what());
        return false;
    }

    // If result is a value, log it
    if (result.get_type() != sol::type::none &&
        result.get_type() != sol::type::nil) {
        const auto& val = result.get<sol::object>();
        log(ConsoleEntry::Kind::Output, luaValueToString(val));
    }
    return true;
}

bool ScriptEnv::updateEntity(scene::EntityId id,
                              const std::string& code,
                              float dt) noexcept
{
    if (!lua_) return false;

    // Update dt in forge.time (elapsed is managed by setTime)
    (*lua_)["forge"]["time"]["dt"] = dt;
    (*lua_)["forge"]["__entity_id"] = static_cast<uint64_t>(id);

    // Load the script (defines global `update` function)
    auto load = lua_->safe_script(code, sol::script_pass_on_error);
    if (!load.valid()) return false;

    // Call update(dt)
    const sol::optional<sol::function> fn = (*lua_)["update"];
    if (!fn) return false;

    auto call = fn.value()(dt);
    if (!call.valid()) {
        const sol::error err = call;
        log(ConsoleEntry::Kind::Error,
            std::format("[entity {}] {}", id, err.what()));
        return false;
    }
    return true;
}

void ScriptEnv::setTime(float dt, float elapsed) noexcept {
    if (!lua_) return;
    (*lua_)["forge"]["time"]["dt"]      = dt;
    (*lua_)["forge"]["time"]["elapsed"] = elapsed;
}

void ScriptEnv::fireEvent(const std::string& name, scene::EntityId source) noexcept {
    if (!lua_ || !scene_) return;

    for (const auto& [id, ent] : scene_->entities) {
        if (const auto* pe = std::get_if<scene::PointEntity>(&ent)) {
            const auto it = pe->properties.find("script");
            if (it == pe->properties.end()) continue;
            const auto* code = std::get_if<std::string>(&it->second);
            if (!code || code->empty()) continue;

            auto load = lua_->safe_script(*code, sol::script_pass_on_error);
            if (!load.valid()) continue;

            const sol::optional<sol::function> fn = (*lua_)[name];
            if (!fn) continue;

            auto call = fn.value()(static_cast<uint64_t>(source));
            if (!call.valid()) {
                const sol::error err = call;
                log(ConsoleEntry::Kind::Error,
                    std::format("[{} entity {}] {}", name, id, err.what()));
            }
        }
    }
}

bool ScriptEnv::loadFile(const std::filesystem::path& path) noexcept {
    if (!lua_) return false;
    auto result = lua_->safe_script_file(path.string(), sol::script_pass_on_error);
    if (!result.valid()) {
        const sol::error err = result;
        log(ConsoleEntry::Kind::Error, std::string("[file] ") + err.what());
        return false;
    }
    return true;
}

} // namespace forge::script
