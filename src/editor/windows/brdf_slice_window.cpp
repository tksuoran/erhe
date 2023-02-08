// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/brdf_slice_window.hpp"

#include "editor_log.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/programs.hpp"
#include "scene/content_library.hpp"
#include "scene/material_library.hpp"
#include "scene/material_preview.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "tools/tools.hpp"
#include "windows/content_library_window.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/texture_rendergraph_node.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp" // TODO move light layer to separate header
#include "erhe/toolkit/profile.hpp"

#include <imgui/imgui.h>

#include <fmt/format.h>

namespace editor
{

Brdf_slice_rendergraph_node::Brdf_slice_rendergraph_node()
    : erhe::application::Texture_rendergraph_node{
        erhe::application::Texture_rendergraph_node_create_info{
            .name                 = std::string{"Brdf_slice_rendergraph_node"},
            .output_key           = erhe::application::Rendergraph_node_key::texture_for_gui,
            .color_format         = gl::Internal_format::rgba16f,
            .depth_stencil_format = gl::Internal_format{0}
        }
    }
{
    initialize_pipeline();

    register_output(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "brdf_slice",
        erhe::application::Rendergraph_node_key::texture_for_gui
    );
}

void Brdf_slice_rendergraph_node::initialize_pipeline()
{
    m_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
        erhe::graphics::Vertex_input_state_data{}
    );

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>();

    m_renderpass.pipeline.data = {
        .name           = "Brdf_slice",
        .shader_stages  = g_programs->brdf_slice.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
    };

}

// Implements erhe::application::Rendergraph_node
void Brdf_slice_rendergraph_node::execute_rendergraph_node()
{
    SPDLOG_LOGGER_TRACE(log_render, "Brdf_slice_rendergraph_node::execute_rendergraph_node()");

    // Execute base class in order to update texture and framebuffer
    Texture_rendergraph_node::execute_rendergraph_node();

    if (!m_framebuffer)
    {
        // Likely because output ImGui window has no viewport size yet.
        return;
    }

    const auto selected_material = g_content_library_window->selected_material();
    if (!selected_material)
    {
        return;
    }

    ERHE_PROFILE_FUNCTION

    erhe::graphics::Scoped_debug_group pass_scope{"BRDF Slice"};

    const auto& output_viewport = get_producer_output_viewport(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        m_output_key
    );

    gl::bind_framebuffer(
        gl::Framebuffer_target::draw_framebuffer,
        m_framebuffer->gl_name()
    );

    Light_projections light_projections;
    light_projections.brdf_phi          = g_brdf_slice_window->phi;
    light_projections.brdf_incident_phi = g_brdf_slice_window->incident_phi;
    light_projections.brdf_material     = g_brdf_slice_window->material;

    g_forward_renderer->render_fullscreen(
        Forward_renderer::Render_parameters{
            .light_projections = &light_projections,
            .lights            = {},
            .materials         = gsl::span<const std::shared_ptr<erhe::primitive::Material>>(&selected_material, 1),
            .mesh_spans        = {},
            .passes            = { &m_renderpass },
            .shadow_texture    = nullptr,
            .viewport          = output_viewport
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
    erhe::application::Resource_routing resource_routing,
    int                                 key,
    int                                 depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(key); // TODO Validate
    static_cast<void>(depth);
    return erhe::scene::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_area_size,
        .height = m_area_size
    };
}

Brdf_slice_window* g_brdf_slice_window{nullptr};

Brdf_slice_window::Brdf_slice_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Brdf_slice_window::~Brdf_slice_window() noexcept
{
    ERHE_VERIFY(g_brdf_slice_window== nullptr);
}

void Brdf_slice_window::deinitialize_component()
{
    ERHE_VERIFY(g_brdf_slice_window == this);
    m_node.reset();
    g_brdf_slice_window = nullptr;
}

void Brdf_slice_window::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Programs>();
}

void Brdf_slice_window::initialize_component()
{
    ERHE_VERIFY(g_brdf_slice_window == nullptr);

    erhe::application::g_imgui_windows->register_imgui_window(this);

    const erhe::application::Scoped_gl_context gl_context;

    m_node = std::make_shared<Brdf_slice_rendergraph_node>();
    erhe::application::g_rendergraph->register_node(m_node);

    g_brdf_slice_window = this;
}

void Brdf_slice_window::imgui()
{
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui()");

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    const auto selected_material = g_content_library_window->selected_material();
    if (!selected_material)
    {
        return;
    }

    ImGui::SliderFloat("Phi",          &phi, 0.0, 1.57);
    ImGui::SliderFloat("Incident Phi", &incident_phi, 0.0, 1.57);

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float image_size     = std::min(available_size.x, available_size.y);
    const int   area_size      = static_cast<int>(image_size);
    m_node->set_enabled(true);
    m_node->set_area_size(area_size);

    const auto& texture = m_node->get_producer_output_texture(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::texture_for_gui
    );
    if (!texture)
    {
        log_render->warn("Brdf_slice_window has no output render graph node");
        return;
    }

    const int texture_width  = texture->width();
    const int texture_height = texture->height();

    if (
        (texture_width  > 0) &&
        (texture_height > 0) &&
        (area_size      > 0)
    )
    {
        ////auto cursor_position = ImGui::GetCursorPos();
        ////cursor_position.x += (available_size.x - image_size) / 2.0f;
        ////cursor_position.y += (available_size.y - image_size) / 2.0f;
        ////ImGui::SetCursorPos(cursor_position);
        ////ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        ////SPDLOG_LOGGER_TRACE(log_render, "Brdf_slice_window::imgui() - drawing image using texture {}", m_texture->gl_name());
        image(texture, area_size, area_size);
        ////// bool is_hovered = ImGui::IsItemHovered();
        ////ImGui::PopStyleVar();
    }
    else
    {
        SPDLOG_LOGGER_TRACE(log_render, "Brdf_slice_window::imgui() - skipped - no texture or empty size");
    }
    SPDLOG_LOGGER_TRACE(log_render, "Brdf_slice_window::imgui() - done");
#endif
}

} // namespace editor
