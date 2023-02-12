#pragma once

#include "tools/tool.hpp"
#include "tools/brushes/create/create.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/scene/transform.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class Create_torus
    : public Brush_create
{
public:
    void render_preview(
        const Render_context&         context,
        const erhe::scene::Transform& transform
    ) override;

    void imgui() override;

    [[nodiscard]] auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

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

} // namespace editor
