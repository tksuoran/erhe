#pragma once

#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include <atomic>

namespace erhe::renderer {

class Debug_renderer;

// TODO rename Physics_debug_renderer
class Jolt_debug_renderer : public JPH::DebugRenderer
{
public:
    explicit Jolt_debug_renderer(Debug_renderer& debug_renderer);
    ~Jolt_debug_renderer() override;

    /// Should be called every frame by the application to provide the camera position.
    /// This is used to determine the correct LOD for rendering.
    void SetCameraPos(JPH::RVec3Arg inCameraPos)
    {
        mCameraPos = inCameraPos;
        mCameraPosSet = true;
    }

    /// Draw line
    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;

    /// Draw a single back face culled triangle
    void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow = ECastShadow::Off) override;

    /// Draw text
    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor = JPH::Color::sWhite, float inHeight = 0.5f) override;

    /// Create a batch of triangles that can be drawn efficiently
    Batch CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override;
    Batch CreateTriangleBatch(const Vertex* inVertices, int inVertexCount, const JPH::uint32* inIndices, int inIndexCount) override;

    void DrawGeometry(
        JPH::RMat44Arg      inModelMatrix,
        const JPH::AABox&   inWorldSpaceBounds,
        float               inLODScaleSq,
        JPH::ColorArg       inModelColor,
        const GeometryRef&  inGeometry,
        ECullMode           inCullMode   = ECullMode::CullBackFace,
        ECastShadow         inCastShadow = ECastShadow::On,
        EDrawMode           inDrawMode   = EDrawMode::Solid
    ) override;

private:
    Debug_renderer& m_debug_renderer;

    /// Implementation specific batch object
    class BatchImpl : public JPH::RefTargetVirtual
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        void AddRef() override { ++mRefCount; }
        void Release() override { if (--mRefCount == 0) delete this; }

        JPH::Array<Triangle> mTriangles;

    private:
        std::atomic<uint32_t> mRefCount = 0;
    };

    JPH::RVec3 mCameraPos;
    bool       mCameraPosSet = false;
};

} // namespace erhe::renderer
#endif
