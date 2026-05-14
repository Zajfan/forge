#include "forge/runtime/GameRuntime.hpp"
#include <forge/script/ScriptEnv.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <format>
#include <limits>
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

template<typename EntityT>
float propAsFloat(const EntityT& entity, const std::string& key, float fallback) {
    const auto it = entity.properties.find(key);
    if (it == entity.properties.end()) return fallback;
    if (const auto* f = std::get_if<float>(&it->second)) return *f;
    if (const auto* i = std::get_if<int>(&it->second)) return static_cast<float>(*i);
    if (const auto* s = std::get_if<std::string>(&it->second)) {
        try { return std::stof(*s); } catch (...) { return fallback; }
    }
    return fallback;
}

template<typename EntityT>
std::string propAsString(const EntityT& entity, const std::string& key) {
    const auto it = entity.properties.find(key);
    if (it == entity.properties.end()) return {};
    if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    return {};
}

template<typename EntityT>
bool propAsBool(const EntityT& entity, const std::string& key, bool fallback) {
    const auto it = entity.properties.find(key);
    if (it == entity.properties.end()) return fallback;
    if (const auto* b = std::get_if<bool>(&it->second)) return *b;
    if (const auto* i = std::get_if<int>(&it->second)) return *i != 0;
    if (const auto* f = std::get_if<float>(&it->second)) return std::abs(*f) > 1e-6f;
    if (const auto* s = std::get_if<std::string>(&it->second)) {
        const std::string lower = toLower(*s);
        return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
    }
    return fallback;
}

std::string resolvePlayerTeamFromScene(const scene::Scene& scene, glm::vec3 spawnPos) {
    std::string resolved;
    double bestDistSq = std::numeric_limits<double>::max();

    for (const auto& [id, ent] : scene.entities) {
        (void)id;
        const auto* pe = std::get_if<scene::PointEntity>(&ent);
        if (!pe || pe->classname != "info_player_start") continue;

        const std::string team = propAsString(*pe, "team");
        if (team.empty()) continue;

        const glm::vec3 delta = glm::vec3(pe->transform.translation) - spawnPos;
        const double distSq = static_cast<double>(glm::dot(delta, delta));
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            resolved = team;
        }
    }

    return resolved;
}

std::optional<glm::vec3> vec3FromString(const std::string& text) {
    std::stringstream ss(text);
    float x = 0.f, y = 0.f, z = 0.f;
    if (ss >> x >> y >> z) return glm::vec3{x, y, z};
    return std::nullopt;
}

template<typename EntityT>
std::optional<glm::vec3> propAsVec3(const EntityT& entity, const std::string& key) {
    const auto it = entity.properties.find(key);
    if (it == entity.properties.end()) return std::nullopt;
    if (const auto* v = std::get_if<glm::vec3>(&it->second)) return *v;
    if (const auto* s = std::get_if<std::string>(&it->second)) return vec3FromString(*s);
    return std::nullopt;
}

glm::dvec3 motionDirectionFromAngle(double angleDegrees) {
    if (angleDegrees == -1.0) return {0.0, 1.0, 0.0};
    if (angleDegrees == -2.0) return {0.0, -1.0, 0.0};

    const double r = glm::radians(angleDegrees);
    return glm::normalize(glm::dvec3(std::cos(r), 0.0, std::sin(r)));
}

// Helper: Test if a world-space point is inside a transformed convex brush
bool pointInTransformedBrush(glm::dvec3 worldPoint,
                              const geo::Brush& brush,
                              const scene::Transform& transform) noexcept {
    // Transform world point to local brush space
    const glm::dmat4 invTransform = glm::inverse(glm::dmat4(transform.matrix()));
    const glm::dvec4 localH = invTransform * glm::dvec4(worldPoint, 1.0);
    const glm::dvec3 localPoint = glm::dvec3(localH) / localH.w;

    // Test against all brush faces using plane equations
    // A point is inside if it's on the back side (inside) of all planes (eval <= 0)
    for (const auto& face : brush.faces) {
        const double signedDist = face.plane.eval(localPoint);
        if (signedDist > 1e-6) return false;  // point is outside (in front of) this plane
    }
    return true;
}

// Helper: Check if entity classname matches a filter pattern
bool classNameMatches(const std::string& entityClass, const std::string& pattern) noexcept {
    if (pattern.empty()) return true;  // no filter
    return entityClass == pattern || pattern == "*";
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
    playerTeam_ = resolvePlayerTeamFromScene(scene, spawnPos);

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
    registerBrushLogicEntities();
    scripts_->fireOnStart();
    elapsedSeconds_ = 0.f;
    wasOnGround_ = player_.onGround();
    hasInputHistory_ = false;

    return true;
}

void GameRuntime::shutdown() noexcept {
    for (auto& logic : brushLogic_) {
        if (logic.bodyHandle != PhysicsWorld::kInvalidKinematic)
            physics_.removeKinematicBody(logic.bodyHandle);
    }
    scripts_->shutdown();
    player_.shutdown();
    physics_.shutdown();
    scene_ = nullptr;
    elapsedSeconds_ = 0.f;
    wasOnGround_ = false;
    hasInputHistory_ = false;
    playerTeam_.clear();
    triggers_.clear();
    triggerIndex_.clear();
    pendingTriggerFires_.clear();
    brushLogic_.clear();
    brushLogicIndex_.clear();
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

    updateBrushLogic(dt);
    player_.update(dt, currentInput_, physics_);
    physics_.setPrimaryTriggerProbe(player_.footPosition());
    physics_.step(dt);
    processPendingTriggerFires();
    updateTriggerOccupancy();  // Check for enter/exit events

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
        std::string classname;
        glm::vec3 pos(0.f);
        glm::vec3 halfExt(64.f, 64.f, 64.f);
        float wait = 0.2f;
        float delay = 0.f;
        std::string target;
        std::string message;
        std::string filterClass;
        std::string filterTeam;
        bool oncePerEntity = false;
        const scene::BrushEntity* brushEnt = nullptr;

        if (const auto* be = std::get_if<scene::BrushEntity>(&ent)) {
            classname = be->classname;
            if (classname != "trigger_once" && classname != "trigger_multiple") continue;

            const geo::AABB wb = be->worldBounds();
            if (!wb.isValid()) continue;
            pos = glm::vec3(wb.center());
            halfExt = glm::max(glm::vec3(wb.extents()) * 0.5f, glm::vec3(1.f));

            wait = std::max(0.f, propAsFloat(*be, "wait", 0.2f));
            delay = std::max(0.f, propAsFloat(*be, "delay", 0.f));
            target = propAsString(*be, "target");
            message = propAsString(*be, "message");
            filterClass = propAsString(*be, "filter_classname");
            filterTeam = propAsString(*be, "filter_team");
            oncePerEntity = propAsBool(*be, "once_per_entity", classname == "trigger_once");
            brushEnt = be;
        } else if (const auto* pe = std::get_if<scene::PointEntity>(&ent)) {
            // Backward compatibility for older scenes using point trigger entities.
            classname = pe->classname;
            if (classname != "trigger_once" && classname != "trigger_multiple") continue;

            if (const auto v = propAsVec3(*pe, "size"); v.has_value()) {
                halfExt = glm::abs(*v) * 0.5f;
            } else {
                const float radius = propAsFloat(*pe, "radius", 0.f);
                if (radius > 0.f) halfExt = glm::vec3(radius);
            }
            pos = glm::vec3(pe->transform.translation);

            wait = std::max(0.f, propAsFloat(*pe, "wait", 0.2f));
            delay = std::max(0.f, propAsFloat(*pe, "delay", 0.f));
            target = propAsString(*pe, "target");
            message = propAsString(*pe, "message");
            filterClass = propAsString(*pe, "filter_classname");
            filterTeam = propAsString(*pe, "filter_team");
            oncePerEntity = propAsBool(*pe, "once_per_entity", classname == "trigger_once");
        } else {
            continue;
        }

        const auto handle = physics_.addTriggerVolume(
            pos,
            halfExt,
            [this, id](scene::EntityId) { onTriggerContact(id); });

        if (handle == PhysicsWorld::kInvalidTrigger) continue;

        TriggerRuntime t;
        t.entityId = id;
        t.handle = handle;
        t.once = (classname == "trigger_once");
        t.wait = wait;
        t.delay = delay;
        t.target = std::move(target);
        t.message = std::move(message);
        t.classname = std::move(classname);
        t.position = pos;
        t.nextFireTime = 0.f;
        t.filterClassname = std::move(filterClass);
        t.filterTeam = std::move(filterTeam);
        t.oncePerEntity = oncePerEntity || t.once;

        // Store brush geometry for precise testing if available
        if (brushEnt) {
            t.useGeometry = true;
            t.triggerBrushes = brushEnt->brushes;
            t.triggerTransform = brushEnt->transform;
        }

        triggerIndex_[id] = triggers_.size();
        triggers_.push_back(std::move(t));
    }
}

void GameRuntime::registerBrushLogicEntities() noexcept {
    brushLogic_.clear();
    brushLogicIndex_.clear();
    if (!scene_) return;

    for (const auto& [id, ent] : scene_->entities) {
        const auto* be = std::get_if<scene::BrushEntity>(&ent);
        if (!be || be->classname.empty()) continue;

        BrushLogicRuntime logic;
        logic.entityId = id;
        logic.baseTransform = be->transform;
        logic.closedTranslation = be->transform.translation;
        logic.targetname = propAsString(*be, "targetname");
        logic.classname = be->classname;

        if (be->classname == "func_door") {
            logic.kind = BrushLogicRuntime::Kind::Door;
            logic.speed = std::max(1.0, static_cast<double>(propAsFloat(*be, "speed", 100.f)));
            logic.wait  = std::max(0.0, static_cast<double>(propAsFloat(*be, "wait", 3.f)));
            logic.lip   = static_cast<double>(propAsFloat(*be, "lip", 8.f));
            logic.angle = static_cast<double>(propAsFloat(*be, "angle", 0.f));

            const auto bounds = be->worldBounds();
            const glm::dvec3 ext = bounds.isValid() ? bounds.extents() : glm::dvec3(128.0);
            const glm::dvec3 dir = motionDirectionFromAngle(logic.angle);
            double travel = glm::dot(glm::abs(dir), ext) - logic.lip;
            if (travel <= 1.0) travel = 64.0;
            logic.openTranslation = logic.closedTranslation + dir * travel;
            logic.active = false;
        } else if (be->classname == "func_plat") {
            logic.kind = BrushLogicRuntime::Kind::Plat;
            logic.speed = std::max(1.0, static_cast<double>(propAsFloat(*be, "speed", 150.f)));
            logic.wait  = std::max(0.0, static_cast<double>(propAsFloat(*be, "wait", 1.f)));
            logic.height = static_cast<double>(propAsFloat(*be, "height", 0.f));
            if (logic.height <= 0.0) {
                const auto bounds = be->worldBounds();
                const glm::dvec3 ext = bounds.isValid() ? bounds.extents() : glm::dvec3(128.0);
                logic.height = std::max(32.0, ext.y - 8.0);
            }
            logic.openTranslation = logic.closedTranslation + glm::dvec3(0.0, logic.height, 0.0);
            logic.active = false;
        } else if (be->classname == "func_rotating") {
            logic.kind = BrushLogicRuntime::Kind::Rotating;
            logic.speed = static_cast<double>(propAsFloat(*be, "speed", 100.f));
            logic.openTranslation = logic.closedTranslation;
            logic.active = true;
        } else {
            continue;
        }

        logic.bodyHandle = physics_.addKinematicBrushEntity(*be);
        if (logic.bodyHandle == PhysicsWorld::kInvalidKinematic)
            continue;

        brushLogicIndex_[id] = brushLogic_.size();
        brushLogic_.push_back(std::move(logic));
    }
}

void GameRuntime::onTriggerContact(scene::EntityId triggerId) noexcept {
    (void)triggerId;
    // Trigger fire scheduling is handled by updateTriggerOccupancy() to ensure
    // a single consistent path with precise geometry + filtering.
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
        dispatchTargetActivations(t);
    }
    pendingTriggerFires_.erase(out, pendingTriggerFires_.end());
}

void GameRuntime::dispatchTargetActivations(const TriggerRuntime& trigger) noexcept {
    if (!scene_ || trigger.target.empty()) return;

    for (const auto& [id, ent] : scene_->entities) {
        const std::string targetname = std::visit([](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, scene::PointEntity> || std::is_same_v<T, scene::BrushEntity>)
                return propAsString(e, "targetname");
            else
                return std::string{};
        }, ent);
        if (targetname.empty() || targetname != trigger.target) continue;

        script::ScriptEnv::EventArgs args;
        args.source = trigger.entityId;
        args.other = id;
        args.target = trigger.target;
        args.message = trigger.message;
        args.delay = trigger.delay;
        args.wait = trigger.wait;

        if (const auto* pe = std::get_if<scene::PointEntity>(&ent)) {
            args.classname = pe->classname;
            args.position = glm::vec3(pe->transform.translation);
            args.hasPosition = true;
            scripts_->fireEntityEvent(id, "on_activate", args);
            scripts_->fireEvent("on_target", args);
        } else if (const auto* be = std::get_if<scene::BrushEntity>(&ent)) {
            args.classname = be->classname;
            args.position = glm::vec3(be->transform.translation);
            args.hasPosition = true;
            activateBrushLogic(id);
            scripts_->fireEvent("on_target", args);
        }
    }
}

void GameRuntime::activateBrushLogic(scene::EntityId entityId) noexcept {
    const auto it = brushLogicIndex_.find(entityId);
    if (it == brushLogicIndex_.end()) return;
    auto& logic = brushLogic_[it->second];

    switch (logic.kind) {
    case BrushLogicRuntime::Kind::Door:
    case BrushLogicRuntime::Kind::Plat:
        if (logic.motionState == BrushLogicRuntime::MotionState::Open ||
            logic.motionState == BrushLogicRuntime::MotionState::Opening) {
            logic.closeAt = elapsedSeconds_ + logic.wait;
        } else {
            logic.motionState = BrushLogicRuntime::MotionState::Opening;
            logic.closeAt = -1.0;
        }
        break;
    case BrushLogicRuntime::Kind::Rotating:
        logic.active = !logic.active;
        break;
    }
}

void GameRuntime::updateBrushLogic(float dt) noexcept {
    if (!scene_) return;

    for (auto& logic : brushLogic_) {
        auto* ent = scene_->getEntity(logic.entityId);
        auto* be = ent ? std::get_if<scene::BrushEntity>(ent) : nullptr;
        if (!be) continue;

        if (logic.bodyHandle == PhysicsWorld::kInvalidKinematic) continue;

        if (logic.kind == BrushLogicRuntime::Kind::Rotating) {
            if (!logic.active) continue;
            logic.rotationAngle += logic.speed * dt;
            be->transform.rotation = logic.baseTransform.rotation *
                glm::angleAxis(glm::radians(static_cast<double>(logic.rotationAngle)), glm::dvec3(0.0, 1.0, 0.0));
            physics_.setKinematicTransform(logic.bodyHandle, be->transform, dt);
            continue;
        }

        const glm::dvec3 target =
            (logic.motionState == BrushLogicRuntime::MotionState::Opening ||
             logic.motionState == BrushLogicRuntime::MotionState::Open)
                ? logic.openTranslation
                : logic.closedTranslation;

        glm::dvec3 delta = target - be->transform.translation;
        const double dist = glm::length(delta);
        if (dist > 1e-6) {
            const double step = logic.speed * dt;
            if (step >= dist) {
                be->transform.translation = target;
            } else {
                be->transform.translation += (delta / dist) * step;
            }
        }

        const double remaining = glm::length(target - be->transform.translation);
        if (remaining <= 1e-4) {
            if (logic.motionState == BrushLogicRuntime::MotionState::Opening) {
                logic.motionState = BrushLogicRuntime::MotionState::Open;
                logic.closeAt = elapsedSeconds_ + logic.wait;
            } else if (logic.motionState == BrushLogicRuntime::MotionState::Closing) {
                logic.motionState = BrushLogicRuntime::MotionState::Closed;
                logic.closeAt = -1.0;
            }
        }

        if (logic.motionState == BrushLogicRuntime::MotionState::Open &&
            logic.closeAt >= 0.0 && elapsedSeconds_ >= logic.closeAt) {
            logic.motionState = BrushLogicRuntime::MotionState::Closing;
        }

        physics_.setKinematicTransform(logic.bodyHandle, be->transform, dt);
    }
}

void GameRuntime::updateTriggerOccupancy() noexcept {
    if (!scene_) return;

    constexpr scene::EntityId kPlayerOccupant = scene::kInvalidEntityId;
    const std::string playerClassname = "player";
    const std::string& playerTeam = playerTeam_;
    const glm::dvec3 playerPos = glm::dvec3(player_.footPosition());

    for (auto& trigger : triggers_) {
        if (trigger.once && trigger.fired) continue;

        // Determine if player is currently inside this trigger
        bool playerInside = false;

        if (trigger.useGeometry) {
            // Use precise brush geometry testing
            for (const auto& brush : trigger.triggerBrushes) {
                if (pointInTransformedBrush(playerPos, brush, trigger.triggerTransform)) {
                    playerInside = true;
                    break;
                }
            }
        } else {
            // Fall back to point-in-sphere testing
            const float defaultRadius = 64.f;
            const float dist = glm::distance(glm::vec3(playerPos), trigger.position);
            playerInside = dist <= defaultRadius;
        }

        const bool classPass = classNameMatches(playerClassname, trigger.filterClassname);
        const bool teamPass = classNameMatches(playerTeam, trigger.filterTeam);
        trigger.lastClassFilterPass = classPass;
        trigger.lastTeamFilterPass = teamPass;

        if (!classPass || !teamPass) {
            playerInside = false;
        }

        bool wasInside = trigger.occupants.count(kPlayerOccupant) > 0;
        trigger.lastInside = playerInside;

        if (playerInside && !wasInside) {
            // Player entered the trigger
            trigger.occupants.insert(kPlayerOccupant);

            bool canFire = elapsedSeconds_ >= trigger.nextFireTime;
            if (trigger.oncePerEntity && trigger.firedOccupants.count(kPlayerOccupant) > 0)
                canFire = false;

            if (canFire) {
                if (trigger.delay > 0.f) {
                    pendingTriggerFires_.push_back({ trigger.entityId, elapsedSeconds_ + trigger.delay });
                } else {
                    pendingTriggerFires_.push_back({ trigger.entityId, elapsedSeconds_ });
                }
                ++trigger.enterFireCount;

                trigger.firedOccupants.insert(kPlayerOccupant);

                if (trigger.once) {
                    trigger.fired = true;
                } else {
                    trigger.nextFireTime = elapsedSeconds_ + trigger.wait;
                }
            }
        } else if (!playerInside && wasInside) {
            // Player exited the trigger
            trigger.occupants.erase(kPlayerOccupant);

            // Fire on_trigger_exit event
            script::ScriptEnv::EventArgs args;
            args.source = trigger.entityId;
            args.classname = trigger.classname;
            args.target = trigger.target;
            args.message = trigger.message;
            args.delay = trigger.delay;
            args.wait = trigger.wait;
            args.position = trigger.position;
            args.hasPosition = true;
            args.isExit = true;
            scripts_->fireEvent("on_trigger_exit", args);
            ++trigger.exitFireCount;
        }
    }
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

const std::string& GameRuntime::playerTeam() const noexcept {
    return playerTeam_;
}

std::vector<GameRuntime::TriggerDebugInfo> GameRuntime::triggerDebugSnapshot() const {
    constexpr scene::EntityId kPlayerOccupant = scene::kInvalidEntityId;
    std::vector<TriggerDebugInfo> out;
    out.reserve(triggers_.size());

    for (const auto& t : triggers_) {
        TriggerDebugInfo d;
        d.entityId = t.entityId;
        d.classname = t.classname;
        d.target = t.target;
        d.filterClassname = t.filterClassname;
        d.filterTeam = t.filterTeam;
        d.fired = t.fired;
        d.once = t.once;
        d.oncePerEntity = t.oncePerEntity;
        d.classFilterPass = t.lastClassFilterPass;
        d.teamFilterPass = t.lastTeamFilterPass;

        d.occupants.reserve(t.occupants.size());
        for (const auto& occupantId : t.occupants) {
            OccupantDebugInfo o;
            o.entityId = occupantId;
            o.firedForThis = t.firedOccupants.count(occupantId) > 0;
            o.fireCount = (occupantId == kPlayerOccupant) ? t.enterFireCount : 0;
            d.occupants.push_back(std::move(o));
        }

        out.push_back(std::move(d));
    }

    return out;
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
