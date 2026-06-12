#pragma once

#include "create/create_shape.hpp"

#include <glm/glm.hpp>

namespace editor {

class Torus_parameters
{
public:
    float major_radius{1.0f};
    float minor_radius{0.5f};
    int   major_steps {32};
    int   minor_steps {28};
};

class Create_torus : public Create_shape
{
public:
    ~Create_torus() noexcept override;

    void render_preview(const Create_preview_settings& preview_settings)               override;
    void imgui         ()                                                              override;
    auto create        (Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

    [[nodiscard]] static auto create_brush(Brush_data& brush_create_info, const Torus_parameters& parameters) -> std::shared_ptr<Brush>;

private:
    Torus_parameters m_parameters;

    bool      m_use_debug_camera{false};
    glm::vec3 m_debug_camera    {0.0f, 0.0f, 0.0f};
    float     m_epsilon         {0.004f};
};

}
