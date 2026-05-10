#pragma once

#include <forge/geo/Math.hpp>
#include <expected>
#include <string>
#include <string_view>

namespace forge::gfx {

// ─── Shader ───────────────────────────────────────────────────────────────────

/// An OpenGL shader program (vertex + fragment).
///
/// Compiled from GLSL source strings.
/// Move-only (owns the GL program handle).
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(Shader&&) noexcept;
    Shader& operator=(Shader&&) noexcept;
    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

    // ── Factory ───────────────────────────────────────────────────────────────

    /// Compile and link from GLSL source strings.
    /// @returns Shader on success, error message on failure.
    [[nodiscard]] static std::expected<Shader, std::string>
    compile(std::string_view vertSrc, std::string_view fragSrc) noexcept;

    // ── Usage ─────────────────────────────────────────────────────────────────

    void bind()   const noexcept;
    void unbind() const noexcept;

    [[nodiscard]] bool valid() const noexcept { return program_ != 0; }

    // ── Uniforms ──────────────────────────────────────────────────────────────
    // All setters are no-ops if the uniform name doesn't exist in the shader.

    void setInt  (std::string_view name, int v)              const noexcept;
    void setFloat(std::string_view name, float v)            const noexcept;
    void setBool (std::string_view name, bool v)             const noexcept;
    void setVec2 (std::string_view name, glm::vec2 v)        const noexcept;
    void setVec3 (std::string_view name, glm::vec3 v)        const noexcept;
    void setVec4 (std::string_view name, glm::vec4 v)        const noexcept;
    void setMat3 (std::string_view name, const glm::mat3& m) const noexcept;
    void setMat4 (std::string_view name, const glm::mat4& m) const noexcept;

private:
    uint32_t program_ = 0;

    [[nodiscard]] int location(std::string_view name) const noexcept;
};

} // namespace forge::gfx
