#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_utility/debug_label.hpp"

#include <memory>
#include <string_view>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
}
namespace erhe::rendergraph {
    class Rendergraph;
}

namespace editor {

class App_context;
class Viewport_scene_view;

// The overdraw visualization of one viewport (doc/plans/overdraw_visualization.md
// O4): placed after the viewport's Viewport_scene_view in place of its
// post-processing node while the viewport's overdraw mode is on
// (Scene_views::set_overdraw_mode()). One fullscreen pass into its own
// texture of the view's size: the view's per-pixel fragment shader
// invocation counts (Overdraw_view_state) through a color ramp, and over
// count 0 the view's color at 25 % luminance
// (res/editor/shaders/overdraw_visualize.frag).
class Overdraw_node : public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Overdraw_node(
        erhe::rendergraph::Rendergraph&             rendergraph,
        App_context&                                app_context,
        const std::shared_ptr<Viewport_scene_view>& viewport_scene_view,
        erhe::utility::Debug_label                  debug_label
    );
    ~Overdraw_node() noexcept override;

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Overdraw_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // The viewport this node belongs to (Scene_views teardown and
    // rewiring). May be expired during shutdown.
    [[nodiscard]] auto get_viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view> { return m_viewport_scene_view.lock(); }

private:
    static constexpr unsigned int input_binding_point     {0};
    static constexpr unsigned int counts_binding_point    {1};
    static constexpr unsigned int parameters_binding_point{2};

    App_context&                             m_app_context;
    // Weak, as Viewport_overlay_node: the node graph does not keep the view
    // alive.
    std::weak_ptr<Viewport_scene_view>       m_viewport_scene_view;
    erhe::graphics::Shader_resource          m_counts_block;
    erhe::graphics::Shader_resource          m_parameters_block;
    erhe::graphics::Bind_group_layout        m_bind_group_layout;
    erhe::graphics::Fragment_outputs         m_fragment_outputs;
    erhe::graphics::Reloadable_shader_stages m_shader_stages;
    erhe::graphics::Base_render_pipeline     m_pipeline;
    erhe::graphics::Sampler                  m_sampler_nearest;
    erhe::graphics::Ring_buffer_client       m_parameters_buffer;
};

}
