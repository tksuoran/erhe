#pragma once

#include "erhe_raytrace/igeometry.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <embree4/rtcore.h>

#include <string>

namespace erhe::buffer {
    class Cpu_buffer;
}

namespace erhe::raytrace
{

class Embree_geometry
    : public IGeometry
{
public:
    explicit Embree_geometry(
        std::string_view debug_label,
        Geometry_type    geometry_type
    );
    ~Embree_geometry() noexcept override;

    // Implements IGeometry
    void commit                    () override;
    void enable                    () override;
    void disable                   () override;
    void set_mask                  (uint32_t mask) override;
    void set_vertex_attribute_count(unsigned int count) override;
    void set_buffer(
        Buffer_type               type,
        unsigned int              slot,
        erhe::dataformat::Format  format,
        erhe::buffer::Cpu_buffer* buffer,
        std::size_t               byte_offset,
        std::size_t               byte_stride,
        std::size_t               item_count
    ) override;
    void set_user_data(const void* ptr) override;
    [[nodiscard]] auto get_mask     () const -> uint32_t         override;
    [[nodiscard]] auto get_user_data() const -> const void*      override;
    [[nodiscard]] auto is_enabled   () const -> bool             override;
    [[nodiscard]] auto debug_label  () const -> std::string_view override;

    auto get_rtc_geometry() -> RTCGeometry;

    // TODO This limits to one scene.
    unsigned int geometry_id{0};

private:
    RTCGeometry  m_geometry {nullptr};
    const void*  m_user_data{nullptr};
    std::string  m_debug_label;
    bool         m_enabled  {true};
    uint32_t     m_mask     {0xffffffffu};
};

}
