#pragma once

#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"

#include <memory>

namespace erhe::graphics       { class Command_buffer; }
namespace erhe::imgui          { class Imgui_windows; }
namespace erhe::primitive      { class Material; }
namespace erhe::rendergraph    { class Rendergraph; }
namespace erhe::scene_renderer { class Forward_renderer; }

namespace editor {

class Brdf_slice;
class App_context;
class App_message_bus;
class Programs;

class Brdf_slice_rendergraph_node : public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Brdf_slice_rendergraph_node(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        Brdf_slice&                             brdf_slice,
        Programs&                               programs
    );

    // Implements erhe::rendergraph::Rendergraph_node
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    void set_area_size(int size);

    // Public API
    void set_material(const std::shared_ptr<erhe::primitive::Material>& material);
    [[nodiscard]] auto get_material() const -> erhe::primitive::Material*;

private:
    erhe::scene_renderer::Forward_renderer&    m_forward_renderer;
    Brdf_slice&                                m_brdf_slice;
    std::shared_ptr<erhe::primitive::Material> m_material;

    erhe::graphics::Vertex_input_state                 m_empty_vertex_input;
    erhe::graphics::Base_render_pipeline               m_render_pipeline_state;
    int                                                m_area_size{0};
};


/// Rendergraph sink node for showing texture in ImGui window
class Brdf_slice
{
public:
    Brdf_slice(
        erhe::rendergraph::Rendergraph&         rendergraph,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        App_context&                            app_context,
        App_message_bus&                        app_message_bus,
        Programs&                               programs
    );

    auto get_node() const -> Brdf_slice_rendergraph_node*;
    void show_brdf_slice(int area_size);

    float phi         {0.0f};
    float incident_phi{0.0f};

private:
    // Scene close: drop the inspected material when the closing scene's
    // content library hosts it (write-only cache; it would keep the dead
    // scene's material alive).
    void on_close_scene(erhe::Item_host* closing_host);

    erhe::rendergraph::Rendergraph&              m_rendergraph;
    App_context&                                 m_context;
    std::shared_ptr<Brdf_slice_rendergraph_node> m_node;

    erhe::message_bus::Subscription<Close_scene_message> m_close_scene_subscription;
};

}
