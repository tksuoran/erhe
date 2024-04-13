// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/brdf_slice.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "renderers/programs.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor
{

Brdf_slice_rendergraph_node::Brdf_slice_rendergraph_node(
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    Brdf_slice&                             brdf_slice,
    Programs&                               programs
)
    : erhe::rendergraph::Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .name                 = std::string{"Brdf_slice_rendergraph_node"},
            .output_key           = erhe::rendergraph::Rendergraph_node_key::texture_for_gui,
            .color_format         = gl::Internal_format::rgba16f,
            .depth_stencil_format = gl::Internal_format{0}
        }
    }
    , m_forward_renderer  {forward_renderer}
    , m_brdf_slice        {brdf_slice}
    , m_empty_vertex_input{
        erhe::graphics::Vertex_input_state_data{}
    }
    , m_renderpass{ 
        erhe::graphics::Pipeline{
            erhe::graphics::Pipeline_data{
                .name           = "Brdf_slice",
                .shader_stages  = &programs.brdf_slice.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        }
    }
{
    register_output(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        "brdf_slice",
        erhe::rendergraph::Rendergraph_node_key::texture_for_gui
    );
}

void Brdf_slice_rendergraph_node::set_material(
    const std::shared_ptr<erhe::primitive::Material>& material
)
{
    m_material = material;
}

auto Brdf_slice_rendergraph_node::get_material() const -> erhe::primitive::Material*
{
    return m_material.get();
}

// Implements erhe::rendergraph::Rendergraph_node
void Brdf_slice_rendergraph_node::execute_rendergraph_node()
{
    SPDLOG_LOGGER_TRACE(log_render, "Brdf_slice_rendergraph_node::execute_rendergraph_node()");

    // Execute base class in order to update texture and framebuffer
    Texture_rendergraph_node::execute_rendergraph_node();

    if (!m_framebuffer) {
        // Likely because output ImGui window has no viewport size yet.
        return;
    }

    if (!m_material) {
        return;
    }

    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group pass_scope{"BRDF Slice"};

    const auto& output_viewport = get_producer_output_viewport(
        erhe::rendergraph::Resource_routing::Resource_provided_by_consumer,
        m_output_key
    );

    gl::bind_framebuffer(
        gl::Framebuffer_target::draw_framebuffer,
        m_framebuffer->gl_name()
    );

    erhe::scene_renderer::Light_projections light_projections;
    light_projections.brdf_phi          = m_brdf_slice.phi;
    light_projections.brdf_incident_phi = m_brdf_slice.incident_phi;
    light_projections.brdf_material     = m_material;

    m_forward_renderer.render_fullscreen(
        erhe::scene_renderer::Forward_renderer::Render_parameters{
            .index_type         = gl::Draw_elements_type::unsigned_int, // Note: This indices are not used by render_fullscreen()
            .light_projections  = &light_projections,
            .lights             = {},
            .materials          = gsl::span<const std::shared_ptr<erhe::primitive::Material>>(&m_material, 1),
            .mesh_spans         = {},
            .passes             = { &m_renderpass },
            .shadow_texture     = nullptr,
            .viewport           = output_viewport
        },
        nullptr
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - done");
}

void Brdf_slice_rendergraph_node::set_area_size(const int size)
{
    m_area_size = size;
}

[[nodiscard]] auto Brdf_slice_rendergraph_node::get_producer_output_viewport(
    erhe::rendergraph::Resource_routing resource_routing,
    int                                 key,
    int                                 depth
) const -> erhe::math::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(key); // TODO Validate
    static_cast<void>(depth);
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_area_size,
        .height = m_area_size
    };
}

Brdf_slice::Brdf_slice(
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    Editor_context&                         editor_context,
    Programs&                               programs
)
    : m_rendergraph{rendergraph}
    , m_context    {editor_context}
    , m_node{
        std::make_shared<Brdf_slice_rendergraph_node>(
            rendergraph,
            forward_renderer,
            *this,
            programs
        )
    }
{
}

auto Brdf_slice::get_node() const -> Brdf_slice_rendergraph_node*
{
    return m_node.get();
}

void Brdf_slice::show_brdf_slice(int area_size)
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const auto* material = m_node->get_material();
    if (material == nullptr) {
        return;
    }

    ImGui::SliderFloat("Phi",          &phi, 0.0, 1.57);
    ImGui::SliderFloat("Incident Phi", &incident_phi, 0.0, 1.57);

    //const auto  available_size = ImGui::GetContentRegionAvail();
    //const float image_size     = available_size.x; //std::min(available_size.x, available_size.y);
    //const int   area_size      = static_cast<int>(image_size);
    m_node->set_enabled(true);
    m_node->set_area_size(area_size);

    const auto& texture = m_node->get_producer_output_texture(
        erhe::rendergraph::Resource_routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::texture_for_gui
    );
    if (!texture) {
        log_render->warn("Brdf_slice has no output render graph node");
        return;
    }

    const int texture_width  = texture->width();
    const int texture_height = texture->height();

    if (
        (texture_width  > 0) &&
        (texture_height > 0) &&
        (area_size      > 0)
    ) {
        m_context.imgui_renderer->image(texture, area_size, area_size);
    }
#endif
}

} // namespace editor
