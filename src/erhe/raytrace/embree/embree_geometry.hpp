#pragma once

#include "erhe/raytrace/igeometry.hpp"

#include <embree3/rtcore.h>

#include <string>

namespace erhe::raytrace
{

class Embree_geometry
    : public IGeometry
{
public:
    explicit Embree_geometry(
        const std::string_view debug_label,
        const Geometry_type    geometry_type
    );
    ~Embree_geometry() override;

    // Implements IGeometry
    void commit () override;
    void enable () override;
    void disable() override;
    void set_user_data(void* ptr) override;
    [[nodiscard]] auto get_user_data() -> void* override;
    [[nodiscard]] auto debug_label() const -> std::string_view override;

    void set_vertex_attribute_count(const unsigned int count) override;

    void set_buffer(
        const Buffer_type  type,
        const unsigned int slot,
        const Format       format,
        const IBuffer*     buffer,
        const size_t       byte_offset,
        const size_t       byte_stride,
        const size_t       item_count
    ) override;

    auto get_rtc_geometry() -> RTCGeometry;

    // TODO This limits to one scene.
    unsigned int geometry_id{0};

private:
    RTCGeometry m_geometry {nullptr};
    void*       m_user_data{nullptr};
    std::string m_debug_label;
};

}
