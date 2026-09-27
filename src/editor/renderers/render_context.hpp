#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <span>

namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Shader_stages;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Xformable; using Node = Xformable;
    class Scene;
}

struct Viewport_config;

namespace editor {

class App_context;
class Scene_view;
class Viewport_scene_view;

// Which composition passes a render draws. scene_only leaves out the passes
// marked Composition_pass_kind::editor_aid (grid, selection outline, ghost
// edge lines, brush preview, solid bones): the offscreen scene image render
// (MCP render_scene_image, scene/scene_image_capture.hpp) uses it.
enum class Render_content : unsigned int {
    scene_and_editor_aids = 0,
    scene_only
};

class Render_context
{
public:
    [[nodiscard]] auto get_camera_node() const -> const erhe::scene::Node*;
    [[nodiscard]] auto get_scene      () const -> const erhe::scene::Scene*;
    [[nodiscard]] auto get            (const erhe::renderer::Debug_renderer_config& config) const -> erhe::renderer::Primitive_renderer;

    erhe::graphics::Command_buffer*         command_buffer     {nullptr};
    erhe::graphics::Render_command_encoder* encoder            {nullptr};
    erhe::graphics::Render_pass*            render_pass        {nullptr};
    App_context&                            app_context;
    Scene_view&                             scene_view;
    Viewport_config&                        viewport_config;
    erhe::scene::Camera*                    camera             {nullptr};
    Viewport_scene_view*                    viewport_scene_view{nullptr};
    erhe::math::Viewport                    viewport           {0, 0, 0, 0};
    erhe::scene_renderer::Shader_debug      shader_debug       {erhe::scene_renderer::Shader_debug::none};
    std::span<const erhe::scene_renderer::Camera_view_input> views; // multiview
    Render_content                          content            {Render_content::scene_and_editor_aids};

};

}
