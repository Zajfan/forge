#include "forge/gfx/MeshAssetCache.hpp"

// tinygltf — STB implementations are in stb_impl.cpp
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <format>
#include <numeric>

namespace forge::gfx {

MeshAssetCache::MeshAssetCache()  = default;
MeshAssetCache::~MeshAssetCache() { evictAll(); }

void MeshAssetCache::setAssetRoot(const std::filesystem::path& root) noexcept {
    root_ = root;
}

std::filesystem::path MeshAssetCache::resolve(const std::string& p) const noexcept {
    const std::filesystem::path path(p);
    if (path.is_absolute() && std::filesystem::exists(path)) return path;
    if (!root_.empty()) {
        auto full = root_ / path;
        if (std::filesystem::exists(full)) return full;
    }
    if (std::filesystem::exists(path)) return path;
    return {};
}

void MeshAssetCache::evictAll() noexcept { cache_.clear(); }

// ─── GLTF loading ─────────────────────────────────────────────────────────────

static glm::vec3 readVec3(const tinygltf::Model& m, int accIdx, int i) {
    const auto& acc  = m.accessors[accIdx];
    const auto& bv   = m.bufferViews[acc.bufferView];
    const auto& buf  = m.buffers[bv.buffer];
    const int stride = acc.ByteStride(bv) > 0 ? acc.ByteStride(bv) : 12;
    const uint8_t* ptr = buf.data.data() + bv.byteOffset + acc.byteOffset
                       + static_cast<std::size_t>(i) * stride;
    float v[3];
    std::memcpy(v, ptr, 12);
    return { v[0], v[1], v[2] };
}

static glm::vec2 readVec2(const tinygltf::Model& m, int accIdx, int i) {
    const auto& acc  = m.accessors[accIdx];
    const auto& bv   = m.bufferViews[acc.bufferView];
    const auto& buf  = m.buffers[bv.buffer];
    const int stride = acc.ByteStride(bv) > 0 ? acc.ByteStride(bv) : 8;
    const uint8_t* ptr = buf.data.data() + bv.byteOffset + acc.byteOffset
                       + static_cast<std::size_t>(i) * stride;
    float v[2];
    std::memcpy(v, ptr, 8);
    return { v[0], v[1] };
}

static uint32_t readIndex(const tinygltf::Model& m, int accIdx, int i) {
    const auto& acc = m.accessors[accIdx];
    const auto& bv  = m.bufferViews[acc.bufferView];
    const uint8_t* base = m.buffers[bv.buffer].data.data()
                        + bv.byteOffset + acc.byteOffset;

    switch (acc.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return static_cast<uint32_t>(base[i]);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        uint16_t v; std::memcpy(&v, base + i * 2, 2); return v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        uint32_t v; std::memcpy(&v, base + i * 4, 4); return v;
    }
    default: return 0;
    }
}

std::optional<build::EntityMesh>
MeshAssetCache::loadGLTF(const std::filesystem::path& path) noexcept {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    const bool binary = path.extension() == ".glb";
    bool ok = binary
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path.string())
        : loader.LoadASCIIFromFile (&model, &err, &warn, path.string());

    if (!warn.empty()) std::cerr << "[MeshAssetCache] Warning: " << warn << '\n';
    if (!ok)           { std::cerr << "[MeshAssetCache] Error: " << err << '\n'; return {}; }

    build::EntityMesh result;

    // Traverse default scene nodes
    const int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (model.scenes.empty()) return {};

    const auto& scene = model.scenes[sceneIdx];

    auto processNode = [&](auto& self, int nodeIdx) -> void {
        const auto& node = model.nodes[nodeIdx];

        if (node.mesh >= 0) {
            const auto& mesh = model.meshes[node.mesh];
            for (const auto& prim : mesh.primitives) {
                if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

                const auto posIt   = prim.attributes.find("POSITION");
                const auto normIt  = prim.attributes.find("NORMAL");
                const auto uvIt    = prim.attributes.find("TEXCOORD_0");
                if (posIt == prim.attributes.end()) continue;

                const int posAcc  = posIt->second;
                const int normAcc = normIt  != prim.attributes.end() ? normIt->second  : -1;
                const int uvAcc   = uvIt    != prim.attributes.end() ? uvIt->second    : -1;

                const int vertCount = static_cast<int>(model.accessors[posAcc].count);
                const int idxCount  = prim.indices >= 0
                    ? static_cast<int>(model.accessors[prim.indices].count) : 0;

                build::MeshData sub;
                sub.materialId = prim.material >= 0
                    ? model.materials[prim.material].name
                    : "default";

                sub.vertices.reserve(vertCount);
                for (int i = 0; i < vertCount; ++i) {
                    build::Vertex v;
                    v.position = readVec3(model, posAcc, i);
                    v.normal   = normAcc >= 0 ? readVec3(model, normAcc, i)
                                              : glm::vec3{0.f,1.f,0.f};
                    v.uv       = uvAcc   >= 0 ? readVec2(model, uvAcc,   i)
                                              : glm::vec2{0.f,0.f};
                    sub.bounds.expand(geo::AABB{glm::dvec3(v.position), glm::dvec3(v.position)});
                    sub.vertices.push_back(v);
                }

                if (idxCount > 0) {
                    sub.indices.reserve(idxCount);
                    for (int i = 0; i < idxCount; ++i)
                        sub.indices.push_back(readIndex(model, prim.indices, i));
                } else {
                    // Non-indexed — generate sequential indices
                    sub.indices.resize(vertCount);
                    std::iota(sub.indices.begin(), sub.indices.end(), 0u);
                }

                result.bounds.expand(sub.bounds);
                result.submeshes.push_back(std::move(sub));
            }
        }

        for (int child : node.children) self(self, child);
    };

    for (int n : scene.nodes) processNode(processNode, n);

    if (result.empty()) return {};
    return result;
}

std::optional<build::EntityMesh>
MeshAssetCache::loadFromDisk(const std::filesystem::path& path) noexcept {
    const auto ext = path.extension().string();
    if (ext == ".gltf" || ext == ".glb")
        return loadGLTF(path);

    std::cerr << "[MeshAssetCache] Unsupported format: " << ext << '\n';
    return {};
}

const GPUEntityMesh* MeshAssetCache::load(const std::string& assetPath) noexcept {
    if (const auto it = cache_.find(assetPath); it != cache_.end())
        return &it->second;

    const auto path = resolve(assetPath);
    if (path.empty()) {
        std::cerr << "[MeshAssetCache] Not found: " << assetPath << '\n';
        return nullptr;
    }

    auto cpu = loadFromDisk(path);
    if (!cpu) return nullptr;

    auto gpu = GPUEntityMesh::upload(*cpu);
    std::cout << "[MeshAssetCache] Loaded " << path.filename().string()
              << " (" << gpu.submeshes.size() << " submesh(es))\n";

    cache_.emplace(assetPath, std::move(gpu));
    return &cache_.at(assetPath);
}

} // namespace forge::gfx
