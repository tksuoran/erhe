#pragma once

#include <glm/glm.hpp>

namespace erhe::scene {
    class Transform;
}

namespace editor
{

class Render_context;

class Create_preview_settings
{
public:
    const Render_context&         render_context;
    const erhe::scene::Transform& transform;
    glm::vec4                     major_color    {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4                     minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
    const float                   major_thickness{6.0f};
    const float                   minor_thickness{3.0f};
    bool                          ideal_shape    {false};
};

} // namespace editor
