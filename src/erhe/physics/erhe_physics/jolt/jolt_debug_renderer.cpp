#ifdef JPH_DEBUG_RENDERER

#include "erhe_physics/jolt/jolt_debug_renderer.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"
#include "erhe_physics/idebug_draw.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::physics {

namespace {

[[nodiscard]] auto to_glm(const JPH::RVec3Arg v) -> glm::vec3
{
    return glm::vec3{static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}

[[nodiscard]] auto to_glm(const JPH::Color color) -> glm::vec4
{
    return glm::vec4{
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f
    };
}

} // anonymous namespace

Jolt_debug_renderer::Jolt_debug_renderer()
{
    JPH::DebugRenderer::Initialize();
}

Jolt_debug_renderer::~Jolt_debug_renderer() noexcept = default;

void Jolt_debug_renderer::begin(IDebug_draw& debug_draw, const glm::vec3 camera_position)
{
    ERHE_VERIFY(m_debug_draw == nullptr);
    m_debug_draw      = &debug_draw;
    m_camera_position = to_jolt(camera_position);
}

void Jolt_debug_renderer::end()
{
    m_debug_draw = nullptr;
}

void Jolt_debug_renderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
{
    ERHE_VERIFY(m_debug_draw != nullptr);
    m_debug_draw->draw_line(to_glm(inFrom), to_glm(inTo), to_glm(inColor));
}

void Jolt_debug_renderer::DrawTriangle(
    JPH::RVec3Arg inV1,
    JPH::RVec3Arg inV2,
    JPH::RVec3Arg inV3,
    JPH::ColorArg inColor,
    ECastShadow   inCastShadow
)
{
    static_cast<void>(inCastShadow);
    DrawLine(inV1, inV2, inColor);
    DrawLine(inV2, inV3, inColor);
    DrawLine(inV3, inV1, inColor);
}

void Jolt_debug_renderer::DrawText3D(
    JPH::RVec3Arg           inPosition,
    const std::string_view& inString,
    JPH::ColorArg           inColor,
    float                   inHeight
)
{
    static_cast<void>(inPosition);
    static_cast<void>(inString);
    static_cast<void>(inColor);
    static_cast<void>(inHeight);
}

auto Jolt_debug_renderer::CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) -> Batch
{
    Batch_impl* batch = new Batch_impl;
    if ((inTriangles == nullptr) || (inTriangleCount == 0)) {
        return batch;
    }
    batch->m_triangles.assign(inTriangles, inTriangles + inTriangleCount);
    return batch;
}

auto Jolt_debug_renderer::CreateTriangleBatch(
    const Vertex*      inVertices,
    int                inVertexCount,
    const JPH::uint32* inIndices,
    int                inIndexCount
) -> Batch
{
    Batch_impl* batch = new Batch_impl;
    if ((inVertices == nullptr) || (inVertexCount == 0) || (inIndices == nullptr) || (inIndexCount == 0)) {
        return batch;
    }

    // Indexed triangle list to triangle list
    batch->m_triangles.resize(inIndexCount / 3);
    for (std::size_t t = 0; t < batch->m_triangles.size(); ++t) {
        Triangle& triangle = batch->m_triangles[t];
        triangle.mV[0] = inVertices[inIndices[t * 3 + 0]];
        triangle.mV[1] = inVertices[inIndices[t * 3 + 1]];
        triangle.mV[2] = inVertices[inIndices[t * 3 + 2]];
    }
    return batch;
}

void Jolt_debug_renderer::DrawGeometry(
    JPH::RMat44Arg     inModelMatrix,
    const JPH::AABox&  inWorldSpaceBounds,
    float              inLODScaleSq,
    JPH::ColorArg      inModelColor,
    const GeometryRef& inGeometry,
    ECullMode          inCullMode,
    ECastShadow        inCastShadow,
    EDrawMode          inDrawMode
)
{
    static_cast<void>(inCullMode);
    static_cast<void>(inDrawMode);
    const LOD&        lod   = inGeometry->GetLOD(m_camera_position, inWorldSpaceBounds, inLODScaleSq);
    const Batch_impl* batch = static_cast<const Batch_impl*>(lod.mTriangleBatch.GetPtr());
    for (const Triangle& triangle : batch->m_triangles) {
        const JPH::RVec3 v0    = inModelMatrix * JPH::Vec3{triangle.mV[0].mPosition};
        const JPH::RVec3 v1    = inModelMatrix * JPH::Vec3{triangle.mV[1].mPosition};
        const JPH::RVec3 v2    = inModelMatrix * JPH::Vec3{triangle.mV[2].mPosition};
        const JPH::Color color = inModelColor * triangle.mV[0].mColor;
        DrawTriangle(v0, v1, v2, color, inCastShadow);
    }
}

auto get_jolt_debug_renderer() -> Jolt_debug_renderer&
{
    static Jolt_debug_renderer jolt_debug_renderer;
    return jolt_debug_renderer;
}

} // namespace erhe::physics

#endif
