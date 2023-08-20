#if 0
#include "erhe_physics/jolt/jolt_debug_renderer.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"

namespace erhe::physics
{

Jolt_debug_renderer::Jolt_debug_renderer()
{
    DebugRenderer::Initialize();
}

void Jolt_debug_renderer::set_debug_draw(IDebug_draw* debug_draw)
{
    m_debug_draw = debug_draw;
}

#if 0
Jolt_debug_renderer::~Jolt_debug_renderer()
{
}

void Jolt_debug_renderer::connect()
{
    //require<Gl_context_provider>();

    //m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Jolt_debug_renderer::initialize_component()
{
}
#endif

void Jolt_debug_renderer::DrawLine(
    const JPH::Float3& inFrom,
    const JPH::Float3& inTo,
    JPH::ColorArg      inColor
)
{
    if (m_debug_draw == nullptr) {
        return;
    }
    m_debug_draw->draw_line(
        from_jolt(inFrom),
        from_jolt(inTo),
        from_jolt(inColor)
    );
}

void Jolt_debug_renderer::DrawTriangle(
    JPH::Vec3Arg  inV1,
    JPH::Vec3Arg  inV2,
    JPH::Vec3Arg  inV3,
    JPH::ColorArg inColor
)
{
    if (m_debug_draw == nullptr) {
        return;
    }
    m_debug_draw->draw_line(from_jolt(inV1), from_jolt(inV2), from_jolt(inColor));
    m_debug_draw->draw_line(from_jolt(inV2), from_jolt(inV3), from_jolt(inColor));
    m_debug_draw->draw_line(from_jolt(inV3), from_jolt(inV1), from_jolt(inColor));
}

auto Jolt_debug_renderer::CreateTriangleBatch(
    const Triangle* inTriangles,
    int             inTriangleCount
) -> Batch
{
    static_cast<void>(inTriangles);
    static_cast<void>(inTriangleCount);
    //if (m_debug_draw == nullptr) {
    //    return;
    //}
    //
    //for (int i = 0; i < inTriangleCount; ++i) {
    //    const Triangle& t = inTriangles[i];
    //    m_debug_draw->draw_line(from_jolt(t.mV[0].mPosition), from_jolt(t.mV[1].mPosition), from_jolt(t.mV[0].mColor));
    //    m_debug_draw->draw_line(from_jolt(t.mV[1].mPosition), from_jolt(t.mV[2].mPosition), from_jolt(t.mV[1].mColor));
    //    m_debug_draw->draw_line(from_jolt(t.mV[2].mPosition), from_jolt(t.mV[0].mPosition), from_jolt(t.mV[2].mColor));
    //}
    //
    return {};
}

auto Jolt_debug_renderer::CreateTriangleBatch(
    const Vertex*      inVertices,
    int                inVertexCount,
    const JPH::uint32* inIndices,
    int                inIndexCount
) -> Batch
{
    static_cast<void>(inVertices);
    static_cast<void>(inVertexCount);
    static_cast<void>(inIndices);
    static_cast<void>(inIndexCount);
    return {};
}

void Jolt_debug_renderer::DrawGeometry(
    JPH::Mat44Arg      inModelMatrix,
    const JPH::AABox&  inWorldSpaceBounds,
    float              inLODScaleSq,
    JPH::ColorArg      inModelColor,
    const GeometryRef& inGeometry,
    ECullMode          inCullMode,
    ECastShadow        inCastShadow,
    EDrawMode          inDrawMode
)
{
    static_cast<void>(inModelMatrix);
    static_cast<void>(inWorldSpaceBounds);
    static_cast<void>(inLODScaleSq);
    static_cast<void>(inModelColor);
    static_cast<void>(inGeometry);
    static_cast<void>(inCullMode);
    static_cast<void>(inCastShadow);
    static_cast<void>(inDrawMode);
}

void Jolt_debug_renderer::DrawText3D(
    JPH::Vec3Arg       inPosition,
    const JPH::string& inString,
    JPH::ColorArg      inColor,
    float              inHeight
)
{
    if (m_debug_draw == nullptr) {
        return;
    }
    const auto p = from_jolt(inPosition);
    m_debug_draw->draw_3d_text(p, inString.c_str());
}

} // namespace erhe::physics
#endif
