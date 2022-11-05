#pragma once

#include "erhe/scene/viewport.hpp"

namespace erhe::graphics
{
    class Shader_stages;
}

namespace erhe::scene
{
    class Camera;
}

namespace editor
{

class Scene_view;
class Viewport_config;
class Viewport_window;

class Render_context
{
public:
    Scene_view*                    scene_view            {nullptr};
    Viewport_window*               viewport_window       {nullptr};
    Viewport_config*               viewport_config       {nullptr};
    erhe::scene::Camera*           camera                {nullptr};
    erhe::scene::Viewport          viewport              {0, 0, 0, 0, true};
    erhe::graphics::Shader_stages* override_shader_stages{nullptr};
};

} // namespace editor
