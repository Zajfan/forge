#include "forge/runtime/GameRuntime.hpp"
#include <forge/script/ScriptEnv.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <format>
#include <optional>
#include <sstream>

namespace forge::runtime {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool keyStateFromName(const gfx::KeyState& keys, const std::string& rawName) {
    const std::string key = toLower(rawName);
    if (key == "w") return keys.w;
    if (key == "a") return keys.a;
    if (key == "s") return keys.s;
    if (key == "d") return keys.d;
    if (key == "q") return keys.q;
    if (key == "e") return keys.e;
    if (key == "r") return keys.r;
    if (key == "t") return keys.t;
    if (key == "y") return keys.y;
    if (key == "c") return keys.c;
    if (key == "v") return keys.v;
    if (key == "p") return keys.p;
    if (key == "shift") return keys.shift;
    if (key == "ctrl" || key == "control") return keys.ctrl;
    if (key == "escape" || key == "esc") return keys.escape;
    if (key == "f1") return keys.f1;
    if (key == "f5") return keys.f5;
    return false;
}

float propAsFloat(const scene::PointEntity& pe, const std::string& key, float fallback) {
    const auto it = pe.properties.find(key);
    if (it == pe.properties.end()) return fallback;
    if (const auto* f = std::get_if<float>(&it->second)) return *f;
    if (const auto* i = std::get_if<int>(&it->second)) return static_cast<float>(*i);
    if (const auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stof(*s); } catch (...) { return fallback; }
    }
    return fallback;
}

std::string propAsString(const scene::PointEntity& pe, const std::string& key) {
    const auto it = pe.properties.find(key);
    if (it == pe.properties.end()) return {};
    if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    return {};
}

std::optional<glm::vec3> vec3FromString(const std::string& text) {
    std::stringstream ss(text);
    float x = 0.f, y = 0.f, z = 0.f;
    if (ss >> x >> y >> z) return glm::vec3{x, y, z};
    return std::nullopt;
}

std::optional<glm::vec3> propAsVec3(const scene::PointEntity& pe, const std::string& key) {
    const auto it = pe.properties.find(key);
    if (it == pe.properties.end()) return std::nullopt;
    if (const auto* v = std::get_if<glm::vec3>(&it->second)) return *v;
    if (const auto* s = std::get_if<std::string>(&it->second)) return vec3FromString(*s);
    return std::nullopt;
}

} // namespace

GameRuntime::GameRuntime() : scripts_(std::make_unique<script::ScriptEnv>()) {}
GameRuntime::~GameRuntime() = default;

bool GameRuntime::init(const scene::Scene& scene, glm::vec3 spawnPos) noexcept {
    PhysicsWorld::initGlobal();

    if (!physics_.init()) {
        std::cerr << "[GameRuntime] PhysicsWorld init failed\n";
        return false;
    }

    physics_.addSceneGeometry(scene);

    if (!player_.init(physics_, spawnPos)) {
        std::cerr << "[GameRuntime] PlayerController init failed\n";
        physics_.shutdown();
        return false;
    }

    // Init scripting
    scene_ = const_cast<scene::Scene*>(&scene);
    if (!scripts_->init()) {
        std::cerr << "[GameRuntime] ScriptEnv init failed\n";
        return false;
    }
    scripts_->bindScene(scene_);

    // Wire physics callbacks (full PhysicsWorld type available here)
    script::ScriptEnv::PhysicsCallbacks cbs;
    cbs.raycast = [this](float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          float maxDist) -> script::ScriptEnv::PhysicsCallbacks::RaycastResult {
        const auto hit = physics_.raycast(
            {ox,oy,oz}, glm::normalize(glm::vec3{dx,dy,dz}), maxDist);
        if (!hit) return {};
        return { true,
            hit->point.x, hit->point.y, hit->point.z,
            hit->normal.x, hit->normal.y, hit->normal.z,
            hit->t };
    };
    cbs.applyImpulse = [this](uint32_t h, float ix, float iy, float iz) {
        physics_.applyImpulse(h, {ix, iy, iz});
    };
    cbs.getBodyPosition = [this](uint32_t h) -> std::tuple<float,float,float> {
        const auto s = physics_.getBodyState(h);
        return { s.position.x, s.position.y, s.position.z };
    };
    scripts_->bindPhysicsCallbacks(std::move(cbs));

    script::ScriptEnv::InputCallbacks icbs;
    icbs.keyDown = [this](const std::string& key) {
        return keyStateFromName(currentInput_.keys, key);
    };
    icbs.keyPressed = [this](const std::string& key) {
        const bool now = keyStateFromName(currentInput_.keys, key);
        const bool was = hasInputHistory_ ? keyStateFromName(prevInput_.keys, key) : false;
        return now && !was;
    };
    scripts_->bindInputCallbacks(std::move(icbs));

    registerSceneTriggers();
    scripts_->fireOnStart();
    elapsedSeconds_ = 0.f;
    wasOnGround_ = player_.onGround();
    hasInputHistory_ = false;

    return true;
}

void GameRuntime::shutdown() noexcept {
    scripts_->shutdown();
    player_.shutdown();
    physics_.shutdown();
    scene_ = nullptr;
    elapsedSeconds_ = 0.f;
    wasOnGround_ = false;
    hasInputHistory_ = false;
    triggers_.clear();
    triggerIndex_.clear();
    pendingTriggerFires_.clear();
}

void GameRuntime::update(float dt, const gfx::InputState& input) noexcept {
    if (!physics_.valid()) return;

    if (!hasInputHistory_) {
        currentInput_ = input;
        prevInput_ = input;
        hasInputHistory_ = true;
    } else {
        prevInput_ = currentInput_;
        currentInput_ = input;
    }

    elapsedSeconds_ += dt;
    scripts_->setTime(dt, elapsedSeconds_);

    player_.update(dt, currentInput_, physics_);
    physics_.step(dt);
    processPendingTriggerFires();

    const bool onGroundNow = player_.onGround();
    if (!wasOnGround_ && onGroundNow) {
        script::ScriptEnv::EventArgs args;
        args.classname = "player_landing";
        args.position = player_.footPosition();
        args.hasPosition = true;
        scripts_->fireEvent("on_collision", args);
    }
    wasOnGround_ = onGroundNow;

    // Run per-entity scripts
    if (scene_) {
        for (const auto& [id, ent] : scene_->entities) {
            if (const auto* pe = std::get_if<scene::PointEntity>(&ent)) {
                const auto it = pe->properties.find("script");
                if (it == pe->properties.end()) continue;
                const auto* code = std::get_if<std::string>(&it->second);
                if (code && !code->empty())
                    scripts_->updateEntity(id, *code, dt);
            }
        }
    }
}

void GameRuntime::registerSceneTriggers() noexcept {
    triggers_.clear();
    triggerIndex_.clear();
    pendingTriggerFires_.clear();
    if (!scene_) return;

    for (const auto& [id, ent] : scene_->entities) {
        const auto* pe = std::get_if<scene::PointEntity>(&ent);
        if (!pe) continue;
        if (pe->classname != "trigger_once" && pe->classname != "trigger_multiple") continue;

        glm::vec3 halfExt(64.f, 64.f, 64.f);
        if (const auto v = propAsVec3(*pe, "size"); v.has_value()) {
            halfExt = glm::abs(*v) * 0.5f;
        } else {
            const float radius = propAsFloat(*pe, "radius", 0.f);
            if (radius > 0.f) halfExt = glm::vec3(radius);
        }

        const glm::vec3 pos = glm::vec3(pe->transform.translation);
        const auto handle = physics_.addTriggerVolume(
            pos,
            halfExt,
            [this, id](scene::EntityId) { onTriggerContact(id); });

        if (handle == PhysicsWorld::kInvalidTrigger) continue;

        TriggerRuntime t;
        t.entityId = id;
        t.handle = handle;
        t.once = (pe->classname == "trigger_once");
        t.wait = std::max(0.f, propAsFloat(*pe, "wait", 0.2f));
        t.delay = std::max(0.f, propAsFloat(*pe, "delay", 0.f));
        t.target = propAsString(*pe, "target");
        t.message = propAsString(*pe, "message");
        t.classname = pe->classname;
        t.position = pos;
        t.nextFireTime = 0.f;

        triggerIndex_[id] = triggers_.size();
        triggers_.push_back(std::move(t));
    }
}

void GameRuntime::onTriggerContact(scene::EntityId triggerId) noexcept {
    const auto it = triggerIndex_.find(triggerId);
    if (it == triggerIndex_.end()) return;
    auto& t = triggers_[it->second];

    if (t.once && t.fired) return;
    if (elapsedSeconds_ < t.nextFireTime) return;

    if (t.delay > 0.f) {
        pendingTriggerFires_.push_back({ triggerId, elapsedSeconds_ + t.delay });
    } else {
        pendingTriggerFires_.push_back({ triggerId, elapsedSeconds_ });
    }

    if (t.once) {
        t.fired = true;
    } else {
        t.nextFireTime = elapsedSeconds_ + t.wait;
    }
}

void GameRuntime::processPendingTriggerFires() noexcept {
    if (pendingTriggerFires_.empty()) return;

    auto out = pendingTriggerFires_.begin();
    for (auto it = pendingTriggerFires_.begin(); it != pendingTriggerFires_.end(); ++it) {
        if (it->fireAt > elapsedSeconds_) {
            *out++ = *it;
            continue;
        }

        const auto ti = triggerIndex_.find(it->triggerId);
        if (ti == triggerIndex_.end()) continue;
        const auto& t = triggers_[ti->second];

        script::ScriptEnv::EventArgs args;
        args.source = t.entityId;
        args.classname = t.classname;
        args.target = t.target;
        args.message = t.message;
        args.delay = t.delay;
        args.wait = t.wait;
        args.position = t.position;
        args.hasPosition = true;
        scripts_->fireEvent("on_trigger", args);
    }
    pendingTriggerFires_.erase(out, pendingTriggerFires_.end());
}

glm::mat4 GameRuntime::viewMatrix() const noexcept {
    return player_.camera.viewMatrix();
}

glm::mat4 GameRuntime::projMatrix(float aspect) const noexcept {
    return player_.camera.projMatrix(aspect);
}

glm::vec3 GameRuntime::cameraPosition() const noexcept {
    return player_.camera.position;
}

glm::vec3 GameRuntime::playerPosition() const noexcept {
    return player_.footPosition();
}

bool GameRuntime::playerOnGround() const noexcept {
    return player_.onGround();
}

float GameRuntime::playerYaw() const noexcept {
    return player_.camera.yaw;
}

bool GameRuntime::playerCrouched() const noexcept {
    return player_.crouched();
}

void GameRuntime::teleportPlayer(glm::vec3 pos) noexcept {
    player_.teleport(physics_, pos);
}

void GameRuntime::toggleCrouch() noexcept {
    player_.toggleCrouch();
}

script::ScriptEnv& GameRuntime::scriptEnv() noexcept {
    return *scripts_;
}

} // namespace forge::runtime
