#pragma once

#include "erhe_raytrace/igeometry.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::raytrace
{

class Null_scene;

class Null_geometry
    : public IGeometry
{
public:
    explicit Null_geometry(
        const std::string_view debug_label,
        const Geometry_type    geometry_type
    );
    ~Null_geometry() noexcept override;

    // Implements IGeometry
    void commit                    () override {}
    void enable                    () override { m_enabled = true; }
    void disable                   () override { m_enabled = false; }
    void set_mask                  (const uint32_t mask) override { m_mask = mask; }
    void set_vertex_attribute_count(const unsigned int) override {}
    void set_buffer(
        const Buffer_type ,
        const unsigned int,
        const Format      ,
        IBuffer* const    ,
        const std::size_t ,
        const std::size_t ,
        const std::size_t
    ) override {}
    void set_user_data(void* ptr) override { m_user_data = ptr; }
    [[nodiscard]] auto get_mask     () const -> uint32_t         override { return m_mask; }
    [[nodiscard]] auto get_user_data() const -> void*            override { return m_user_data; }
    [[nodiscard]] auto is_enabled   () const -> bool             override { return m_enabled; }
    [[nodiscard]] auto debug_label  () const -> std::string_view override { return m_debug_label; }

private:
    glm::mat4   m_transform{1.0f};
    void*       m_user_data{nullptr};
    bool        m_enabled  {true};
    uint32_t    m_mask     {0xffffffffu};
    std::string m_debug_label;
};

}
