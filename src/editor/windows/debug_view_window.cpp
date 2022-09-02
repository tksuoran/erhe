// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/debug_view_window.hpp"
#include "editor_log.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
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

Depth_to_color_rendergraph_node::Depth_to_color_rendergraph_node(
    erhe::components::Components& components
)
    : erhe::application::Texture_rendergraph_node{
        erhe::application::Texture_rendergraph_node_create_info{
            .name                 = std::string{"Depth_to_color_rendergraph_node"},
            .input_key            = erhe::application::Rendergraph_node_key::shadow_maps,
            .output_key           = erhe::application::Rendergraph_node_key::depth_visualization,
            .color_format         = gl::Internal_format::rgba8,
            .depth_stencil_format = gl::Internal_format{0}
        }
    }
{
    m_programs               = components.get<Programs>();
    m_gl_context_provider    = components.get<erhe::application::Gl_context_provider>();
    m_pipeline_state_tracker = components.get<erhe::graphics::OpenGL_state_tracker>();
    m_forward_renderer       = components.get<Forward_renderer>();
    m_shadow_renderer        = components.get<Shadow_renderer >();

    initialize_pipeline();

    // register_input() & register_output() is done by Texture_rendergraph_node constructor
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "shadow_maps",
        erhe::application::Rendergraph_node_key::shadow_maps
    );
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "depth_visualization",
        erhe::application::Rendergraph_node_key::depth_visualization
    );
}

void Depth_to_color_rendergraph_node::initialize_pipeline()
{
    const erhe::application::Scoped_gl_context gl_context{
        m_gl_context_provider
    };

    m_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
        erhe::graphics::Vertex_input_state_data{}
    );

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>();

    m_renderpass.pipeline.data = {
        .name           = "Debug_view",
        .shader_stages  = m_programs->debug_depth.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
    };

}

// Implements erhe::application::Rendergraph_node
void Depth_to_color_rendergraph_node::execute_rendergraph_node()
{
    SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node()");

    // Execute base class in order to update texture and framebuffer
    Texture_rendergraph_node::execute_rendergraph_node();

    if (!m_framebuffer)
    {
        // Likely because output ImGui window has no viewport size yet.
        return;
    }

    ERHE_PROFILE_FUNCTION

    Rendergraph_node* input_node = get_consumer_input_node(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::shadow_maps
    ).lock().get();
    if (input_node == nullptr)
    {
        return;
    }

    Shadow_render_node* shadow_render_node = reinterpret_cast<Shadow_render_node*>(input_node);
    if (shadow_render_node == nullptr)
    {
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: shadow_maps input is not connected"
        );
        return;
    }

    const auto& shadow_texture = shadow_render_node->texture();
    if (!shadow_texture)
    {
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: shadow render node has no texture"
        );
        return;
    }

    const int texture_width  = shadow_texture->width ();
    const int texture_height = shadow_texture->height();

    if (
        (texture_width  < 1) ||
        (texture_height < 1)
    )
    {
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: no shadow renderer or empty viewport"
        );
        return;
    }

    const auto& viewport_window = shadow_render_node->viewport_window();
    const auto& scene_root = viewport_window->scene_root();
    if (!scene_root)
    {
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: no shadow scene root"
        );
        return;
    }

    const auto& light_projections = shadow_render_node->light_projections();
    if (light_projections.light_projection_transforms.empty())
    {
        return;
    }

    if (m_light_index >= light_projections.light_projection_transforms.size())
    {
        SPDLOG_LOGGER_TRACE(
            log_render,
            "Depth_to_color_rendergraph_node::rendexecute_rendergraph_nodeer() - skipped: invalid selected light index"
        );
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Debug View"};

    gl::bind_framebuffer(
        gl::Framebuffer_target::draw_framebuffer,
        m_framebuffer->gl_name()
    );

    const auto& light_projection_transforms = light_projections.light_projection_transforms.at(m_light_index);
    const auto& layers = scene_root->layers();

    m_forward_renderer->render_fullscreen(
        Forward_renderer::Render_parameters{
            .light_projections = &light_projections,
            .lights            = layers.light()->lights,
            .materials         = {},
            .mesh_spans        = {},
            .passes            = { &m_renderpass },
            .shadow_texture    = shadow_render_node->texture().get(),
            .viewport          = erhe::scene::Viewport{
                .x      = 0,
                .y      = 0,
                .width  = texture_width,
                .height = texture_height
            }
        },
        light_projection_transforms.light
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - done");
}

[[nodiscard]] auto Depth_to_color_rendergraph_node::get_light_index() -> int&
{
    return m_light_index;
}

Debug_view_window::Debug_view_window()
    : erhe::components::Component        {c_type_name}
    , erhe::application::Rendergraph_node{"Debug View"}
    , erhe::application::Imgui_window    {c_title, c_type_name}
{
    register_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        "depth_visualization",
        erhe::application::Rendergraph_node_key::depth_visualization
    );

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Debug_view_window a dependency for Imgui_viewport, forcing
    // correct rendering order (Imgui_viewport_window must be rendered before
    // Imgui_viewport).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(
        erhe::application::Resource_routing::None,
        "window",
        erhe::application::Rendergraph_node_key::window
    );
}

Debug_view_window::~Debug_view_window() noexcept
{
}

void Debug_view_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Debug_view_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Debug_view_window::post_initialize()
{
    m_shadow_renderer = get<Shadow_renderer>();
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

void Debug_view_window::execute_rendergraph_node()
{
    // NOP
}

[[nodiscard]] auto Debug_view_window::get_consumer_input_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
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

void Debug_view_window::imgui()
{
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui()");

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    if (!m_shadow_renderer)
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - skipped - no shadow renderer");
        return;
    }

    const auto* input = get_input(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::depth_visualization
    );
    if (input == nullptr)
    {
        log_render->error("Debug_view_window has no input registered.");
        return;
    }

    if (input->producer_nodes.empty())
    {
        log_render->error("Debug_view_window input producer is not connected.");
        return;
    }

    const auto& producer_node = input->producer_nodes.front().lock();
    if (!producer_node)
    {
        log_render->error("Debug_view_window input producer is expired or not set.");
    }

    // TODO add safety?
    auto* input_texture_node = reinterpret_cast<Depth_to_color_rendergraph_node*>(producer_node.get());
    if (input_texture_node == nullptr)
    {
        log_render->error("Debug_view_window has no input render graph node");
        return;
    }

    auto* shadow_render_node = reinterpret_cast<Shadow_render_node*>(
        producer_node->get_consumer_input_node(
            erhe::application::Resource_routing::Resource_provided_by_producer,
            erhe::application::Rendergraph_node_key::shadow_maps
        ).lock().get()
    );
    if (shadow_render_node == nullptr)
    {
        return;
    }

    const auto& light_projections           = shadow_render_node->light_projections();
    const auto& light_projection_transforms = light_projections.light_projection_transforms;
    const int count = static_cast<int>(light_projection_transforms.size());
    int& light_index = input_texture_node->get_light_index();
    for (int i = 0; i < count; ++i)
    {
        const auto& light_projection_transform = light_projection_transforms.at(i);
        ERHE_VERIFY(light_projection_transform.light != nullptr);

        ImGui::SameLine();
        std::string label = fmt::format("{}", i);
        if (
            erhe::application::make_button(
                label.c_str(),
                (light_index == i)
                    ? erhe::application::Item_mode::active
                    : erhe::application::Item_mode::normal
            )
        )
        {
            light_index = i;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", light_projection_transform.light->name().c_str());
        }
    }

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float image_size     = std::min(available_size.x, available_size.y);
    m_area_size = static_cast<int>(image_size);

    const auto& texture = get_consumer_input_texture(
        erhe::application::Resource_routing::Resource_provided_by_producer,
        erhe::application::Rendergraph_node_key::depth_visualization
    );
    if (!texture)
    {
        log_render->warn("Debug_view_window has no input render graph node");
        return;
    }

    const int texture_width  = texture->width();
    const int texture_height = texture->height();

    if (
        texture &&
        (texture_width  > 0) &&
        (texture_height > 0) &&
        (m_area_size > 0)
    )
    {
        auto cursor_position = ImGui::GetCursorPos();
        cursor_position.x += (available_size.x - image_size) / 2.0f;
        cursor_position.y += (available_size.y - image_size) / 2.0f;
        ImGui::SetCursorPos(cursor_position);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - drawing image using texture {}", m_texture->gl_name());
        image(texture, m_area_size, m_area_size);
        // bool is_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
    }
    else
    {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - skipped - no texture or empty size");
    }
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - done");
#endif
}

} // namespace editor
