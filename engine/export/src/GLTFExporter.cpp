#include "forge/export/GLTFExporter.hpp"
#include <forge/build.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <format>
#include <map>
#include <cstring>

namespace forge::export_ {

// ─── Colour from material name hash ──────────────────────────────────────────

static std::array<double, 4> materialBaseColour(const std::string& id) {
    static const std::array<std::array<double,3>, 8> pal = {{
        {0.72,0.70,0.65}, {0.55,0.50,0.45}, {0.80,0.75,0.60}, {0.40,0.45,0.55},
        {0.65,0.58,0.50}, {0.50,0.60,0.50}, {0.60,0.50,0.55}, {0.45,0.55,0.65},
    }};
    const auto& c = pal[std::hash<std::string>{}(id) % pal.size()];
    return { c[0], c[1], c[2], 1.0 };
}

// ─── Buffer helpers ───────────────────────────────────────────────────────────

static int addBufferView(tinygltf::Model& m, const void* data,
                          std::size_t byteLength, int target) {
    const auto& existing = m.buffers[0].data;
    const std::size_t offset = existing.size();

    // Append data to buffer 0
    m.buffers[0].data.resize(offset + byteLength);
    std::memcpy(m.buffers[0].data.data() + offset, data, byteLength);

    tinygltf::BufferView bv;
    bv.buffer     = 0;
    bv.byteOffset = static_cast<int>(offset);
    bv.byteLength = static_cast<int>(byteLength);
    bv.target     = target;
    m.bufferViews.push_back(bv);
    return static_cast<int>(m.bufferViews.size() - 1);
}

static int addAccessor(tinygltf::Model& m, int bufView,
                        int componentType, int type, std::size_t count,
                        const std::vector<double>& mins,
                        const std::vector<double>& maxs) {
    tinygltf::Accessor acc;
    acc.bufferView    = bufView;
    acc.byteOffset    = 0;
    acc.componentType = componentType;
    acc.type          = type;
    acc.count         = static_cast<int>(count);
    acc.minValues     = mins;
    acc.maxValues     = maxs;
    m.accessors.push_back(acc);
    return static_cast<int>(m.accessors.size() - 1);
}

// ─── exportGLTF ──────────────────────────────────────────────────────────────

std::string exportGLTF(
    const scene::Scene&          scene,
    const std::filesystem::path& outputPath,
    const GLTFExportOptions&     opts) noexcept
{
    try {
        tinygltf::Model model;
        tinygltf::TinyGLTF writer;

        model.asset.version   = "2.0";
        model.asset.generator = "FORGE v0.1.0";

        // Single shared buffer (all meshes packed sequentially)
        model.buffers.emplace_back();
        model.buffers[0].name = "forge_geo";

        // Root scene node
        tinygltf::Scene gltfScene;
        gltfScene.name = scene.name;

        // Material cache: materialId → GLTF material index
        std::map<std::string, int> matCache;
        auto getMaterial = [&](const std::string& id) -> int {
            auto it = matCache.find(id);
            if (it != matCache.end()) return it->second;

            tinygltf::Material mat;
            mat.name                                         = id;
            mat.pbrMetallicRoughness.baseColorFactor        = materialBaseColour(id);
            mat.pbrMetallicRoughness.metallicFactor         = 0.0;
            mat.pbrMetallicRoughness.roughnessFactor        = 0.8;
            mat.doubleSided                                  = false;

            const int idx = static_cast<int>(model.materials.size());
            model.materials.push_back(mat);
            matCache[id] = idx;
            return idx;
        };

        // ── BrushEntities ─────────────────────────────────────────────────────
        for (const auto& [eid, entity] : scene.entities) {
            const auto* be = std::get_if<scene::BrushEntity>(&entity);
            if (!be || !be->visible) continue;

            const auto cpuMesh = build::buildEntityMesh(*be, opts.applyTransforms);
            if (cpuMesh.empty()) continue;

            tinygltf::Mesh gMesh;
            gMesh.name = be->name;

            for (const auto& sub : cpuMesh.submeshes) {
                if (sub.empty()) continue;

                // ── Vertex buffer ────────────────────────────────────────────
                // Pack as: [pos(3f) | normal(3f) | uv(2f)] = 32 bytes/vertex
                const int vbView = addBufferView(model,
                    sub.vertices.data(),
                    sub.vertices.size() * sizeof(build::Vertex),
                    TINYGLTF_TARGET_ARRAY_BUFFER);

                // Compute bounds for position accessor
                glm::vec3 posMin{ 1e30f}, posMax{-1e30f};
                for (const auto& v : sub.vertices) {
                    posMin = glm::min(posMin, v.position);
                    posMax = glm::max(posMax, v.position);
                }

                // Accessors — byte offsets within vbView
                tinygltf::Accessor posAcc, normAcc, uvAcc;

                posAcc.bufferView    = vbView;
                posAcc.byteOffset    = offsetof(build::Vertex, position);
                posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
                posAcc.type          = TINYGLTF_TYPE_VEC3;
                posAcc.count         = static_cast<int>(sub.vertexCount());
                posAcc.minValues     = {posMin.x, posMin.y, posMin.z};
                posAcc.maxValues     = {posMax.x, posMax.y, posMax.z};

                normAcc.bufferView    = vbView;
                normAcc.byteOffset    = offsetof(build::Vertex, normal);
                normAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
                normAcc.type          = TINYGLTF_TYPE_VEC3;
                normAcc.count         = static_cast<int>(sub.vertexCount());

                uvAcc.bufferView    = vbView;
                uvAcc.byteOffset    = offsetof(build::Vertex, uv);
                uvAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
                uvAcc.type          = TINYGLTF_TYPE_VEC2;
                uvAcc.count         = static_cast<int>(sub.vertexCount());

                // Set interleaved stride on the buffer view
                model.bufferViews.back().byteStride = sizeof(build::Vertex);

                const int iPos  = static_cast<int>(model.accessors.size()); model.accessors.push_back(posAcc);
                const int iNorm = static_cast<int>(model.accessors.size()); model.accessors.push_back(normAcc);
                const int iUV   = static_cast<int>(model.accessors.size()); model.accessors.push_back(uvAcc);

                // ── Index buffer ─────────────────────────────────────────────
                const int ibView = addBufferView(model,
                    sub.indices.data(),
                    sub.indices.size() * sizeof(uint32_t),
                    TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);

                tinygltf::Accessor idxAcc;
                idxAcc.bufferView    = ibView;
                idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
                idxAcc.type          = TINYGLTF_TYPE_SCALAR;
                idxAcc.count         = static_cast<int>(sub.indices.size());
                const int iIdx = static_cast<int>(model.accessors.size());
                model.accessors.push_back(idxAcc);

                // ── Primitive ─────────────────────────────────────────────────
                tinygltf::Primitive prim;
                prim.attributes["POSITION"]   = iPos;
                prim.attributes["NORMAL"]     = iNorm;
                prim.attributes["TEXCOORD_0"] = iUV;
                prim.indices                  = iIdx;
                prim.mode                     = TINYGLTF_MODE_TRIANGLES;
                prim.material                 = getMaterial(sub.materialId());

                gMesh.primitives.push_back(prim);
            }

            // GLTF mesh + node
            const int meshIdx = static_cast<int>(model.meshes.size());
            model.meshes.push_back(gMesh);

            tinygltf::Node node;
            node.name = be->name;
            node.mesh = meshIdx;

            // Entity transform → GLTF TRS (if not pre-baked)
            if (!opts.applyTransforms) {
                const glm::dvec3& t = be->transform.translation;
                const glm::dquat& r = be->transform.rotation;
                const glm::dvec3& s = be->transform.scale;
                node.translation = { t.x * opts.scale, t.y * opts.scale, t.z * opts.scale };
                node.rotation    = { r.x, r.y, r.z, r.w };
                node.scale       = { s.x, s.y, s.z };
            }

            gltfScene.nodes.push_back(static_cast<int>(model.nodes.size()));
            model.nodes.push_back(node);
        }

        // ── PointEntities → empty nodes with extras ────────────────────────
        for (const auto& [eid, entity] : scene.entities) {
            const auto* pe = std::get_if<scene::PointEntity>(&entity);
            if (!pe) continue;

            tinygltf::Node node;
            node.name = pe->name;
            const glm::dvec3& t = pe->transform.translation;
            node.translation = { t.x * opts.scale, t.y * opts.scale, t.z * opts.scale };

            // Store classname + properties in GLTF extras
            node.extras = tinygltf::Value(tinygltf::Value::Object{
                { "classname", tinygltf::Value(pe->classname) }
            });

            gltfScene.nodes.push_back(static_cast<int>(model.nodes.size()));
            model.nodes.push_back(node);
        }

        model.scenes.push_back(gltfScene);
        model.defaultScene = 0;

        // ── Write ─────────────────────────────────────────────────────────────
        const bool binary = opts.binary;
        std::filesystem::path out = outputPath;
        out.replace_extension(binary ? ".glb" : ".gltf");

        bool ok;
        std::string err, warn;

        if (binary) {
            ok = writer.WriteGltfSceneToFile(&model, out.string(),
                /*embedImages=*/true, /*embedBuffers=*/true,
                /*prettyPrint=*/false, /*writeBinary=*/true);
        } else {
            ok = writer.WriteGltfSceneToFile(&model, out.string(),
                /*embedImages=*/false, /*embedBuffers=*/false,
                /*prettyPrint=*/true,  /*writeBinary=*/false);
        }

        if (!ok) return std::format("tinygltf write failed for '{}'", out.string());
        return {};

    } catch (const std::exception& ex) {
        return std::format("GLTF export error: {}", ex.what());
    }
}

} // namespace forge::export_
