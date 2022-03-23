#pragma once

#include "erhe/raytrace/iinstance.hpp"

#include <embree3/rtcore.h>

#include <cstdint>
#include <string>

namespace erhe::raytrace
{

class Embree_scene;

class Embree_instance
    : public IInstance
{
public:
    explicit Embree_instance(const std::string_view debug_label); // rtcNewGeometry()
    ~Embree_instance() noexcept override; // rtcReleaseGeometry()

    // Implements IGeometry
    void commit       ()                          override;
    void enable       ()                          override;
    void disable      ()                          override;
    void set_transform(const glm::mat4 transform) override; // rtcSetGeometryTransform()
    void set_scene    (IScene* scene)             override;
    void set_mask     (const uint32_t mask)       override;
    void set_user_data(void* ptr)                 override;
    [[nodiscard]] auto get_transform() const -> glm::mat4        override;
    [[nodiscard]] auto get_scene    () const -> IScene*          override;
    [[nodiscard]] auto get_mask     () const -> uint32_t         override;
    [[nodiscard]] auto get_user_data() const -> void*            override;
    [[nodiscard]] auto is_enabled   () const -> bool             override;
    [[nodiscard]] auto debug_label  () const -> std::string_view override;

    auto get_rtc_geometry() -> RTCGeometry;

    // TODO This limits to one scene.
    unsigned int geometry_id{0};

    auto get_embree_scene() const -> Embree_scene*;

private:
    RTCGeometry   m_geometry {nullptr};
    Embree_scene* m_scene    {nullptr};
    void*         m_user_data{nullptr};
    bool          m_enabled  {true};
    uint32_t      m_mask     {0xffffffffu};
    std::string   m_debug_label;
};

}
