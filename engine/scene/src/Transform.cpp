#include "forge/scene/Transform.hpp"

#include <glm/gtc/matrix_inverse.hpp>

namespace forge::scene {

glm::dmat4 Transform::matrix() const noexcept {
    // T * R * S
    glm::dmat4 m = glm::identity<glm::dmat4>();
    m = glm::translate(m, translation);
    m = m * glm::dmat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

glm::dmat4 Transform::inverseMatrix() const noexcept {
    return glm::inverse(matrix());
}

glm::dmat3 Transform::normalMatrix() const noexcept {
    // Inverse-transpose of the upper-left 3×3 of the model matrix.
    // This correctly handles non-uniform scale for surface normals.
    return glm::dmat3(glm::transpose(glm::inverse(glm::dmat3(matrix()))));
}

glm::dvec3 Transform::transformPoint(glm::dvec3 p) const noexcept {
    return glm::dvec3(matrix() * glm::dvec4(p, 1.0));
}

glm::dvec3 Transform::transformVector(glm::dvec3 v) const noexcept {
    return glm::dvec3(matrix() * glm::dvec4(v, 0.0));
}

glm::dvec3 Transform::transformNormal(glm::dvec3 n) const noexcept {
    return glm::normalize(normalMatrix() * n);
}

} // namespace forge::scene
