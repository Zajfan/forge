#include "forge/gfx/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace forge::gfx {

// ─── Matrices ─────────────────────────────────────────────────────────────────

glm::vec3 OrbitCamera::position() const noexcept {
    const float yawR   = glm::radians(yaw);
    const float pitchR = glm::radians(pitch);

    return target + glm::vec3{
        distance * std::cos(pitchR) * std::cos(yawR),
        distance * std::sin(pitchR),
        distance * std::cos(pitchR) * std::sin(yawR)
    };
}

glm::mat4 OrbitCamera::viewMatrix() const noexcept {
    return glm::lookAt(position(), target, glm::vec3{ 0.f, 1.f, 0.f });
}

glm::mat4 OrbitCamera::projMatrix(float aspectRatio) const noexcept {
    return glm::perspective(glm::radians(fovY), aspectRatio, nearZ, farZ);
}

glm::mat4 OrbitCamera::vpMatrix(float aspectRatio) const noexcept {
    return projMatrix(aspectRatio) * viewMatrix();
}

// ─── Input ────────────────────────────────────────────────────────────────────

void OrbitCamera::orbit(float dx, float dy, float sensitivity) noexcept {
    yaw   += dx * sensitivity;
    pitch -= dy * sensitivity;    // invert Y: drag up = look up
    pitch  = std::clamp(pitch, -89.f, 89.f);

    // Keep yaw in [0, 360)
    yaw = std::fmod(yaw, 360.f);
    if (yaw < 0.f) yaw += 360.f;
}

void OrbitCamera::pan(float dx, float dy, float sensitivity) noexcept {
    // Build right and up vectors relative to the current camera orientation
    const glm::vec3 forward = glm::normalize(target - position());
    const glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3{0, 1, 0}));
    const glm::vec3 up      = glm::cross(right, forward);

    // Scale pan speed with distance so it stays proportional
    const float speed = distance * sensitivity;
    target -= right * (dx * speed);
    target += up    * (dy * speed);
}

void OrbitCamera::zoom(float delta, float sensitivity) noexcept {
    // Exponential zoom keeps it feeling consistent at all distances
    distance *= std::pow(1.f - sensitivity, delta);
    distance  = std::clamp(distance, nearZ * 2.f, farZ * 0.5f);
}

// ─── Utility ──────────────────────────────────────────────────────────────────

void OrbitCamera::frameAABB(const geo::AABB& box) noexcept {
    if (!box.isValid()) return;

    target   = glm::vec3(box.center());
    const glm::dvec3 ext = box.extents();
    const float diag = static_cast<float>(glm::length(ext));

    distance = diag * 1.5f;
    distance = std::max(distance, nearZ * 4.f);

    // Comfortable default angle
    yaw   = 45.f;
    pitch = 25.f;
}

glm::vec3 OrbitCamera::unproject(glm::vec2 ndc, float aspectRatio) const noexcept {
    const glm::mat4 invVP = glm::inverse(vpMatrix(aspectRatio));
    const glm::vec4 ray   = invVP * glm::vec4(ndc, 1.f, 1.f);
    return glm::normalize(glm::vec3(ray) / ray.w - position());
}

} // namespace forge::gfx
