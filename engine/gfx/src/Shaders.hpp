#pragma once
#include <string_view>

namespace forge::gfx::shaders {

// ─────────────────────────────────────────────────────────────────────────────
// Brush vertex shader — OpenGL 4.6 Core
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kBrushVert = R"glsl(
#version 450 core

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
#version 450 core

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

// Shadows
uniform sampler2DShadow u_shadowMap;
uniform mat4            u_lightSpaceMatrix;
uniform bool            u_shadowsEnabled;

float computeShadow(vec3 worldPos, vec3 N, vec3 L) {
    if (!u_shadowsEnabled) return 0.0;

    vec4 lightClip = u_lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 proj      = lightClip.xyz / lightClip.w;
    proj           = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    float bias = max(0.0005 * (1.0 - dot(N, L)), 0.00005);
    return 1.0 - texture(u_shadowMap, vec3(proj.xy, proj.z - bias));
}

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

    float shadow = computeShadow(v_worldPos, N, L);

    vec3 color = u_ambientColor * baseAlbedo
               + u_sunColor * u_sunIntensity * NdotL * (1.0 - shadow) * baseAlbedo
               + u_sunColor * spec * (1.0 - shadow)
               + baseAlbedo * backFill;

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
#version 450 core

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
#version 450 core

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
// Shadow depth-only shaders
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kShadowVert = R"glsl(
#version 450 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_lightMVP;
void main() {
    gl_Position = u_lightMVP * vec4(a_position, 1.0);
}
)glsl";

inline constexpr std::string_view kShadowFrag = R"glsl(
#version 450 core
// No colour output — only writes gl_FragDepth (automatic)
void main() {}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Skybox vertex shader — fullscreen triangle, writes to far depth
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kSkyboxVert = R"glsl(
#version 450 core

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
#version 450 core

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


// ─────────────────────────────────────────────────────────────────────────────
// Bloom post-process shaders
// ─────────────────────────────────────────────────────────────────────────────

// Shared fullscreen-triangle vertex shader (UV in [0,1])
inline constexpr std::string_view kFullscreenVert = R"glsl(
#version 450 core
out vec2 v_uv;
void main() {
    vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    vec2 uv[3]  = vec2[](vec2(0, 0),  vec2(2, 0),  vec2(0, 2));
    v_uv        = uv[gl_VertexID];
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Bright-pass: keep only pixels above luminance threshold
inline constexpr std::string_view kBrightPassFrag = R"glsl(
#version 450 core
in  vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_scene;
uniform float     u_threshold;   // default 0.8

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 col  = texture(u_scene, v_uv).rgb;
    float lum = luminance(col);
    // Soft knee: smoothstep avoids hard cutoff artefacts
    float w = smoothstep(u_threshold * 0.9, u_threshold * 1.1, lum);
    o_color = vec4(col * w, 1.0);
}
)glsl";

// 9-tap separable Gaussian blur — run H then V
inline constexpr std::string_view kGaussianBlurFrag = R"glsl(
#version 450 core
in  vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2      u_dir;   // (1,0) or (0,1); scaled by texel size inside shader

// Gaussian weights, sigma≈2, 9 taps
const float W[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);

void main() {
    vec2 texel = u_dir / vec2(textureSize(u_tex, 0));
    vec3 col   = texture(u_tex, v_uv).rgb * W[0];
    for (int i = 1; i < 5; ++i) {
        col += texture(u_tex, v_uv + texel * float(i)).rgb * W[i];
        col += texture(u_tex, v_uv - texel * float(i)).rgb * W[i];
    }
    o_color = vec4(col, 1.0);
}
)glsl";

// Composite: additive blend bloom on top of scene
inline constexpr std::string_view kBloomCompositeFrag = R"glsl(
#version 450 core
in  vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float     u_intensity;  // default 1.0

void main() {
    vec3 scene = texture(u_scene, v_uv).rgb;
    vec3 bloom = texture(u_bloom, v_uv).rgb;

    // Add bloom additively, then re-apply Reinhard tonemapping
    vec3 combined = scene + bloom * u_intensity;
    combined = combined / (combined + vec3(1.0));
    combined = pow(clamp(combined, 0.0, 1.0), vec3(1.0 / 2.2));

    o_color = vec4(combined, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// PBR (Physically Based Rendering) vertex shader — Cook-Torrance BRDF
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kPBRVert = R"glsl(
#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_mvp;
uniform mat3 u_normalMatrix;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_tangent;

void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos    = worldPos.xyz;
    v_normal      = normalize(u_normalMatrix * a_normal);
    v_uv          = a_uv;
    v_tangent     = normalize(u_normalMatrix * vec3(1.0, 0.0, 0.0));
    gl_Position   = u_mvp * vec4(a_position, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// PBR fragment shader — Cook-Torrance with metallic/roughness/normal/AO
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kPBRFrag = R"glsl(
#version 450 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_tangent;

out vec4 o_color;

// ── Material ──────────────────────────────────────────────────────────────────
uniform vec3  u_albedo;
uniform float u_metallic;
uniform float u_roughness;
uniform float u_ao;

// ── Textures ──────────────────────────────────────────────────────────────────
uniform bool      u_hasAlbedoMap;
uniform sampler2D u_albedoMap;
uniform bool      u_hasNormalMap;
uniform sampler2D u_normalMap;
uniform float     u_normalScale;
uniform bool      u_hasMetallicMap;
uniform sampler2D u_metallicMap;
uniform bool      u_hasRoughnessMap;
uniform sampler2D u_roughnessMap;
uniform bool      u_hasAOMap;
uniform sampler2D u_aoMap;
uniform bool      u_hasEmissiveMap;
uniform sampler2D u_emissiveMap;

// ── Lighting ──────────────────────────────────────────────────────────────────
uniform vec3  u_sunDirection;
uniform vec3  u_sunColor;
uniform float u_sunIntensity;
uniform vec3  u_ambientColor;
uniform vec3  u_cameraPos;

// ── Point lights ──────────────────────────────────────────────────────────────
uniform int   u_numPointLights;
uniform vec3  u_plPos[8];
uniform vec3  u_plColor[8];
uniform float u_plIntensity[8];
uniform float u_plRadius[8];

// ── Environment ───────────────────────────────────────────────────────────────
uniform vec3  u_fogColor;
uniform float u_fogDensity;
uniform sampler2DShadow u_shadowMap;
uniform mat4            u_lightSpaceMatrix;
uniform bool            u_shadowsEnabled;

// Constants
const float PI = 3.14159265359;
const float Epsilon = 0.00001;

// Normal perturbation from normal map
vec3 perturbNormal(vec3 N, vec3 V) {
    vec3 normal = N;
    
    if (u_hasNormalMap) {
        vec3 T = normalize(v_tangent - dot(v_tangent, N) * N);
        vec3 B = cross(N, T);
        mat3 TBN = mat3(T, B, N);
        
        vec3 nmap = texture(u_normalMap, v_uv).rgb;
        nmap = normalize(nmap * 2.0 - 1.0);
        nmap.xy *= u_normalScale;
        
        normal = normalize(TBN * nmap);
    }
    
    return normal;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / max(denom, Epsilon);
}

// Schlick-Beckmann geometry shadowing
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / max(denom, Epsilon);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Cook-Torrance BRDF
vec3 cookTorrance(vec3 albedo, vec3 N, vec3 V, vec3 L, 
                   float metallic, float roughness) {
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    
    // F0 based on metalness
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    
    // Distribution, Fresnel, Geometry
    float D = distributionGGX(N, H, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float G = geometrySmith(N, V, L, roughness);
    
    // Specular component
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;  // Metals have no diffuse
    
    vec3 numerator = D * F * G;
    float denominator = 4.0 * NdotV * NdotL;
    vec3 specular = numerator / max(denominator, Epsilon);
    
    return (kD * albedo / PI + specular) * NdotL;
}

// Point light contribution (PBR)
vec3 pointLightPBR(int i, vec3 N, vec3 V, vec3 albedo, 
                    float metallic, float roughness) {
    vec3  Ld   = u_plPos[i] - v_worldPos;
    float dist = length(Ld);
    if (dist >= u_plRadius[i]) return vec3(0.0);
    
    vec3  L       = Ld / dist;
    float window  = pow(max(0.0, 1.0 - (dist / u_plRadius[i])), 2.0);
    float atten   = u_plIntensity[i] / (dist * dist + 1.0) * window;
    
    return cookTorrance(albedo, N, V, L, metallic, roughness) 
           * u_plColor[i] * atten;
}

void main() {
    // Sample textures
    vec3 albedo = u_albedo;
    if (u_hasAlbedoMap) {
        albedo *= texture(u_albedoMap, v_uv).rgb;
    }
    albedo = pow(albedo, vec3(2.2));  // sRGB → linear
    
    float metallic = u_metallic;
    if (u_hasMetallicMap) {
        metallic *= texture(u_metallicMap, v_uv).r;
    }
    
    float roughness = u_roughness;
    if (u_hasRoughnessMap) {
        roughness *= texture(u_roughnessMap, v_uv).r;
    }
    roughness = max(roughness, 0.04);  // Clamp to prevent division by zero
    
    float ao = u_ao;
    if (u_hasAOMap) {
        ao *= texture(u_aoMap, v_uv).r;
    }
    
    vec3 emissive = vec3(0.0);
    if (u_hasEmissiveMap) {
        emissive = texture(u_emissiveMap, v_uv).rgb;
    }
    
    // Perturb normal from normal map
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_cameraPos - v_worldPos);
    N = perturbNormal(N, V);
    
    // Sun/directional light
    vec3 L = normalize(u_sunDirection);
    vec3 color = cookTorrance(albedo, N, V, L, metallic, roughness)
                 * u_sunColor * u_sunIntensity;
    
    // Ambient + AO
    color += u_ambientColor * albedo * ao;
    
    // Point lights
    int numPL = min(u_numPointLights, 8);
    for (int i = 0; i < numPL; ++i) {
        color += pointLightPBR(i, N, V, albedo, metallic, roughness);
    }
    
    // Emissive
    color += emissive;
    
    // Fog
    if (u_fogDensity > 0.001) {
        float dist = length(u_cameraPos - v_worldPos);
        float fogFact = 1.0 - exp(-u_fogDensity * dist * 0.001);
        color = mix(color, u_fogColor, clamp(fogFact, 0.0, 1.0));
    }
    
    // Tonemap + gamma
    color = color / (color + vec3(1.0));
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    
    o_color = vec4(color, 1.0);
}
)glsl";

} // namespace forge::gfx::shaders
