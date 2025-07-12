#pragma once

#include "create/create_shape.hpp"

#include <glm/glm.hpp>

namespace editor {

class Create_torus : public Create_shape
{
public:
    void render_preview(const Create_preview_settings& preview_settings) override;

    void imgui() override;

    auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

private:
    float m_major_radius{1.0f};
    float m_minor_radius{0.5f};
    int   m_major_steps {32};
    int   m_minor_steps {28};

    bool      m_use_debug_camera{false};
    glm::vec3 m_debug_camera    {0.0f, 0.0f, 0.0f};
    int       m_debug_major     {0};
    int       m_debug_minor     {0};
    float     m_epsilon         {0.004f};
};

}
