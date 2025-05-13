#pragma once

#include "erhe_raytrace/igeometry.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::raytrace {

class Null_scene;

class Null_geometry
    : public IGeometry
{
public:
    Null_geometry(const std::string_view debug_label, const Geometry_type geometry_type);
    ~Null_geometry() noexcept override;

    // Implements IGeometry
    void commit                    () override {}
    void enable                    () override { m_enabled = true; }
    void disable                   () override { m_enabled = false; }
    void set_mask                  (const uint32_t mask) override { m_mask = mask; }
    void set_vertex_attribute_count(const unsigned int) override {}
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
    auto get_mask     () const -> uint32_t         override { return m_mask; }
    auto get_user_data() const -> const void*      override { return nullptr; }
    auto is_enabled   () const -> bool             override { return m_enabled; }
    auto debug_label  () const -> std::string_view override { return m_debug_label; }

private:
    glm::mat4   m_transform{1.0f};
    void*       m_user_data{nullptr};
    bool        m_enabled  {true};
    uint32_t    m_mask     {0xffffffffu};
    std::string m_debug_label;
};

}
