#if 0 //pragma once

#include "erhe/physics/idebug_draw.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

namespace erhe::physics
{

class Jolt_debug_renderer final
    : public JPH::DebugRenderer
{
public:
    //static constexpr std::string_view c_name{"Jolt_debug_renderer"};
    //static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Jolt_debug_renderer();

    void set_debug_draw(IDebug_draw* debug_draw);

    //auto get_type_hash       () const -> uint32_t override { return hash; }
    //void connect             () override;
    //void initialize_component() override;

    /// Implementation of DebugRenderer interface
    void DrawLine           (const JPH::Float3& inFrom, const JPH::Float3& inTo, JPH::ColorArg inColor) override;
    void DrawTriangle       (JPH::Vec3Arg inV1, JPH::Vec3Arg inV2, JPH::Vec3Arg inV3, JPH::ColorArg inColor) override;
    auto CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) -> Batch override;
    auto CreateTriangleBatch(const Vertex* inVertices, int inVertexCount, const JPH::uint32* inIndices, int inIndexCount) -> Batch override;
    void DrawGeometry       (JPH::Mat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds, float inLODScaleSq, JPH::ColorArg inModelColor, const GeometryRef& inGeometry, ECullMode inCullMode, ECastShadow inCastShadow, EDrawMode inDrawMode) override;
    void DrawText3D         (JPH::Vec3Arg inPosition, const JPH::string& inString, JPH::ColorArg inColor, float inHeight) override;

private:
    IDebug_draw* m_debug_draw{nullptr};
};

} // namespace erhe::physics

#endif