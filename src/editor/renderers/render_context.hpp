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
namespace erhe::scene_renderer {
    class Face_id_base_provider;
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

    // ID-buffer edge-line method (active for the frame only when set by
    // Viewport_scene_view). edge_id_texture is the face-ID buffer the
    // EDGE_LINES_FROM_ID fill variant samples; face_id_base_provider supplies the
    // matching per-primitive base. Null when the method is off, so capable fill
    // passes fall back to the normal fill.
    const erhe::graphics::Texture*                   edge_id_texture      {nullptr};
    const erhe::scene_renderer::Face_id_base_provider* face_id_base_provider{nullptr};
};

}
