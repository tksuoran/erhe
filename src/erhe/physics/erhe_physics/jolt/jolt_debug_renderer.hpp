#pragma once

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include <glm/glm.hpp>

#include <atomic>

namespace erhe::physics {

class IDebug_draw;

// Adapts JPH::DebugRenderer to IDebug_draw: triangles and batched geometry
// are drawn as wireframe lines. Jolt allows one DebugRenderer per process
// (JPH::DebugRenderer::sInstance), so every Jolt_world shares the one
// returned by get_jolt_debug_renderer().
class Jolt_debug_renderer : public JPH::DebugRenderer
{
public:
    Jolt_debug_renderer();
    ~Jolt_debug_renderer() noexcept override;

    // Target and camera position (for LOD selection) of the following draws.
    void begin(IDebug_draw& debug_draw, glm::vec3 camera_position);
    void end  ();

    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
    void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow = ECastShadow::Off) override;
    void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor = JPH::Color::sWhite, float inHeight = 0.5f) override;
    auto CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) -> Batch override;
    auto CreateTriangleBatch(const Vertex* inVertices, int inVertexCount, const JPH::uint32* inIndices, int inIndexCount) -> Batch override;
    void DrawGeometry(
        JPH::RMat44Arg     inModelMatrix,
        const JPH::AABox&  inWorldSpaceBounds,
        float              inLODScaleSq,
        JPH::ColorArg      inModelColor,
        const GeometryRef& inGeometry,
        ECullMode          inCullMode   = ECullMode::CullBackFace,
        ECastShadow        inCastShadow = ECastShadow::On,
        EDrawMode          inDrawMode   = EDrawMode::Solid
    ) override;

private:
    class Batch_impl : public JPH::RefTargetVirtual
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        void AddRef () override { ++m_ref_count; }
        void Release() override { if (--m_ref_count == 0) delete this; }

        JPH::Array<Triangle> m_triangles;

    private:
        std::atomic<uint32_t> m_ref_count{0};
    };

    IDebug_draw* m_debug_draw{nullptr};
    JPH::Vec3    m_camera_position{JPH::Vec3::sZero()};
};

[[nodiscard]] auto get_jolt_debug_renderer() -> Jolt_debug_renderer&;

} // namespace erhe::physics

#endif
