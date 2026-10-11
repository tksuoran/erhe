// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "developer/depth_visualization_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_rendering.hpp"
#include "editor_log.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_view.hpp"
#include "scene/scene_root.hpp"
#include "erhe_utility/thread_roles.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
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
    erhe::scene_renderer::Mesh_memory&      mesh_memory,
    Programs&                               programs
)
    : erhe::rendergraph::Texture_rendergraph_node{
        erhe::rendergraph::Texture_rendergraph_node_create_info{
            .rendergraph          = rendergraph,
            .debug_label          = erhe::utility::Debug_label{"Depth_to_color_rendergraph_node"},
            .output_key           = erhe::rendergraph::Rendergraph_node_key::depth_visualization,
            .color_format         = erhe::dataformat::Format::format_8_vec4_srgb,
            .depth_stencil_format = erhe::dataformat::Format::format_undefined
        }
    }
    , m_forward_renderer  {forward_renderer}
    , m_mesh_memory       {mesh_memory}
    , m_programs          {programs}
    , m_render_pipeline{
        rendergraph.get_graphics_device(),
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Debug_view"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled
        }
    }
    ,m_render_pipelines{&m_render_pipeline}
{
    register_input("shadow_maps", erhe::rendergraph::Rendergraph_node_key::shadow_maps);
}

// Implements erhe::rendergraph::Rendergraph_node
void Depth_to_color_rendergraph_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node()");

    // The size Depth_visualization_window::update() requested for this frame.
    apply_requested_size();
    if (m_render_target.get_render_pass() == nullptr) {
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
    // The render phase's copy of this frame's scene root.
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    const std::shared_ptr<Scene_root> scene_root = scene_view.get_render_scene_root();
    if (!scene_root) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::execute_rendergraph_node() - skipped: no shadow scene root");
        return;
    }

    // The shadow node's result of this frame (it executed earlier in it).
    const erhe::scene_renderer::Light_projections& light_projections = shadow_render_node->get_light_projections(m_rendergraph.get_graphics_device().get_frame_index());
    if (light_projections.light_projection_transforms.empty()) {
        return;
    }

    if (static_cast<std::size_t>(m_light_index) >= light_projections.light_projection_transforms.size()) {
        SPDLOG_LOGGER_TRACE(log_render, "Depth_to_color_rendergraph_node::rendexecute_rendergraph_nodeer() - skipped: invalid selected light index");
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{
        command_buffer,
        "Depth_to_color_rendergraph_node::execute_rendergraph_node()"
    };

    erhe::graphics::Render_pass* render_pass = m_render_target.get_render_pass();
    erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = render_pass->get_render_target_width(),
        .height = render_pass->get_render_target_height()
    };
    ERHE_VERIFY(viewport.width >= 0);
    ERHE_VERIFY(viewport.height >= 0);
    ERHE_VERIFY(viewport.width < 32768);
    ERHE_VERIFY(viewport.height < 32768);
    if ((viewport.width == 0) || (viewport.height == 0)) {
        return;
    }

    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();
    erhe::graphics::Render_command_encoder render_encoder = graphics_device.make_render_command_encoder(command_buffer);
    erhe::graphics::Scoped_render_pass scoped_render_pass{*render_pass, command_buffer};

    const auto& light_projection_transforms = light_projections.light_projection_transforms.at(m_light_index);

    m_forward_renderer.draw_primitives(
        erhe::scene_renderer::Forward_renderer::Primitive_render_parameters{
            .base = erhe::scene_renderer::Forward_renderer::Base_render_parameters{
                .render_encoder    = render_encoder,
                .render_pass       = m_render_target.get_render_pass(),
                .viewport          = viewport,
                .light_projections = &light_projections,
                // No materials: the shared empty set (D8).
                .material_source   = nullptr,
                .debug_label       = "Depth_to_color_rendergraph_node::execute_rendergraph_node()"
            },
            .vertex_count          = 3, // Full-screen triangle
            .base_render_pipeline  = m_render_pipeline,
            .shader_stages         = &m_programs.depth_to_color.shader_stages

        },
        light_projection_transforms.light
    );

    SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::render() - done");
}


Depth_visualization_window::Depth_visualization_window(
    erhe::imgui::Imgui_renderer&            imgui_renderer,
    erhe::imgui::Imgui_windows&             imgui_windows,
    erhe::rendergraph::Rendergraph&         rendergraph,
    erhe::scene_renderer::Forward_renderer& forward_renderer,
    App_context&                            context,
    App_message_bus&                        app_message_bus,
    erhe::scene_renderer::Mesh_memory&      mesh_memory,
    Programs&                               programs
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Depth Visualization", "depth_visualization", true}
    , m_context                {context}
{
    if (context.OpenXR || !context.developer_mode) {
        hide_window();
        hidden();
        return;
    }

    m_depth_to_color_node = erhe::rendergraph::make_rendergraph_node<Depth_to_color_rendergraph_node>(rendergraph, forward_renderer, mesh_memory, programs);
    m_depth_to_color_node->set_enabled(false);

    // Connect depth-to-color output to the Window_imgui_host so
    // topological sort schedules it before the swapchain render pass.
    // The ImGui host displays the depth visualization texture via
    // Depth_visualization_window::imgui(), but without this explicit
    // render graph edge the sort cannot infer the dependency.
    const std::shared_ptr<erhe::imgui::Window_imgui_host>& window_imgui_host = imgui_windows.get_window_imgui_host();
    if (window_imgui_host) {
        window_imgui_host->register_input("depth visualization", erhe::rendergraph::Rendergraph_node_key::depth_visualization);
        rendergraph.connect(
            erhe::rendergraph::Rendergraph_node_key::depth_visualization,
            m_depth_to_color_node.get(),
            window_imgui_host.get()
        );
    }

    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(message.scene_root);
        }
    );
}

void Depth_visualization_window::on_close_scene(const std::shared_ptr<Scene_root>& scene_root)
{
    if (!scene_root || (m_scene_root.lock() != scene_root)) {
        return;
    }
    // The closed scene's views are unbound but their shadow nodes stay, and
    // the depth-to-color texture still holds its last image. Drop the scene
    // and the input so neither is shown.
    m_scene_root.reset();
    set_shadow_renderer_node({});
}

void Depth_visualization_window::set_shadow_renderer_node(const std::shared_ptr<Shadow_render_node>& shadow_node)
{
    // ImGui time (and scene close): requested; update() connects it.
    m_requested_shadow_node       = shadow_node;
    m_shadow_node_request_pending = true;
}

void Depth_visualization_window::update()
{
    if (!m_depth_to_color_node) {
        return;
    }
    if (m_shadow_node_request_pending) {
        m_shadow_node_request_pending = false;
        apply_shadow_renderer_node(m_requested_shadow_node.lock());
    }
    const std::shared_ptr<Shadow_render_node> shadow_node = m_shadow_renderer_node.lock();
    m_depth_to_color_node->set_enabled(m_requested_enabled && static_cast<bool>(shadow_node));
    m_depth_to_color_node->set_light_index(m_light_index);
    if (m_requested_area_size > 0) {
        m_depth_to_color_node->request_size(m_requested_area_size, m_requested_area_size);
    }
}

void Depth_visualization_window::apply_shadow_renderer_node(const std::shared_ptr<Shadow_render_node>& shadow_node)
{
    if (!m_depth_to_color_node) {
        return;
    }

    erhe::rendergraph::Rendergraph& rendergraph = m_depth_to_color_node->get_rendergraph();

    std::shared_ptr<Shadow_render_node> old_shadow_renderer_node = m_shadow_renderer_node.lock();
    if (old_shadow_renderer_node == shadow_node) {
        return;
    }
    if (old_shadow_renderer_node) {
        rendergraph.disconnect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, old_shadow_renderer_node.get(), m_depth_to_color_node.get());
    }

    m_shadow_renderer_node = shadow_node;
    if (shadow_node) {
        rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, shadow_node.get(), m_depth_to_color_node.get());
    }
}

auto Depth_visualization_window::find_first_shadow_node(const Scene_root* const scene_root) const -> std::shared_ptr<Shadow_render_node>
{
    if (scene_root == nullptr) {
        return {};
    }
    for (const std::shared_ptr<Shadow_render_node>& shadow_node : m_context.app_rendering->get_all_shadow_nodes()) {
        if (shadow_node->get_scene_view().get_scene_root().get() == scene_root) {
            return shadow_node;
        }
    }
    return {};
}

namespace {

[[nodiscard]] auto get_view_label(const Shadow_render_node& shadow_node) -> const char*
{
    const std::string& settings_key = shadow_node.get_scene_view().get_settings_key();
    return settings_key.empty() ? "(preview)" : settings_key.c_str();
}

}

auto Depth_visualization_window::view_combo(const Scene_root* const scene_root) -> Shadow_render_node*
{
    const std::shared_ptr<Shadow_render_node> current = m_shadow_renderer_node.lock();
    const char* const preview = current ? get_view_label(*current) : "(none)";
    if (ImGui::BeginCombo("View", preview)) {
        int id = 0;
        for (const std::shared_ptr<Shadow_render_node>& shadow_node : m_context.app_rendering->get_all_shadow_nodes()) {
            if (shadow_node->get_scene_view().get_scene_root().get() != scene_root) {
                continue;
            }
            ImGui::PushID(id++);
            const bool is_selected = (shadow_node == current);
            if (ImGui::Selectable(get_view_label(*shadow_node), is_selected)) {
                set_shadow_renderer_node(shadow_node);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const std::shared_ptr<Shadow_render_node> selected = m_shadow_renderer_node.lock();
    return selected.get();
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> std::span<const T>
{
    return std::span<const T>(&value, 1);
}

void Depth_visualization_window::hidden()
{
    m_requested_enabled = false; // applied by update()
}

void Depth_visualization_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_depth_to_color_node) {
        return;
    }

    std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (m_context.app_scenes->scene_combo("Scene", scene_root, false)) {
        m_scene_root = scene_root;
        set_shadow_renderer_node(find_first_shadow_node(scene_root.get()));
    }
    if (!scene_root) {
        set_shadow_renderer_node({});
        ImGui::TextUnformatted("Select a scene");
        return;
    }

    // Views are created, destroyed and rebound to other scenes independently
    // of this window: validate the input on access and follow to another view
    // of the selected scene when it no longer shows that scene.
    {
        const std::shared_ptr<Shadow_render_node> current = m_shadow_renderer_node.lock();
        if (!current || (current->get_scene_view().get_scene_root() != scene_root)) {
            set_shadow_renderer_node(find_first_shadow_node(scene_root.get()));
        }
    }

    Shadow_render_node* const shadow_render_node = view_combo(scene_root.get());
    if (shadow_render_node == nullptr) {
        ImGui::TextUnformatted("The scene is not shown in any view");
        return;
    }

    // The view's light slots as the scene's light set resolved them in the
    // prepare phase of this frame (slot i of the set is light projection i;
    // the main thread reads the prepare frame's set, which no render phase
    // writes). A view lit by the shadow node's headlight has an empty scene
    // set and shows no light here.
    const std::vector<erhe::scene::Light_snapshot>& lights = scene_root->get_light_set(m_context.graphics_device->get_frame_index()).get_lights();

    // Depth_to_color_rendergraph_node skips rendering without a valid light,
    // leaving its texture holding the image of the previous view / scene.
    const int count = static_cast<int>(lights.size());
    if (count == 0) {
        ImGui::TextUnformatted("The view has no lights");
        return;
    }
    int& light_index = m_light_index; // applied to the node by update()
    if ((light_index < 0) || (light_index >= count)) {
        light_index = 0;
    }
    ImGui::Text("Light");
    ImGui::SameLine();
    for (int i = 0; i < count; ++i) {
        const erhe::scene::Light_snapshot& light = lights.at(static_cast<std::size_t>(i));

        if (i > 0) {
            ImGui::SameLine();
        }
        std::string label = fmt::format("{}", i);
        if (erhe::imgui::make_button(label.c_str(), (light_index == i) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal)) {
            light_index = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", light.get_name().c_str());
        }
    }

    m_requested_enabled = true; // applied by update()

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float image_size     = std::min(available_size.x, available_size.y);
    const int   area_size      = static_cast<int>(image_size);
    if (area_size <= 0) {
        return; // Not visible
    }
    m_requested_area_size = area_size; // applied by update()

    // The node itself is the texture reference: the ImGui renderer resolves
    // it to the node's output texture when it renders, in the render phase,
    // which is the phase that sizes (and replaces) that texture.
    {
        auto cursor_position = ImGui::GetCursorPos();
        cursor_position.x += (available_size.x - image_size) / 2.0f;
        cursor_position.y += (available_size.y - image_size) / 2.0f;
        ImGui::SetCursorPos(cursor_position);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::imgui() - drawing image using texture {}", m_texture->gl_name());
        draw_image(
            std::static_pointer_cast<erhe::graphics::Texture_reference>(m_depth_to_color_node),
            area_size,
            area_size
        );
        // bool is_hovered = ImGui::IsItemHovered();
        ImGui::PopStyleVar();
    }
    SPDLOG_LOGGER_TRACE(log_render, "Depth_visualization_window::imgui() - done");
}

}
