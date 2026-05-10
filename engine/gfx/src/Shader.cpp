#include "forge/gfx/Shader.hpp"

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <format>
#include <vector>

namespace forge::gfx {

// ─── Internal helpers ─────────────────────────────────────────────────────────

static std::expected<uint32_t, std::string>
compileStage(GLenum type, std::string_view src) noexcept {
    const GLuint id  = glCreateShader(type);
    const char*  raw = src.data();
    const int    len = static_cast<int>(src.size());
    glShaderSource(id, 1, &raw, &len);
    glCompileShader(id);

    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<std::size_t>(logLen), '\0');
        glGetShaderInfoLog(id, logLen, nullptr, log.data());
        glDeleteShader(id);
        return std::unexpected(std::format("[{}] {}",
            (type == GL_VERTEX_SHADER ? "VERT" : "FRAG"), log));
    }
    return id;
}

// ─── Shader ───────────────────────────────────────────────────────────────────

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

Shader::Shader(Shader&& o) noexcept : program_(o.program_) { o.program_ = 0; }

Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) {
        if (program_) glDeleteProgram(program_);
        program_   = o.program_;
        o.program_ = 0;
    }
    return *this;
}

std::expected<Shader, std::string>
Shader::compile(std::string_view vertSrc, std::string_view fragSrc) noexcept {
    auto vert = compileStage(GL_VERTEX_SHADER,   vertSrc);
    if (!vert) return std::unexpected(vert.error());

    auto frag = compileStage(GL_FRAGMENT_SHADER, fragSrc);
    if (!frag) {
        glDeleteShader(*vert);
        return std::unexpected(frag.error());
    }

    const GLuint prog = glCreateProgram();
    glAttachShader(prog, *vert);
    glAttachShader(prog, *frag);
    glLinkProgram(prog);

    glDeleteShader(*vert);
    glDeleteShader(*frag);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::string log(static_cast<std::size_t>(logLen), '\0');
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        glDeleteProgram(prog);
        return std::unexpected(std::format("[LINK] {}", log));
    }

    Shader s;
    s.program_ = prog;
    return s;
}

void Shader::bind()   const noexcept { glUseProgram(program_); }
void Shader::unbind() const noexcept { glUseProgram(0); }

// ─── Uniforms ─────────────────────────────────────────────────────────────────

int Shader::location(std::string_view name) const noexcept {
    return glGetUniformLocation(program_, name.data());
}

void Shader::setInt  (std::string_view n, int v)              const noexcept { glUniform1i (location(n), v); }
void Shader::setFloat(std::string_view n, float v)            const noexcept { glUniform1f (location(n), v); }
void Shader::setBool (std::string_view n, bool v)             const noexcept { glUniform1i (location(n), v ? 1 : 0); }
void Shader::setVec2 (std::string_view n, glm::vec2 v)        const noexcept { glUniform2fv(location(n), 1, glm::value_ptr(v)); }
void Shader::setVec3 (std::string_view n, glm::vec3 v)        const noexcept { glUniform3fv(location(n), 1, glm::value_ptr(v)); }
void Shader::setVec4 (std::string_view n, glm::vec4 v)        const noexcept { glUniform4fv(location(n), 1, glm::value_ptr(v)); }
void Shader::setMat3 (std::string_view n, const glm::mat3& m) const noexcept { glUniformMatrix3fv(location(n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::setMat4 (std::string_view n, const glm::mat4& m) const noexcept { glUniformMatrix4fv(location(n), 1, GL_FALSE, glm::value_ptr(m)); }

} // namespace forge::gfx
