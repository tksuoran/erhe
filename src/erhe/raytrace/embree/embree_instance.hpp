#pragma once

#include "erhe/raytrace/iinstance.hpp"

#include <embree3/rtcore.h>

#include <string>

namespace erhe::raytrace
{

class Embree_scene;

class Embree_instance
    : public IInstance
{
public:
    explicit Embree_instance(const std::string_view debug_label); // rtcNewGeometry()
    ~Embree_instance() override; // rtcReleaseGeometry()

    // Implements IGeometry
    void commit       () override;
    void set_transform(const glm::mat4 transform) override; // rtcSetGeometryTransform()
    void set_scene    (IScene* scene) override;
    void set_user_data(void* ptr) override;
    [[nodiscard]] auto get_user_data() -> void* override;
    [[nodiscard]] auto debug_label() const -> std::string_view override;

    auto get_rtc_geometry() -> RTCGeometry;

    // TODO This limits to one scene.
    unsigned int geometry_id{0};

    auto get_embree_scene() const -> Embree_scene*;

private:
    RTCGeometry   m_geometry {nullptr};
    Embree_scene* m_scene    {nullptr};
    void*         m_user_data{nullptr};
    std::string   m_debug_label;
};

}
