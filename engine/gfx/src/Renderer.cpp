#include "forge/gfx/Renderer.hpp"
#include "Shaders.hpp"

#include <GL/glew.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <format>
#include <algorithm>
#include <iostream>

namespace forge::gfx {

// ─── Init / shutdown ──────────────────────────────────────────────────────────

bool Renderer::init() noexcept {
    auto result = Shader::compile(shaders::kBrushVert, shaders::kBrushFrag);
    if (!result) {
        std::cerr << "[forge::gfx] Brush shader compile error:\n" << result.error() << '\n';
        return false;
    }
    shader_ = std::move(*result);

    // Compile PBR shader
    auto pbrResult = Shader::compile(shaders::kPBRVert, shaders::kPBRFrag);
    if (!pbrResult) {
        std::cerr << "[forge::gfx] PBR shader compile error:\n" << pbrResult.error() << '\n';
        return false;
    }
    pbrShader_ = std::move(*pbrResult);
    
    initialised_ = true;

    // Fixed GL state
    glClearColor(0.08f, 0.08f, 0.10f, 1.f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    return true;
}

void Renderer::shutdown() noexcept {
    shader_      = {};
    pbrShader_   = {};
    initialised_ = false;
}

// ─── Per-frame ────────────────────────────────────────────────────────────────

void Renderer::beginFrame(const RenderFrame& frame) noexcept {
    frame_ = frame;
    drawCallCount_ = 0;
    triangleCount_ = 0;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, frame.wireframe ? GL_LINE : GL_FILL);

    // Choose shader based on PBR setting
    Shader& activeShader = pbrEnabled_ ? pbrShader_ : shader_;
    activeShader.bind();

    // Upload frame-global uniforms (lights, camera)
    activeShader.setVec3 ("u_sunDirection",  glm::normalize(frame.sunDirection));
    activeShader.setVec3 ("u_sunColor",      frame.sunColor);
    activeShader.setFloat("u_sunIntensity",  frame.sunIntensity);
    activeShader.setVec3 ("u_ambientColor",  frame.ambientColor);
    activeShader.setVec3 ("u_cameraPos",     frame.cameraPos);

    // Fog
    activeShader.setVec3 ("u_fogColor",    frame.fogColor);
    activeShader.setFloat("u_fogDensity",  frame.fogDensity);

    // Shadows
    activeShader.setBool ("u_shadowsEnabled",   frame.shadowsEnabled);
    activeShader.setMat4 ("u_lightSpaceMatrix", frame.lightSpaceMatrix);
    activeShader.setInt  ("u_shadowMap",        frame.shadowMapUnit);

    // Point lights
    const int nLights = static_cast<int>(
        std::min(frame.pointLights.size(), static_cast<std::size_t>(kMaxPointLights)));
    activeShader.setInt("u_numPointLights", nLights);
    for (int i = 0; i < nLights; ++i) {
        const auto& pl = frame.pointLights[i];
        activeShader.setVec3 (std::format("u_plPos[{}]",       i).c_str(), pl.position);
        activeShader.setVec3 (std::format("u_plColor[{}]",     i).c_str(), pl.color);
        activeShader.setFloat(std::format("u_plIntensity[{}]", i).c_str(), pl.intensity);
        activeShader.setFloat(std::format("u_plRadius[{}]",    i).c_str(), pl.radius);
    }
}

void Renderer::submit(const DrawCall& dc) noexcept {
    if (!dc.mesh || !dc.mesh->valid()) return;

    Shader& activeShader = pbrEnabled_ ? pbrShader_ : shader_;
    
    const glm::mat4 mvp          = frame_.proj * frame_.view * dc.modelMatrix;
    const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(dc.modelMatrix)));

    activeShader.setMat4("u_mvp",          mvp);
    activeShader.setMat4("u_model",        dc.modelMatrix);
    activeShader.setMat3("u_normalMatrix", normalMatrix);

    if (pbrEnabled_ && dc.material) {
        // PBR mode: bind all material properties and textures
        activeShader.setVec3 ("u_albedo",      dc.material->albedoColor);
        activeShader.setFloat("u_metallic",    dc.material->metallic);
        activeShader.setFloat("u_roughness",   dc.material->roughness);
        activeShader.setFloat("u_ao",          dc.material->ambientOcclusion);
        activeShader.setFloat("u_normalScale", dc.material->normalScale);

        // Bind textures
        int textureUnit = 0;
        
        if (!dc.material->albedoTextureId.empty()) {
            const auto tex = textureCache_.load(dc.material->albedoTextureId);
            if (tex != 0) {
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, tex);
                activeShader.setBool("u_hasAlbedoMap", true);
                activeShader.setInt("u_albedoMap", textureUnit);
                ++textureUnit;
            }
        } else {
            activeShader.setBool("u_hasAlbedoMap", false);
        }

        if (!dc.material->normalTextureId.empty()) {
            const auto tex = textureCache_.load(dc.material->normalTextureId);
            if (tex != 0) {
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, tex);
                activeShader.setBool("u_hasNormalMap", true);
                activeShader.setInt("u_normalMap", textureUnit);
                ++textureUnit;
            }
        } else {
            activeShader.setBool("u_hasNormalMap", false);
        }

        if (!dc.material->metallicTextureId.empty()) {
            const auto tex = textureCache_.load(dc.material->metallicTextureId);
            if (tex != 0) {
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, tex);
                activeShader.setBool("u_hasMetallicMap", true);
                activeShader.setInt("u_metallicMap", textureUnit);
                ++textureUnit;
            }
        } else {
            activeShader.setBool("u_hasMetallicMap", false);
        }

        if (!dc.material->roughnessTextureId.empty()) {
            const auto tex = textureCache_.load(dc.material->roughnessTextureId);
            if (tex != 0) {
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, tex);
                activeShader.setBool("u_hasRoughnessMap", true);
                activeShader.setInt("u_roughnessMap", textureUnit);
                ++textureUnit;
            }
        } else {
            activeShader.setBool("u_hasRoughnessMap", false);
        }

        if (!dc.material->aoTextureId.empty()) {
            const auto tex = textureCache_.load(dc.material->aoTextureId);
            if (tex != 0) {
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, tex);
                activeShader.setBool("u_hasAOMap", true);
                activeShader.setInt("u_aoMap", textureUnit);
                ++textureUnit;
            }
        } else {
            activeShader.setBool("u_hasAOMap", false);
        }

        if (!dc.material->emissiveTextureId.empty()) {
            const auto tex = textureCache_.load(dc.material->emissiveTextureId);
            if (tex != 0) {
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(GL_TEXTURE_2D, tex);
                activeShader.setBool("u_hasEmissiveMap", true);
                activeShader.setInt("u_emissiveMap", textureUnit);
                ++textureUnit;
            }
        } else {
            activeShader.setBool("u_hasEmissiveMap", false);
        }
    } else {
        // Fallback to standard shader with flat color
        activeShader.setVec3 ("u_albedo",     glm::vec3(0.7f));
        activeShader.setFloat("u_roughness",  0.7f);
        activeShader.setBool ("u_hasTexture", false);
    }

    dc.mesh->draw();

    // Unbind textures
    for (int i = 0; i < 6; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    ++drawCallCount_;
    triangleCount_ += dc.mesh->indexCount() / 3;
}

void Renderer::endFrame() noexcept {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    pbrEnabled_ ? pbrShader_.unbind() : shader_.unbind();
}

} // namespace forge::gfx

