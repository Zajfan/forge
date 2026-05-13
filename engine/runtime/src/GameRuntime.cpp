#include "forge/runtime/GameRuntime.hpp"
#include <forge/script/ScriptEnv.hpp>

#include <iostream>
#include <format>

namespace forge::runtime {

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
    scripts_->fireOnStart();

    return true;
}

void GameRuntime::shutdown() noexcept {
    scripts_->shutdown();
    player_.shutdown();
    physics_.shutdown();
    scene_ = nullptr;
}

void GameRuntime::update(float dt, const gfx::InputState& input) noexcept {
    if (!physics_.valid()) return;
    player_.update(dt, input, physics_);
    physics_.step(dt);

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
