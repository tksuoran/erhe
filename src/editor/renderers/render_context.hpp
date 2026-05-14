#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"

#include <span>

namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Shader_stages;
}
namespace erhe::scene {
    class Camera;
    class Node;
    class Scene;
}

struct Viewport_config;

namespace editor {

class App_context;
class Scene_view;
class Viewport_scene_view;

class Render_context
{
public:
    [[nodiscard]] auto get_camera_node() const -> const erhe::scene::Node*;
    [[nodiscard]] auto get_scene      () const -> const erhe::scene::Scene*;
    [[nodiscard]] auto get            (const erhe::renderer::Debug_renderer_config& config) const -> erhe::renderer::Primitive_renderer;

    erhe::graphics::Command_buffer*         command_buffer{nullptr};
    erhe::graphics::Render_command_encoder* encoder{nullptr};
    erhe::graphics::Render_pass*            render_pass{nullptr};
    App_context&                            app_context;
    Scene_view&                             scene_view;
    Viewport_config&                        viewport_config;
    erhe::scene::Camera*                    camera                {nullptr};
    Viewport_scene_view*                    viewport_scene_view   {nullptr};
    erhe::math::Viewport                    viewport              {0, 0, 0, 0};
    const erhe::graphics::Shader_stages*    override_shader_stages{nullptr};
    // Multiview cameras for the headset multiview render path. When
    // non-empty, Composition_pass forwards this span as
    // Render_parameters::multiview_views so Forward_renderer writes
    // one Camera UBO entry per view and pipelines pick their
    // multiview_shader_stages sibling. Empty on the editor's
    // single-view viewport rendering path.
    std::span<const erhe::scene_renderer::Camera_view_input> multiview_views{};
};

}
