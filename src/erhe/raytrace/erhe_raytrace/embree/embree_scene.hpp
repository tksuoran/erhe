#pragma once

#include "erhe_raytrace/iscene.hpp"

#include <embree3/rtcore.h>

#include <vector>

namespace erhe::raytrace
{

class Embree_geometry;
class IGeometry;
class IInstance;
class Ray;
class Hit;

class Embree_scene
    : public IScene
{
public:
    explicit Embree_scene(const std::string_view debug_label); // rtcNewScene()
    ~Embree_scene() noexcept override; // rtcReleaseScene()

    // rtcGetSceneDevice()
    // rtcRetainScene()

    // Implements IScene
    [[nodiscard]] auto debug_label() const -> std::string_view override;
    void attach(IGeometry* geometry) override; // rtcAttachGeometry()
    void attach(IInstance* scene) override; // rtcAttachGeometry()

    // rtcAttachGeometryByID()
    // rtcGetGeometry()
    // rtcGetGeometryThreadSafe()

    void detach(IGeometry* geometry) override; // rtcDetachGeometry()
    void detach(IInstance* instance) override; // rtcDetachGeometry()
    void commit()                    override; // rtcCommitScene()

    // rtcJoinCommitScene()
    // rtcSetSceneProgressMonitorFunction()
    // rtcSetSceneBuildQuality()
    // rtcSetSceneFlags()
    // rtcGetSceneFlags()
    // rtcGetSceneBounds()
    // rtcGetSceneLinearBounds()

    void intersect(Ray& ray, Hit& out_hit) override;

    //void set_dirty();
    auto get_rtc_scene() -> RTCScene;
    auto get_geometry_from_id(const unsigned int id) -> Embree_geometry*;

private:
    RTCScene    m_scene{nullptr};
    std::string m_debug_label;
    //bool        m_dirty{true};
};

}
