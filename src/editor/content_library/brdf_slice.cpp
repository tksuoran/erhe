// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "content_library/brdf_slice.hpp"

#include "app_context.hpp"
#include "renderers/material_set_factory.hpp"
#include "app_message_bus.hpp"
#include "assets/asset_manager.hpp"
#include "editor_log.hpp"
#include "renderers/programs.hpp"
#include "scene/scene_root.hpp"

#include "erhe_primitive/material.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

namespace editor {

Brdf_slice_rendergraph_node::Brdf_slice_rendergraph_node(
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    Brdf_slice&                             brdf_slice,
    Programs&                               programs,
    App_context&                            app_context
)
    : erhe::rendergraph::Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .debug_label          = erhe::utility::Debug_label{"Brdf_slice_rendergraph_node"},
            .output_key           = erhe::rendergraph::Rendergraph_node_key::texture_for_gui,
            .color_format         = erhe::dataformat::Format::format_16_vec4_float,
            .depth_stencil_format = erhe::dataformat::Format::format_undefined
        }
    }
    , m_context           {app_context}
    , m_forward_renderer  {forward_renderer}
    , m_brdf_slice        {brdf_slice}
    , m_programs          {programs}
    , m_material_set{
        (app_context.material_set_factory != nullptr)
            ? app_context.material_set_factory->make_create_info("BRDF slice material set", 256, 1)
            : erhe::scene_renderer::Material_set_create_info{}
    }
    , m_empty_vertex_input{rendergraph.get_graphics_device(), erhe::graphics::Vertex_input_state_data{}}
    , m_render_pipeline_state{
        rendergraph.get_graphics_device(),
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Brdf_slice"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled
        }
    }
{
    m_frame_parameters.resize(rendergraph.get_graphics_device().get_number_of_frames_in_flight());
}

void Brdf_slice_rendergraph_node::set_material(const std::shared_ptr<erhe::primitive::Material>& material)
{
    m_material = material;
}

void Brdf_slice_rendergraph_node::prepare_rendergraph_node(const uint64_t frame_index)
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    Frame_parameters& parameters = m_frame_parameters.prepare(frame_index);
    parameters.material      = m_material;
    parameters.phi           = m_brdf_slice.phi;
    parameters.incident_phi  = m_brdf_slice.incident_phi;
    parameters.material_slot = 0u;
    if (!parameters.material) {
        return; // the render phase draws nothing, binds nothing
    }

    // The node's own material set, settled here, after the tick's material
    // schedule (Rendergraph::prepare_frame() runs late in M2): its record
    // write rides this frame's upload command buffer and update() publishes
    // the copy the render phase of this frame binds. On every frame but the
    // first both calls are no-ops: the membership never changes and the
    // content hash matches.
    m_material_set.sync_library(std::span<const std::shared_ptr<erhe::primitive::Material>>{&parameters.material, 1});
    if (m_material_set.has_gpu()) {
        ERHE_VERIFY(m_context.current_command_buffer != nullptr);
        m_material_set.update(*m_context.current_command_buffer);
    }
    parameters.material_slot = m_material_set.get_slot(parameters.material.get()).value_or(0u);
}

auto Brdf_slice_rendergraph_node::get_material() const -> erhe::primitive::Material*
{
    return m_material.get();
}

// Implements erhe::rendergraph::Rendergraph_node
void Brdf_slice_rendergraph_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    SPDLOG_LOGGER_TRACE(log_render, "Brdf_slice_rendergraph_node::execute_rendergraph_node()");

    // The size Brdf_slice::update() requested for this frame.
    apply_requested_size();
    if (m_render_target.get_render_pass() == nullptr) {
        // Likely because output ImGui window has no viewport size yet.
        return;
    }

    // The material and angles the prepare phase of this frame copied.
    const Frame_parameters& parameters = m_frame_parameters.get(m_rendergraph.get_graphics_device().get_frame_index());
    if (!parameters.material) {
        return;
    }

    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Render_pass* render_pass = m_render_target.get_render_pass();
    erhe::graphics::Scoped_render_pass scoped_render_pass{*render_pass, command_buffer};
    erhe::graphics::Scoped_debug_group pass_scope{
        command_buffer,
        "Brdf_slice_rendergraph_node::execute_rendergraph_node()"
    };
    erhe::scene_renderer::Light_projections light_projections;
    light_projections.brdf_phi          = parameters.phi;
    light_projections.brdf_incident_phi = parameters.incident_phi;
    // The slot the prepare phase resolved in the set this pass binds, below.
    light_projections.brdf_material_slot = parameters.material_slot;

    const std::shared_ptr<erhe::graphics::Texture>& color_texture = m_render_target.get_color_texture();
    ERHE_VERIFY(color_texture);
    erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = color_texture->get_width(),
        .height = color_texture->get_height()
    };

    erhe::graphics::Render_command_encoder render_encoder = m_rendergraph.get_graphics_device().make_render_command_encoder(command_buffer);

            //.shader_stages  = programs.brdf_slice.shader_stages(),
            //.vertex_input   = &m_empty_vertex_input,

    m_forward_renderer.draw_primitives(
        erhe::scene_renderer::Forward_renderer::Primitive_render_parameters{
            .base = erhe::scene_renderer::Forward_renderer::Base_render_parameters{
                .render_encoder    = render_encoder,
                .render_pass       = render_pass,
                .viewport          = viewport,
                //.index_type      = erhe::dataformat::Format::format_32_scalar_uint, // Note: Indices are not used by render_fullscreen()
                .light_projections = &light_projections,
                .material_source   = &m_material_set,
                .debug_label       = "Brdf_slice_rendergraph_node::execute_rendergraph_node()"
            },
            .vertex_count         = 3, // full screen quad using vertices generated by vertex shader
            .base_render_pipeline = m_render_pipeline_state,
            .shader_stages        = &m_programs.brdf_slice.shader_stages
        },
        nullptr
    );

    SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::render() - done");
}

void Brdf_slice_rendergraph_node::set_area_size(const int size)
{
    request_size(size, size);
}

Brdf_slice::Brdf_slice(
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    App_context&                            app_context,
    App_message_bus&                        app_message_bus,
    Programs&                               programs
)
    : m_rendergraph{rendergraph}
    , m_context    {app_context}
    , m_node{
        erhe::rendergraph::make_rendergraph_node<Brdf_slice_rendergraph_node>(rendergraph, forward_renderer, *this, programs, app_context)
    }
{
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
}

void Brdf_slice::on_items_removed(const Removed_items& removed)
{
    erhe::primitive::Material* const material = m_node ? m_node->get_material() : nullptr;
    if ((material != nullptr) && removed.lookup.contains(material)) {
        m_node->set_material({});
    }
}

void Brdf_slice::on_close_scene(erhe::Item_host* const closing_host)
{
    // R5.6: materials are not hosted; ask the manager whether the closing
    // scene's container record defines this one.
    erhe::primitive::Material* const material = m_node ? m_node->get_material() : nullptr;
    if ((material != nullptr) && (m_context.asset_manager != nullptr) && m_context.asset_manager->is_hosted_or_defined_by(*material, closing_host)) {
        m_node->set_material({});
    }
}

auto Brdf_slice::get_node() const -> Brdf_slice_rendergraph_node*
{
    return m_node.get();
}

void Brdf_slice::update()
{
    if (!m_request_pending || !m_node) {
        return;
    }
    m_request_pending = false;
    m_node->set_material(m_requested_material);
    m_node->set_enabled(static_cast<bool>(m_requested_material));
    if (m_requested_area_size > 0) {
        m_node->set_area_size(m_requested_area_size);
    }
}

void Brdf_slice::show_brdf_slice(const std::shared_ptr<erhe::primitive::Material>& material, int area_size)
{
    ERHE_PROFILE_FUNCTION();

    if (!material || !m_node) {
        return;
    }
    m_requested_material  = material;
    m_requested_area_size = area_size;
    m_request_pending     = true;

    ImGui::SliderFloat("Phi",          &phi,          0.0f, 1.57f);
    ImGui::SliderFloat("Incident Phi", &incident_phi, 0.0f, 1.57f);

    // The node itself is the texture reference: the ImGui renderer resolves
    // it to the node's output texture when it renders, in the render phase,
    // which is the phase that sizes (and replaces) that texture.
    if (area_size > 0) {
        m_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = std::static_pointer_cast<erhe::graphics::Texture_reference>(m_node),
                .width             = area_size,
                .height            = area_size,
                .uv0               = m_context.imgui_renderer->get_rtt_uv0(),
                .uv1               = m_context.imgui_renderer->get_rtt_uv1(),
                .filter            = erhe::graphics::Filter::nearest,
                .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .debug_label       = "Brdf_slice::show_brdf_slice"
            }
        );
    }
}

}
