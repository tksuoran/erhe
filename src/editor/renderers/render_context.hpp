#pragma once

#include "erhe/toolkit/viewport.hpp"

namespace erhe::graphics {
    class Shader_stages;
}
namespace erhe::scene {
    class Camera;
    class Node;
    class Scene;
}

namespace editor
{

class Editor_context;
class Scene_view;
class Viewport_config;
class Viewport_window;

class Render_context
{
public:
    [[nodiscard]] auto get_camera_node() const -> const erhe::scene::Node*;
    [[nodiscard]] auto get_scene      () const -> const erhe::scene::Scene*;

    Editor_context&                editor_context;
    Scene_view&                    scene_view;
    Viewport_config&               viewport_config;
    erhe::scene::Camera&           camera;
    Viewport_window*               viewport_window       {nullptr};
    erhe::toolkit::Viewport        viewport              {0, 0, 0, 0, true};
    erhe::graphics::Shader_stages* override_shader_stages{nullptr};
};

} // namespace editor
