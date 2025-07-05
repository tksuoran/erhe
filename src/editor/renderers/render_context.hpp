#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

namespace erhe::graphics {
    class Shader_stages;
}
namespace erhe::scene {
    class Camera;
    class Node;
    class Scene;
}

namespace editor {

class App_context;
class Scene_view;
class Viewport_config;
class Viewport_scene_view;

class Render_context
{
public:
    [[nodiscard]] auto get_camera_node  () const -> const erhe::scene::Node*;
    [[nodiscard]] auto get_scene        () const -> const erhe::scene::Scene*;
    [[nodiscard]] auto get_line_renderer(const erhe::renderer::Debug_renderer_config& config) const -> erhe::renderer::Primitive_renderer;
    [[nodiscard]] auto get_line_renderer(unsigned int stencil, bool visible, bool hidden) const -> erhe::renderer::Primitive_renderer;

    erhe::graphics::Render_command_encoder* encoder{nullptr};
    App_context&                            app_context;
    Scene_view&                             scene_view;
    Viewport_config&                        viewport_config;
    erhe::scene::Camera*                    camera                {nullptr};
    Viewport_scene_view*                    viewport_scene_view   {nullptr};
    erhe::math::Viewport                    viewport              {0, 0, 0, 0};
    const erhe::graphics::Shader_stages*    override_shader_stages{nullptr};
};

}
