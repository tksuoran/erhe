// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "developer/depth_visualization_window.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_view.hpp"
#include "scene/scene_root.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_scene/scene.hpp"
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
            .color_format         = erhe::dataformat::Format::format_8_vec4_srgb,
            .depth_stencil_format = erhe::dataformat::Format::format_undefined
        }
    }
    , m_forward_renderer  {forward_renderer}
    , m_mesh_memory       {mesh_memory}
    , m_empty_vertex_input{rendergraph.get_graphics_device()}
    , m_pipeline_pass{
        erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Debug_view",
                .shader_stages  = &programs.debug_depth.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        }
    }
{
    // Registered in Texture_rendergraph_node constructor:
    register_input ("shadow_maps",         erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    register_output("depth_visualization", erhe::rendergraph::Rendergraph_node_key::depth_visualization);
}

// Implements erhe::rendergraph::Rendergraph_node
void Depth_to_color_rendergraph_node::execute_rendergraph_node()
{
    SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node()");

    if (!m_render_pass) {
        // Likely because output ImGui window has no viewport size yet.
        return;
    }

    ERHE_PROFILE_FUNCTION();

    Rendergraph_node* input_node = get_consumer_input_node(erhe::rendergraph::Rendergraph_node_key::shadow_maps);
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

    const int input_texture_width  = shadow_texture->get_width ();
    const int input_texture_height = shadow_texture->get_height();

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

    erhe::graphics::Scoped_debug_group pass_scope{"Depth_to_color_rendergraph_node::execute_rendergraph_node()"};

    erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_render_pass->get_render_target_width(),
        .height = m_render_pass->get_render_target_height()
    };
    ERHE_VERIFY(viewport.width >= 0);
    ERHE_VERIFY(viewport.height >= 0);
    ERHE_VERIFY(viewport.width < 32768);
    ERHE_VERIFY(viewport.height < 32768);
    if ((viewport.width == 0) || (viewport.height == 0)) {
        return;
    }

    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();
    erhe::graphics::Render_command_encoder render_encoder = graphics_device.make_render_command_encoder(*m_render_pass.get());

    const auto& light_projection_transforms = light_projections.light_projection_transforms.at(m_light_index);
    const auto& layers = scene_root->layers();

    m_forward_renderer.draw_primitives(
        erhe::scene_renderer::Forward_renderer::Render_parameters{
            .render_encoder        = render_encoder,
            .index_type            = m_mesh_memory.buffer_info.index_type,
            .index_buffer          = nullptr,
            .vertex_buffer0        = nullptr,
            .vertex_buffer1        = nullptr,
            .light_projections     = &light_projections,
            .lights                = layers.light()->lights,
            .materials             = {},
            .mesh_spans            = {},
            .non_mesh_vertex_count = 3, // Full-screen triangle
            .passes                = { &m_pipeline_pass },
            .viewport              = viewport,
            .debug_label           = "Depth_to_color_rendergraph_node::execute_rendergraph_node()"
        },
        light_projection_transforms.light
    );

    SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::render() - done");
}

auto Depth_to_color_rendergraph_node::get_light_index() -> int&
{
    return m_light_index;
}

Depth_visualization_window::Depth_visualization_window(
    erhe::imgui::Imgui_renderer&            imgui_renderer,
    erhe::imgui::Imgui_windows&             imgui_windows,
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    App_context&                            context,
    App_rendering&                          app_rendering,
    Mesh_memory&                            mesh_memory,
    Programs&                               programs
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Depth Visualization", "depth_visualization"}
    , m_context                {context}
{
    if (context.OpenXR || !context.developer_mode) {
        hide_window();
        hidden();
        return;
    }

    m_depth_to_color_node = std::make_unique<Depth_to_color_rendergraph_node>(rendergraph, forward_renderer, mesh_memory, programs);

    const auto& shadow_nodes = app_rendering.get_all_shadow_nodes();
    if (!shadow_nodes.empty()) {
        const std::shared_ptr<Shadow_render_node>& shadow_node = shadow_nodes.front();
        set_shadow_renderer_node(shadow_node);
    }
}

void Depth_visualization_window::set_shadow_renderer_node(const std::shared_ptr<Shadow_render_node>& shadow_node)
{
    if (!m_depth_to_color_node) {
        return;
    }

    erhe::rendergraph::Rendergraph& rendergraph = m_depth_to_color_node->get_rendergraph();

    std::shared_ptr<Shadow_render_node> old_shadow_renderer_node = m_shadow_renderer_node.lock();
    if (old_shadow_renderer_node) {
        rendergraph.disconnect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, old_shadow_renderer_node.get(), m_depth_to_color_node.get());
    }

    m_shadow_renderer_node = shadow_node;
    if (shadow_node) {
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, shadow_node.get(), m_depth_to_color_node.get());
        m_depth_to_color_node->set_enabled(true);
    } else {
        m_depth_to_color_node->set_enabled(true);
    }
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> std::span<const T>
{
    return std::span<const T>(&value, 1);
}

void Depth_visualization_window::hidden()
{
    if (m_depth_to_color_node) {
        m_depth_to_color_node->set_enabled(false);
    }
}

void Depth_visualization_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_depth_to_color_node) {
        return;
    }

    const auto& shadow_nodes = m_context.app_rendering->get_all_shadow_nodes();
    if (!shadow_nodes.empty()) {
        int last_index = static_cast<int>(shadow_nodes.size() - 1);
        const bool edited = ImGui::SliderInt("Viewport", &m_selected_shadow_node, 0, last_index);
        if (edited && (m_selected_shadow_node >= 0) && (m_selected_shadow_node < shadow_nodes.size())) {
            const std::shared_ptr<Shadow_render_node> shadow_node = shadow_nodes.at(m_selected_shadow_node);
            set_shadow_renderer_node(shadow_node);
        }
    }

    const auto* input = m_depth_to_color_node->get_input(erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    if (input == nullptr) {
        log_render->error("Depth_visualization_window has no input pin registered.");
        return;
    }

    if (input->producer_nodes.empty()) {
        log_render->error("Depth_visualization_window input producer is not connected.");
        return;
    }

    // TODO add safety?
    auto* shadow_render_node = static_cast<Shadow_render_node*>(
        m_depth_to_color_node->get_consumer_input_node(erhe::rendergraph::Rendergraph_node_key::shadow_maps)
    );
    if (shadow_render_node == nullptr) {
        log_render->error("Depth_visualization_window input producer is expired or not set.");
        return;
    }

    erhe::scene_renderer::Light_projections& light_projections = shadow_render_node->get_light_projections();
    const auto& light_projection_transforms = light_projections.light_projection_transforms;

    const int count = static_cast<int>(light_projection_transforms.size());
    int& light_index = m_depth_to_color_node->get_light_index();
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

    m_depth_to_color_node->set_enabled(true);

    const auto& texture = m_depth_to_color_node->get_producer_output_texture(erhe::rendergraph::Rendergraph_node_key::depth_visualization);

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float image_size     = std::min(available_size.x, available_size.y);
    const int   area_size      = static_cast<int>(image_size);
    if (area_size <= 0) {
        return; // Not visible
    }
    m_depth_to_color_node->update_render_pass(area_size, area_size, nullptr);

    if (!texture) {
        log_render->warn("Depth_visualization_window has no input render graph node");
        return;
    }

    const int texture_width  = texture->get_width();
    const int texture_height = texture->get_height();

    if ((texture_width > 0) && (texture_height > 0)) {
        auto cursor_position = ImGui::GetCursorPos();
        cursor_position.x += (available_size.x - image_size) / 2.0f;
        cursor_position.y += (available_size.y - image_size) / 2.0f;
        ImGui::SetCursorPos(cursor_position);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::imgui() - drawing image using texture {}", m_texture->gl_name());
        draw_image(m_depth_to_color_node.get(), area_size, area_size);
        // bool is_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
    } else {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::imgui() - skipped - no texture or empty size");
    }
    SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::imgui() - done");
}

}
