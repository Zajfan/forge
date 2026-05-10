#include "forge/runtime/GameRuntime.hpp"

#include <iostream>

namespace forge::runtime {

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

    return true;
}

void GameRuntime::shutdown() noexcept {
    player_.shutdown();
    physics_.shutdown();
}

void GameRuntime::update(float dt, const gfx::InputState& input) noexcept {
    if (!physics_.valid()) return;
    player_.update(dt, input, physics_);
    physics_.step(dt);
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

} // namespace forge::runtime
