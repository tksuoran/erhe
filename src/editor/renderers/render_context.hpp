#pragma once

#include "erhe/scene/viewport.hpp"

namespace erhe::scene
{
    class ICamera;
}

namespace editor
{

class Viewport_config;
class Viewport_window;

class Render_context
{
public:
    Viewport_window*               window                {nullptr};
    Viewport_config*               viewport_config       {nullptr};
    erhe::scene::ICamera*          camera                {nullptr};
    erhe::scene::Viewport          viewport              {0, 0, 0, 0, true};
    erhe::graphics::Shader_stages* override_shader_stages{nullptr};
};


} // namespace editor
