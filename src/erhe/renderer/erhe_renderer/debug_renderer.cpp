#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"

namespace erhe::renderer {

Debug_renderer::Debug_renderer(Line_renderer& line_renderer)
    : JPH::DebugRenderer{}
    , m_line_renderer{line_renderer}
{
    JPH::DebugRenderer::Initialize();
}

Debug_renderer::~Debug_renderer()
{
}

void Debug_renderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
{
    Scoped_line_renderer r = m_line_renderer.get(2, true, true);
    glm::vec4 color{inColor.r / 255.0f};
    float     width{1.0f};
    glm::vec3 p0{inFrom.GetX(), inFrom.GetY(), inFrom.GetZ()};
    glm::vec3 p1{inTo  .GetX(), inTo  .GetY(), inTo  .GetZ()};
    r.add_line(color, width, p0, color, width, p1);
}

/// Draw a single back face culled triangle
void Debug_renderer::DrawTriangle(
    JPH::RVec3Arg inV1,
    JPH::RVec3Arg inV2,
    JPH::RVec3Arg inV3,
    JPH::ColorArg inColor,
    ECastShadow   inCastShadow
)
{
    Scoped_line_renderer r = m_line_renderer.get(2, true, true);
    glm::vec4 color{inColor.r / 255.0f};
    float     width{1.0f};
    glm::vec3 p0{inV1.GetX(), inV1.GetY(), inV1.GetZ()};
    glm::vec3 p1{inV2.GetX(), inV2.GetY(), inV2.GetZ()};
    glm::vec3 p2{inV3.GetX(), inV3.GetY(), inV3.GetZ()};
    r.add_line(color, width, p0, color, width, p1);
    r.add_line(color, width, p1, color, width, p2);
    r.add_line(color, width, p2, color, width, p0);
    static_cast<void>(inCastShadow);
}

/// Draw text
void Debug_renderer::DrawText3D(
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

/// Create a batch of triangles that can be drawn efficiently
JPH::DebugRenderer::Batch Debug_renderer::CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount)
{
    BatchImpl* batch = new BatchImpl;
    if (inTriangles == nullptr || inTriangleCount == 0) {
        return batch;
    }

    batch->mTriangles.assign(inTriangles, inTriangles + inTriangleCount);
    return batch;
}

JPH::DebugRenderer::Batch Debug_renderer::CreateTriangleBatch(const Vertex* inVertices, int inVertexCount, const JPH::uint32* inIndices, int inIndexCount)
{
    BatchImpl* batch = new BatchImpl;
    if (inVertices == nullptr || inVertexCount == 0 || inIndices == nullptr || inIndexCount == 0) {
        return batch;
    }

    // Convert indexed triangle list to triangle list
    batch->mTriangles.resize(inIndexCount / 3);
    for (size_t t = 0; t < batch->mTriangles.size(); ++t) {
        Triangle &triangle = batch->mTriangles[t];
        triangle.mV[0] = inVertices[inIndices[t * 3 + 0]];
        triangle.mV[1] = inVertices[inIndices[t * 3 + 1]];
        triangle.mV[2] = inVertices[inIndices[t * 3 + 2]];
    }

    return batch;
}

void Debug_renderer::DrawGeometry(
    JPH::RMat44Arg      inModelMatrix,
    const JPH::AABox&   inWorldSpaceBounds,
    float               inLODScaleSq,
    JPH::ColorArg       inModelColor,
    const GeometryRef&  inGeometry,
    ECullMode           inCullMode,
    ECastShadow         inCastShadow,
    EDrawMode           inDrawMode
)
{
    static_cast<void>(inCullMode);
    static_cast<void>(inCastShadow);
    // Figure out which LOD to use
    const LOD* lod = inGeometry->mLODs.data();
    if (mCameraPosSet) {
        lod = &inGeometry->GetLOD(JPH::Vec3{mCameraPos}, inWorldSpaceBounds, inLODScaleSq);
    }

    // Draw the batch
    const BatchImpl* batch = static_cast<const BatchImpl*>(lod->mTriangleBatch.GetPtr());
    for (const Triangle& triangle : batch->mTriangles) {
        JPH::RVec3 v0    = inModelMatrix * JPH::Vec3{triangle.mV[0].mPosition};
        JPH::RVec3 v1    = inModelMatrix * JPH::Vec3{triangle.mV[1].mPosition};
        JPH::RVec3 v2    = inModelMatrix * JPH::Vec3{triangle.mV[2].mPosition};
        JPH::Color color = inModelColor * triangle.mV[0].mColor;

        switch (inDrawMode) {
            case EDrawMode::Wireframe: {
                DrawLine(v0, v1, color);
                DrawLine(v1, v2, color);
                DrawLine(v2, v0, color);
                break;
            }

            case EDrawMode::Solid: {
                DrawTriangle(v0, v1, v2, color, inCastShadow);
                break;
            }
        }
    }
}

} // namespace erhe::renderer
