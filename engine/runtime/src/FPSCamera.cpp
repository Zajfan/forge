#include "forge/runtime/FPSCamera.hpp"

#include <algorithm>
#include <cmath>

namespace forge::runtime {

glm::vec3 FPSCamera::forward() const noexcept {
    const float yR = glm::radians(yaw);
    const float pR = glm::radians(pitch);
    return glm::normalize(glm::vec3{
        std::cos(pR) * std::cos(yR),
        std::sin(pR),
        std::cos(pR) * std::sin(yR)
    });
}

glm::vec3 FPSCamera::right() const noexcept {
    return glm::normalize(glm::cross(forward(), glm::vec3{0.f, 1.f, 0.f}));
}

glm::vec3 FPSCamera::up() const noexcept {
    return glm::cross(right(), forward());
}

glm::mat4 FPSCamera::viewMatrix() const noexcept {
    return glm::lookAt(position, position + forward(), glm::vec3{0.f, 1.f, 0.f});
}

glm::mat4 FPSCamera::projMatrix(float aspect) const noexcept {
    return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
}

void FPSCamera::applyMouseDelta(float dx, float dy, float sensitivity) noexcept {
    yaw   += dx * sensitivity;
    pitch -= dy * sensitivity;     // invert Y: drag up = look up
    pitch  = std::clamp(pitch, -89.f, 89.f);
    yaw    = std::fmod(yaw, 360.f);
    if (yaw < 0.f) yaw += 360.f;
}

} // namespace forge::runtime
