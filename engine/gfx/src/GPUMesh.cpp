#include "forge/gfx/GPUMesh.hpp"

#include <GL/glew.h>

namespace forge::gfx {

// ─── GPUMesh ─────────────────────────────────────────────────────────────────

void GPUMesh::release() noexcept {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_);      ebo_ = 0; }
    indexCount_ = 0;
}

GPUMesh::~GPUMesh() { release(); }

GPUMesh::GPUMesh(GPUMesh&& o) noexcept
    : vao_(o.vao_), vbo_(o.vbo_), ebo_(o.ebo_)
    , indexCount_(o.indexCount_)
    , materialId_(std::move(o.materialId_))
    , bounds_(o.bounds_)
{
    o.vao_ = o.vbo_ = o.ebo_ = 0;
    o.indexCount_ = 0;
}

GPUMesh& GPUMesh::operator=(GPUMesh&& o) noexcept {
    if (this != &o) {
        release();
        vao_        = o.vao_;
        vbo_        = o.vbo_;
        ebo_        = o.ebo_;
        indexCount_ = o.indexCount_;
        materialId_ = std::move(o.materialId_);
        bounds_     = o.bounds_;
        o.vao_ = o.vbo_ = o.ebo_ = 0;
        o.indexCount_ = 0;
    }
    return *this;
}

GPUMesh GPUMesh::upload(const build::MeshData& data) noexcept {
    if (data.empty()) return {};

    GPUMesh mesh;
    mesh.materialId_ = data.materialId;
    mesh.bounds_     = data.bounds;
    mesh.indexCount_ = static_cast<uint32_t>(data.indices.size());

    glGenVertexArrays(1, &mesh.vao_);
    glGenBuffers(1,     &mesh.vbo_);
    glGenBuffers(1,     &mesh.ebo_);

    glBindVertexArray(mesh.vao_);

    // Upload vertices
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.vertices.size() * sizeof(build::Vertex)),
        data.vertices.data(),
        GL_STATIC_DRAW);

    // Upload indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
        data.indices.data(),
        GL_STATIC_DRAW);

    // Vertex attribute layout (must match build::Vertex and brush.vert)
    using V = build::Vertex;

    // location 0: position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(V), reinterpret_cast<void*>(offsetof(V, position)));

    // location 1: normal (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        sizeof(V), reinterpret_cast<void*>(offsetof(V, normal)));

    // location 2: uv (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
        sizeof(V), reinterpret_cast<void*>(offsetof(V, uv)));

    glBindVertexArray(0);
    return mesh;
}

void GPUMesh::draw() const noexcept {
    if (!valid()) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES,
        static_cast<GLsizei>(indexCount_),
        GL_UNSIGNED_INT,
        nullptr);
    glBindVertexArray(0);
}

// ─── GPUEntityMesh ───────────────────────────────────────────────────────────

GPUEntityMesh GPUEntityMesh::upload(const build::EntityMesh& data) noexcept {
    GPUEntityMesh result;
    result.bounds = data.bounds;
    result.submeshes.reserve(data.submeshes.size());
    for (const auto& sub : data.submeshes)
        result.submeshes.push_back(GPUMesh::upload(sub));
    return result;
}

} // namespace forge::gfx
