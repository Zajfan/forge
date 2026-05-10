#pragma once
#include <string_view>

namespace forge::gfx::shaders {

// ─────────────────────────────────────────────────────────────────────────────
// Brush vertex shader — OpenGL 4.6 Core
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kBrushVert = R"glsl(
#version 460 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

// Per-object uniforms
uniform mat4 u_model;
uniform mat4 u_mvp;
uniform mat3 u_normalMatrix;

// Per-vertex outputs
out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos    = worldPos.xyz;
    v_normal      = normalize(u_normalMatrix * a_normal);
    v_uv          = a_uv;
    gl_Position   = u_mvp * vec4(a_position, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Brush fragment shader — Blinn-Phong + ambient + gamma correction
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kBrushFrag = R"glsl(
#version 460 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;

out vec4 o_color;

// ── Directional (sun) light ──────────────────────────────────────────────────
uniform vec3  u_sunDirection;
uniform vec3  u_sunColor;
uniform float u_sunIntensity;
uniform vec3  u_ambientColor;
uniform vec3  u_cameraPos;

// ── Point lights (up to 8) ───────────────────────────────────────────────────
uniform int   u_numPointLights;
uniform vec3  u_plPos[8];        // world-space position
uniform vec3  u_plColor[8];      // linear RGB
uniform float u_plIntensity[8];  // units²
uniform float u_plRadius[8];     // hard cutoff

// ── Material ─────────────────────────────────────────────────────────────────
uniform vec3  u_albedo;
uniform float u_roughness;
uniform bool  u_wireframe;

// Texture
uniform bool      u_hasTexture;
uniform sampler2D u_albedoMap;

// Fog (exponential)
uniform vec3  u_fogColor;
uniform float u_fogDensity;  // 0 = off

// Blinn-Phong point-light contribution
vec3 pointLightContrib(int i, vec3 N, vec3 V, vec3 albedo) {
    vec3  Ld   = u_plPos[i] - v_worldPos;
    float dist = length(Ld);
    if (dist >= u_plRadius[i]) return vec3(0.0);

    vec3  L       = Ld / dist;
    // Inverse-square with smooth window
    float window  = pow(max(0.0, 1.0 - (dist / u_plRadius[i])), 2.0);
    float atten   = u_plIntensity[i] / (dist * dist + 1.0) * window;

    float NdotL   = max(dot(N, L), 0.0);
    vec3  H       = normalize(L + V);
    float NdotH   = max(dot(N, H), 0.0);
    float shine   = mix(4.0, 64.0, 1.0 - u_roughness);
    float spec    = pow(NdotH, shine) * (1.0 - u_roughness) * 0.2;

    return (NdotL * albedo + vec3(spec)) * u_plColor[i] * atten;
}

void main() {
    if (u_wireframe) {
        o_color = vec4(0.2, 0.8, 0.3, 1.0);
        return;
    }

    vec3 baseAlbedo = u_albedo;
    if (u_hasTexture) {
        vec4 texSample = texture(u_albedoMap, v_uv);
        baseAlbedo *= texSample.rgb;
    }

    vec3 N = normalize(v_normal);
    vec3 L = normalize(u_sunDirection);
    vec3 V = normalize(u_cameraPos - v_worldPos);
    vec3 H = normalize(L + V);

    // ── Sun / directional ─────────────────────────────────────────────────────
    float NdotL   = max(dot(N, L), 0.0);
    float shine   = mix(4.0, 128.0, 1.0 - u_roughness);
    float NdotH   = max(dot(N, H), 0.0);
    float spec    = pow(NdotH, shine) * (1.0 - u_roughness) * 0.3;
    float backFill = max(dot(-N, L), 0.0) * 0.05;

    vec3 color = u_ambientColor * baseAlbedo
               + u_sunColor * u_sunIntensity * NdotL * baseAlbedo
               + u_sunColor * spec
               + u_albedo * backFill;

    // ── Point lights ──────────────────────────────────────────────────────────
    int n = min(u_numPointLights, 8);
    for (int i = 0; i < n; ++i)
        color += pointLightContrib(i, N, V, baseAlbedo);

    // ── Fog ──────────────────────────────────────────────────────────────────────
    if (u_fogDensity > 0.001) {
        float dist    = length(u_cameraPos - v_worldPos);
        float fogFact = 1.0 - exp(-u_fogDensity * dist * 0.001);
        color = mix(color, u_fogColor, clamp(fogFact, 0.0, 1.0));
    }

    // ── Tonemap + gamma ───────────────────────────────────────────────────────
    color = color / (color + vec3(1.0));
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    o_color = vec4(color, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Grid vertex shader — infinite grid in the XZ plane
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kGridVert = R"glsl(
#version 460 core

// Fullscreen triangle trick — no vertex buffer needed
// gl_VertexID: 0=(−1,−1), 1=(3,−1), 2=(−1,3) → covers entire screen
out vec3 v_nearPoint;
out vec3 v_farPoint;

uniform mat4 u_invVP;

vec3 unproject(vec2 p, float z) {
    vec4 h = u_invVP * vec4(p, z, 1.0);
    return h.xyz / h.w;
}

void main() {
    vec2 ndc[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    vec2 p  = ndc[gl_VertexID];
    v_nearPoint = unproject(p,  0.0);
    v_farPoint  = unproject(p,  1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Grid fragment shader — world-space grid with anti-aliasing
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kGridFrag = R"glsl(
#version 460 core

in vec3 v_nearPoint;
in vec3 v_farPoint;

out vec4 o_color;

uniform float u_nearZ;
uniform float u_farZ;
uniform mat4  u_proj;
uniform mat4  u_view;

vec4 grid(vec3 pos, float scale, bool drawAxis) {
    vec2  coord = pos.xz * scale;
    vec2  deriv = fwidth(coord);
    vec2  grid  = abs(fract(coord - 0.5) - 0.5) / deriv;
    float line  = min(grid.x, grid.y);
    float minZ  = min(deriv.y, 1.0);
    float minX  = min(deriv.x, 1.0);
    vec4  color = vec4(0.3, 0.3, 0.3, 1.0 - min(line, 1.0));

    if (drawAxis) {
        if (pos.x > -0.1 * minX && pos.x < 0.1 * minX)
            color.z = 1.0; // Z axis = blue
        if (pos.z > -0.1 * minZ && pos.z < 0.1 * minZ)
            color.x = 1.0; // X axis = red
    }
    return color;
}

float computeDepth(vec3 pos) {
    vec4 clip = u_proj * u_view * vec4(pos, 1.0);
    return clip.z / clip.w;
}

float linearDepth(float depth) {
    return (2.0 * u_nearZ * u_farZ) / (u_farZ + u_nearZ - depth * (u_farZ - u_nearZ));
}

void main() {
    float t = -v_nearPoint.y / (v_farPoint.y - v_nearPoint.y);
    if (t <= 0.0) discard;

    vec3 pos = v_nearPoint + t * (v_farPoint - v_nearPoint);

    gl_FragDepth = computeDepth(pos);

    float lin = linearDepth(gl_FragDepth);
    float fade = 1.0 - clamp((lin - u_nearZ * 4.0) / (u_farZ * 0.5), 0.0, 1.0);

    o_color  = (grid(pos, 1.0/64.0, true) + grid(pos, 1.0/512.0, false)) * fade;
    o_color.a *= fade;
    if (o_color.a < 0.01) discard;
}
)glsl";


// ─────────────────────────────────────────────────────────────────────────────
// Skybox vertex shader — fullscreen triangle, writes to far depth
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kSkyboxVert = R"glsl(
#version 460 core

out vec3 v_worldDir;
uniform mat4 u_invVP;

void main() {
    // Fullscreen triangle from gl_VertexID
    vec2 ndc[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    vec2 p = ndc[gl_VertexID];

    // Unproject to world direction
    vec4 h = u_invVP * vec4(p, 1.0, 1.0);
    v_worldDir = h.xyz / h.w;

    // Write at maximum depth so geometry always wins
    gl_Position = vec4(p, 0.9999, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Skybox fragment shader — horizon-zenith-ground gradient
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kSkyboxFrag = R"glsl(
#version 460 core

in  vec3 v_worldDir;
out vec4 o_color;

uniform vec3 u_skyZenith;   // colour directly overhead
uniform vec3 u_skyHorizon;  // colour at eye level
uniform vec3 u_skyGround;   // colour below horizon

void main() {
    vec3 d = normalize(v_worldDir);
    vec3 sky;
    if (d.y >= 0.0) {
        // Upper hemisphere: horizon → zenith
        float t = pow(clamp(d.y, 0.0, 1.0), 0.45);
        sky = mix(u_skyHorizon, u_skyZenith, t);
    } else {
        // Lower hemisphere: horizon → ground
        float t = pow(clamp(-d.y, 0.0, 1.0), 0.35);
        sky = mix(u_skyHorizon, u_skyGround, t);
    }
    // Simple sun disc: large directional blip for atmosphere
    o_color = vec4(sky, 1.0);
}
)glsl";

} // namespace forge::gfx::shaders
