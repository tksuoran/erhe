#pragma once

#include "erhe/raytrace/igeometry.hpp"

#include <glm/glm.hpp>

namespace erhe::raytrace
{

class Null_scene;

class Null_geometry
    : public IGeometry
{
public:
    explicit Null_geometry(const std::string_view debug_label);
    ~Null_geometry() override;

    // Implements IGeometry
    void commit                    () override {}
    void enable                    () override {}
    void disable                   () override {}
    void set_vertex_attribute_count(const unsigned int) override {}
    void set_buffer(
        const Buffer_type ,
        const unsigned int,
        const Format      ,
        const IBuffer*    ,
        const size_t      ,
        const size_t      ,
        const size_t
    ) override {}
    void set_user_data(void* ptr) override;
    [[nodiscard]] auto get_user_data() -> void* override;
    [[nodiscard]] auto debug_label() const -> std::string_view override;

private:
    glm::mat4   m_transform{1.0f};
    void*       m_user_data{nullptr};
    std::string m_debug_label;
};

}
