#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <memory>
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
class Composer_prepared;
class Scene_root;
class Scene_view;
class Viewport_render_state;
class Viewport_scene_view;

// Which composition passes a render draws. scene_only leaves out the passes
// marked Composition_pass_kind::editor_aid (grid, selection outline, ghost
// edge lines, brush preview, solid bones): the offscreen scene image render
// (MCP render_scene_image, scene/scene_image_capture.hpp) uses it.
// scene_surfaces also leaves out the Composition_pass_kind::background passes
// (sky), so pixels no surface covers keep the render target's clear value:
// render_scene_image's linear debug renders mark them from that.
enum class Render_content : unsigned int {
    scene_and_editor_aids = 0,
    scene_only,
    scene_surfaces
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
    // The live view camera: valid on the main thread only (the tools'
    // CPU phase, the previews); null in a render phase context, where the
    // renderers read camera_snapshot, the render phase's copy
    // (doc/editor/editor.md "Frame phases and the render thread").
    erhe::scene::Camera*                    camera             {nullptr};
    const erhe::scene::Camera_snapshot*     camera_snapshot    {nullptr};
    Viewport_scene_view*                    viewport_scene_view{nullptr};
    // The scene root the context renders. A render phase context takes the
    // frame's copy (Viewport_render_state::scene_root,
    // Scene_view::get_render_scene_root()); the render phase reads it
    // instead of Scene_view::get_scene_root(), the live weak reference.
    std::shared_ptr<Scene_root>             scene_root         {};
    // The viewport's render state copy of the frame (prepare or render
    // phase); null for the other views (previews, scene images, headset),
    // whose consumers read the live state on the thread that renders them.
    const Viewport_render_state*            render_state       {nullptr};
    erhe::math::Viewport                    viewport           {0, 0, 0, 0};
    erhe::scene_renderer::Shader_debug      shader_debug       {erhe::scene_renderer::Shader_debug::none};
    // Light slot Shader_debug::shadow_visibility shows
    // (Base_render_parameters::shadow_debug_light_index).
    uint32_t                                shadow_debug_light_index{0};
    std::span<const erhe::scene_renderer::Camera_view_input> views; // multiview
    Render_content                          content            {Render_content::scene_and_editor_aids};
    // The view's composition prepared for this frame in the prepare phase
    // (Composer_frame_slots); null makes Composer::render() prepare each
    // pass where it renders (previews, headset).
    const Composer_prepared*                composer_prepared  {nullptr};

};

}
