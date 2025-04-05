// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "windows/debug_view_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_view.hpp"
#include "scene/scene_root.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

namespace editor {

Depth_to_color_rendergraph_node::Depth_to_color_rendergraph_node(
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    Mesh_memory&                            mesh_memory,
    Programs&                               programs
)
    : erhe::rendergraph::Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .name                 = std::string{"Depth_to_color_rendergraph_node"},
            .input_key            = erhe::rendergraph::Rendergraph_node_key::shadow_maps,
            .output_key           = erhe::rendergraph::Rendergraph_node_key::depth_visualization,
            .color_format         = gl::Internal_format::rgba8,
            .depth_stencil_format = gl::Internal_format{0}
        }
    }
    , m_forward_renderer{forward_renderer}
    , m_mesh_memory{mesh_memory}
    , m_empty_vertex_input{}
    , m_renderpass{ 
        erhe::graphics::Pipeline{
            erhe::graphics::Pipeline_data{
                .name           = "Debug_view",
                .shader_stages  = &programs.debug_depth.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        }
    }
{
    // register_input() & register_output() is done by Texture_rendergraph_node constructor
    register_input(erhe::rendergraph::Routing::Resource_provided_by_producer, "shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    register_output(erhe::rendergraph::Routing::Resource_provided_by_producer, "depth_visualization", erhe::rendergraph::Rendergraph_node_key::depth_visualization);
}

// Implements erhe::rendergraph::Rendergraph_node
void Depth_to_color_rendergraph_node::execute_rendergraph_node()
{
    SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node()");

    // Execute base class in order to update texture and framebuffer
    Texture_rendergraph_node::execute_rendergraph_node();

    if (!m_framebuffer) {
        // Likely because output ImGui window has no viewport size yet.
        return;
    }

    ERHE_PROFILE_FUNCTION();

    Rendergraph_node* input_node = get_consumer_input_node(erhe::rendergraph::Routing::Resource_provided_by_producer, erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    if (input_node == nullptr) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: shadow_maps input is not connected");
        return;
    }

    Shadow_render_node* shadow_render_node = static_cast<Shadow_render_node*>(input_node);
    const auto& shadow_texture = shadow_render_node->get_texture();
    if (!shadow_texture) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: shadow render node has no texture");
        return;
    }

    const int input_texture_width  = shadow_texture->width ();
    const int input_texture_height = shadow_texture->height();

    if ((input_texture_width  < 1) || (input_texture_height < 1)) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: no shadow renderer or empty viewport");
        return;
    }

    const auto& scene_view = shadow_render_node->get_scene_view();
    const auto& scene_root = scene_view.get_scene_root();
    if (!scene_root) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: no shadow scene root");
        return;
    }

    const auto& light_projections = shadow_render_node->get_light_projections();
    if (light_projections.light_projection_transforms.empty()) {
        return;
    }

    if (static_cast<std::size_t>(m_light_index) >= light_projections.light_projection_transforms.size()) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::rendexecute_rendergraph_nodeer() - skipped: invalid selected light index");
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Debug View"};

    const auto& output_viewport = get_producer_output_viewport(erhe::rendergraph::Routing::Resource_provided_by_consumer, m_output_key);
    ERHE_VERIFY(output_viewport.width >= 0);
    ERHE_VERIFY(output_viewport.height >= 0);
    ERHE_VERIFY(output_viewport.width < 32768);
    ERHE_VERIFY(output_viewport.height < 32768);
    if ((output_viewport.width == 0) || (output_viewport.height == 0)) {
        return;
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());

    const auto& light_projection_transforms = light_projections.light_projection_transforms.at(m_light_index);
    const auto& layers = scene_root->layers();
    auto texture = shadow_render_node->get_texture();

    log_render->trace("Depth to color from texture '{}'", texture->debug_label());

    m_forward_renderer.draw_primitives(
        erhe::scene_renderer::Forward_renderer::Render_parameters{
            .index_type            = m_mesh_memory.buffer_info.index_type,
            .index_buffer          = nullptr,
            .vertex_buffer0        = nullptr,
            .vertex_buffer1        = nullptr,
            .light_projections     = &light_projections,
            .lights                = layers.light()->lights,
            .materials             = {},
            .mesh_spans            = {},
            .non_mesh_vertex_count = 3, // Full-screen triangle
            .passes                = { &m_renderpass },
            .shadow_texture        = texture.get(),
            .viewport              = output_viewport,
            .debug_label           = "Depth_to_color_rendergraph_node::execute_rendergraph_node()"
        },
        light_projection_transforms.light
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::render() - done");
}

auto Depth_to_color_rendergraph_node::get_light_index() -> int&
{
    return m_light_index;
}

Debug_view_node::Debug_view_node(erhe::rendergraph::Rendergraph& rendergraph)
    : erhe::rendergraph::Rendergraph_node{rendergraph, "Debug View"}
{
    register_input(
        erhe::rendergraph::Routing::Resource_provided_by_producer,
        "depth_visualization",
        erhe::rendergraph::Rendergraph_node_key::depth_visualization
    );

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Debug_view_window a dependency for Imgui_host, forcing
    // correct rendering order (Imgui_window_scene_view must be rendered before
    // Imgui_host).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_output(erhe::rendergraph::Routing::None, "window", erhe::rendergraph::Rendergraph_node_key::window);
}

void Debug_view_node::execute_rendergraph_node()
{
    // NOP
}

void Debug_view_node::set_area_size(const int size)
{
    ERHE_VERIFY(size >= 0);
    m_area_size = size;
}

auto Debug_view_node::get_consumer_input_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_area_size,
        .height = m_area_size
    };
}

Debug_view_window::Debug_view_window(
    erhe::imgui::Imgui_renderer&            imgui_renderer,
    erhe::imgui::Imgui_windows&             imgui_windows,
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    Editor_context&                         editor_context,
    Editor_rendering&                       editor_rendering,
    Mesh_memory&                            mesh_memory,
    Programs&                               programs
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Debug View", "debug_view"}
    , m_context                {editor_context}
    , m_node                   {rendergraph}
{
    if (editor_context.OpenXR || !editor_context.developer_mode) {
        hide_window();
        hidden();
        return;
    }

    m_depth_to_color_node = std::make_unique<Depth_to_color_rendergraph_node>(rendergraph, forward_renderer, mesh_memory, programs);
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::depth_visualization, m_depth_to_color_node.get(), &m_node);

    const auto& window_imgui_host = imgui_windows.get_window_imgui_host();
    if (window_imgui_host) {
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::window, &m_node, window_imgui_host.get());
    }

    const auto& nodes = editor_rendering.get_all_shadow_nodes();
    if (!nodes.empty()) {
        const auto& node = nodes.front();
        set_shadow_renderer_node(rendergraph, node.get());
    }
}

void Debug_view_window::set_shadow_renderer_node(erhe::rendergraph::Rendergraph& rendergraph, Shadow_render_node* node)
{
    if (m_context.OpenXR) {
        return; // TODO
    }
    if (!m_depth_to_color_node) {
        return;
    }

    if (m_shadow_renderer_node) {
        m_context.rendergraph->disconnect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, m_shadow_renderer_node, m_depth_to_color_node.get());
    }

    m_shadow_renderer_node = node;

    if (node) {
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, m_shadow_renderer_node, m_depth_to_color_node.get());
        m_depth_to_color_node->set_enabled(true);
        m_node.set_enabled(true);
    } else {
        m_depth_to_color_node->set_enabled(true);
        m_node.set_enabled(false);
    }
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> std::span<const T>
{
    return std::span<const T>(&value, 1);
}

void Debug_view_window::hidden()
{
    if (m_context.OpenXR) {
        return; // TODO
    }

    m_node.set_enabled(false);

    // TODO
    const auto* input = m_node.get_input(
        erhe::rendergraph::Routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::depth_visualization
    );
    if ((input == nullptr) || input->producer_nodes.empty()) {
        return;
    }

    auto* producer_node = input->producer_nodes.front();
    if (!producer_node) {
        return;
    }
    producer_node->set_enabled(false);
}

void Debug_view_window::imgui()
{
    if (m_context.OpenXR) {
        return; // TODO
    }
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui()");

    //// Rendergraph_node::set_enabled(true);
    const auto& nodes = m_context.editor_rendering->get_all_shadow_nodes();
    if (!nodes.empty()) {
        int last_node_index = static_cast<int>(nodes.size() - 1);
        const bool node_set = ImGui::SliderInt("Node", &m_selected_node, 0, last_node_index);
        if (node_set && (m_selected_node >= 0) && (m_selected_node < nodes.size())) {
            const auto node = nodes.at(m_selected_node);
            set_shadow_renderer_node(*m_context.rendergraph, node.get());
        }
    }

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const auto* input = m_node.get_input(
        erhe::rendergraph::Routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::depth_visualization
    );
    if (input == nullptr) {
        log_render->error("Debug_view_window has no input registered.");
        return;
    }

    if (input->producer_nodes.empty()) {
        log_render->error("Debug_view_window input producer is not connected.");
        return;
    }

    auto* producer_node = input->producer_nodes.front();
    if (producer_node == nullptr) {
        log_render->error("Debug_view_window input producer is expired or not set.");
        return;
    }
    producer_node->set_enabled(true);

    // TODO add safety?
    auto* input_texture_node = static_cast<Depth_to_color_rendergraph_node*>(producer_node);
    auto* shadow_render_node = static_cast<Shadow_render_node*>(
        producer_node->get_consumer_input_node(
            erhe::rendergraph::Routing::Resource_provided_by_producer,
            erhe::rendergraph::Rendergraph_node_key::shadow_maps
        )
    );
    if (shadow_render_node == nullptr) {
        return;
    }

    const auto& light_projections           = shadow_render_node->get_light_projections();
    const auto& light_projection_transforms = light_projections.light_projection_transforms;
    const int count = static_cast<int>(light_projection_transforms.size());
    int& light_index = input_texture_node->get_light_index();
    ImGui::Text("Light");
    ImGui::SameLine();
    for (int i = 0; i < count; ++i) {
        const auto& light_projection_transform = light_projection_transforms.at(i);
        if (light_projection_transform.light == nullptr) {
            continue;
        }

        if (i > 0) {
            ImGui::SameLine();
        }
        std::string label = fmt::format("{}", i);
        if (erhe::imgui::make_button(label.c_str(), (light_index == i) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal)) {
            light_index = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", light_projection_transform.light->get_name().c_str());
        }
    }

    const uint32_t shadow_map_texture_handle_uvec2[2] = {
        static_cast<uint32_t>((light_projections.shadow_map_texture_handle & 0xffffffffu)),
        static_cast<uint32_t>( light_projections.shadow_map_texture_handle >> 32u)
    };

    const auto& texture = m_node.get_consumer_input_texture(
        erhe::rendergraph::Routing::Resource_provided_by_producer,
        erhe::rendergraph::Rendergraph_node_key::depth_visualization
    );

    ImGui::Text("Shadow Texture Handle: 0x%08x 0x%08x", shadow_map_texture_handle_uvec2[1], shadow_map_texture_handle_uvec2[0]);
    ImGui::Text("Shadow Texture Handle: %u %u", shadow_map_texture_handle_uvec2[1], shadow_map_texture_handle_uvec2[0]);
    if (light_projections.shadow_map_texture) {
        ImGui::Text(
            "Shadow Texture Name: %u",
            light_projections.shadow_map_texture->gl_name()
        );
    }

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float image_size     = std::min(available_size.x, available_size.y);
    const int   area_size      = static_cast<int>(image_size);
    if (area_size <= 0) {
        return; // Not visible
    }
    m_node.set_area_size(area_size);

    if (!texture) {
        log_render->warn("Debug_view_window has no input render graph node");
        return;
    }

    const int texture_width  = texture->width();
    const int texture_height = texture->height();

    if ((texture_width > 0) && (texture_height > 0)) {
        auto cursor_position = ImGui::GetCursorPos();
        cursor_position.x += (available_size.x - image_size) / 2.0f;
        cursor_position.y += (available_size.y - image_size) / 2.0f;
        ImGui::SetCursorPos(cursor_position);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - drawing image using texture {}", m_texture->gl_name());
        draw_image(texture, area_size, area_size);
        // bool is_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
    } else {
        SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - skipped - no texture or empty size");
    }
    SPDLOG_LOGGER_TRACE(log_render, "Debug_view_window::imgui() - done");
#endif
}

} // namespace editor
