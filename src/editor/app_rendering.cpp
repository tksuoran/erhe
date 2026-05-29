#include "app_rendering.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "tools/tools.hpp"
#include "renderable.hpp"
#include "renderers/composer.hpp"
#include "renderers/id_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/viewport_config.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_math/math_util.hpp"
#if defined(ERHE_GRAPHICS_API_OPENGL)
#   include "erhe_gl/wrapper_functions.hpp"
#   include "erhe_gl/enum_bit_mask_operators.hpp"
#endif
#include "erhe_graphics/swapchain.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"

namespace editor {

using erhe::graphics::Color_blend_state;


#pragma region Commands
Capture_frame_command::Capture_frame_command(erhe::commands::Commands& commands, App_context& app_context)
    : Command  {commands, "editor.capture_frame"}
    , m_context{app_context}
{
}

auto Capture_frame_command::try_call() -> bool
{
    m_context.app_rendering->trigger_capture();
    return true;
}
#pragma endregion Commands


App_rendering::App_rendering(
    erhe::commands::Commands&          commands,
    erhe::graphics::Device&            graphics_device,
    App_context&                       app_context,
    App_message_bus&                   app_message_bus,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    Programs&                          programs
)
    : m_context              {app_context}
    , m_capture_frame_command{commands, app_context}
    , m_pipeline_passes      {graphics_device, mesh_memory, programs, graphics_device.get_reverse_depth()}
    , m_composer             {"Main Composer"}
{
    ERHE_PROFILE_FUNCTION();

    commands.register_command(&m_capture_frame_command);
    commands.bind_command_to_key(&m_capture_frame_command, erhe::window::Key_f10);
    commands.bind_command_to_menu(&m_capture_frame_command, "View.Frame");

    using Item_filter = erhe::Item_filter;
    using Item_flags  = erhe::Item_flags;
    using namespace erhe::primitive;
    const Item_filter filter_not_selected{
        .require_all_bits_set         = Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::content  | Item_flags::controller | Item_flags::rendertarget,
        .require_all_bits_clear       = Item_flags::selected | Item_flags::hovered_in_item_tree
    };
    const Item_filter filter_not_selected_positive_determinant{
        .require_all_bits_set         = Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::content  | Item_flags::controller | Item_flags::rendertarget,
        .require_all_bits_clear       = Item_flags::selected | Item_flags::hovered_in_item_tree | Item_flags::negative_determinant
    };
    const Item_filter filter_not_selected_negative_determinant{
        .require_all_bits_set         = Item_flags::visible  | Item_flags::negative_determinant,
        .require_at_least_one_bit_set = Item_flags::content  | Item_flags::controller | Item_flags::rendertarget,
        .require_all_bits_clear       = Item_flags::selected | Item_flags::hovered_in_item_tree
    };
    const Item_filter filter_selected{
        .require_all_bits_set         = Item_flags::content | Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::selected,
        .require_all_bits_clear       = 0
    };
    const Item_filter filter_selected_positive_determinant{
        .require_all_bits_set         = Item_flags::content | Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::selected,
        .require_all_bits_clear       = Item_flags::negative_determinant
    };
    const Item_filter filter_selected_negative_determinant{
        .require_all_bits_set         = Item_flags::content | Item_flags::visible | Item_flags::negative_determinant,
        .require_at_least_one_bit_set = Item_flags::selected,
        .require_all_bits_clear       = 0
    };
    const Item_filter filter_selected_or_hovered{
        .require_all_bits_set         = Item_flags::content  | Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::selected | Item_flags::hovered_in_item_tree,
        .require_all_bits_clear       = 0
    };
    const Item_filter filter_selected_or_hovered_positive_determinant{
        .require_all_bits_set         = Item_flags::content  | Item_flags::visible,
        .require_at_least_one_bit_set = Item_flags::selected | Item_flags::hovered_in_item_tree,
        .require_all_bits_clear       = Item_flags::negative_determinant
    };
    const Item_filter filter_selected_or_hovered_negative_determinant{
        .require_all_bits_set         = Item_flags::content  | Item_flags::visible | Item_flags::negative_determinant,
        .require_at_least_one_bit_set = Item_flags::selected | Item_flags::hovered_in_item_tree,
        .require_all_bits_clear       = 0
    };

    const auto& render_style_not_selected = [](const Render_context& context) -> const Render_style_data& {
        return context.viewport_config.render_style_not_selected;
    };

    using namespace erhe::primitive;
    using namespace erhe::scene_renderer;
    using Blend_mode = erhe::renderer::Blend_mode;
    static constexpr bool selected = true;
    static constexpr bool not_selected = false;
    static constexpr bool negative_determinant = true;
    static constexpr bool positive_determinant = false;

    auto content_fill_not_selected_positive_determinant = make_composition_pass(
        "Content fill opaque not selected positive determinant",
        Composition_pass_data{
            .mesh_layers          {Mesh_layer_id::content, Mesh_layer_id::controller, Mesh_layer_id::rendertarget},
            .blending_mode_policy {Blending_mode_policy::opaque_primitives_only},
            .primitive_mode       {Primitive_mode::polygon_fill},
            .filter               {filter_not_selected_positive_determinant},
            .get_render_style     {render_style_not_selected},
        },
        not_selected, positive_determinant
    );

    auto content_fill_not_selected_negative_determinant = make_composition_pass(
        "Content fill opaque not selected negative determinant",
        Composition_pass_data{
            .mesh_layers         {Mesh_layer_id::content, Mesh_layer_id::controller, Mesh_layer_id::rendertarget},
            .blending_mode_policy{Blending_mode_policy::opaque_primitives_only},
            .primitive_mode      {Primitive_mode::polygon_fill},
            .filter              {filter_not_selected_negative_determinant},
            .get_render_style    {render_style_not_selected},
        },
        not_selected, negative_determinant
    );

    const auto& render_style_selected = [](const Render_context& context) -> const Render_style_data& {
        return context.viewport_config.render_style_selected;
    };

    auto content_fill_selected_or_hovered_filter_positive_determinant = make_composition_pass(
        "Content fill selected positive determinant",
        Composition_pass_data{
            .mesh_layers         {Mesh_layer_id::content, Mesh_layer_id::controller},
            .blending_mode_policy{Blending_mode_policy::opaque_primitives_only},
            .primitive_mode      {Primitive_mode::polygon_fill},
            .filter              {filter_selected_or_hovered_positive_determinant},
            .get_render_style    {render_style_selected}
        },
        selected, positive_determinant
    );

    auto content_fill_selected_or_hovered_filter_negative_determinant = make_composition_pass(
        "Content fill selected negative determinant",
        Composition_pass_data{
            .mesh_layers         {Mesh_layer_id::content, Mesh_layer_id::controller},
            .blending_mode_policy{Blending_mode_policy::opaque_primitives_only},
            .primitive_mode      {Primitive_mode::polygon_fill},
            .filter              {filter_selected_or_hovered_negative_determinant},
            .get_render_style    {render_style_selected}
        },
        selected, negative_determinant
    );

    const bool use_compute_wide_lines = graphics_device.get_info().use_compute_shader;

    // On backends without compute (macOS GL 4.1) the edge_lines passes go
    // through Forward_renderer instead of Content_wide_line_renderer; in
    // that path the variant cache would pick the standard mesh shader,
    // which has no line-expansion logic. Override with the wide_lines
    // geometry-shader program (loaded in programs.cpp only on the same
    // !use_compute_shader condition).
    erhe::graphics::Shader_stages* const wide_lines_shader_stages = use_compute_wide_lines
        ? nullptr
        : &programs.wide_lines_draw_color.shader_stages;

    edge_lines_not_selected = make_composition_pass(
        "Content edge lines not selected",
        Composition_pass_data{
            .use_content_wide_line_renderer{use_compute_wide_lines},
            .content_wide_line_group       {0},
            .mesh_layers                   {Mesh_layer_id::content},
            .blending_mode_policy          {Blending_mode_policy::override_with_base_render_pipeline},
            .primitive_mode                {Primitive_mode::edge_lines},
            .filter                        {filter_not_selected},
            .shader_stages                 {wide_lines_shader_stages},
            .get_render_style              {render_style_not_selected}
        }, not_selected, positive_determinant
    );

    edge_lines_selected = make_composition_pass(
        "Content edge lines opaque selected",
        Composition_pass_data{
            .use_content_wide_line_renderer{use_compute_wide_lines},
            .content_wide_line_group       {1},
            .mesh_layers                   {Mesh_layer_id::content},
            .blending_mode_policy          {Blending_mode_policy::override_with_base_render_pipeline},
            .primitive_mode                {Primitive_mode::edge_lines},
            .filter                        {filter_selected},
            .shader_stages                 {wide_lines_shader_stages},
            .get_render_style              {render_style_selected}
        }, selected, positive_determinant
    );

    selection_outline = make_composition_pass(
        "Content outline opaque selected",
        Composition_pass_data{
            .use_content_wide_line_renderer{use_compute_wide_lines},
            .content_wide_line_group       {2},
            .mesh_layers                   {Mesh_layer_id::content},
            .blending_mode_policy          {Blending_mode_policy::override_with_base_render_pipeline},
            .primitive_mode                {Primitive_mode::edge_lines},
            .filter                        {filter_selected_or_hovered},
            .shader_stages                 {wide_lines_shader_stages},
            .primitive_settings{
                erhe::scene_renderer::Primitive_interface_settings{
                    .constant_color0 = glm::vec4{1.0f, 0.75f, 0.0f, 1.0f},
                    .constant_color1 = glm::vec4{0.0f, 0.0f,  1.0f, 1.0f},
                    .constant_size   = -5.0f
                }
            }
        },
        { &m_pipeline_passes.outline }
    );

    // This gets overridden in Composition_pass::render()
    // TODO Figure out a good way to route the settings

    auto sky = make_composition_pass(
        "Sky",
        Composition_pass_data{
            .non_mesh_vertex_count{3}, // Fullscreen quad
            .primitive_mode{erhe::primitive::Primitive_mode::polygon_fill},
            .filter{
                .require_all_bits_set         = 0,
                .require_at_least_one_bit_set = 0,
                .require_all_bits_clear       = 0
            },
            .shader_stages{&programs.sky.shader_stages}
        },
        { &m_pipeline_passes.sky }
    );

    // Infinite plane with 4 triangles / 12 indices - https://stackoverflow.com/questions/12965161/rendering-infinitely-large-plane
    m_grid_composition_pass = make_composition_pass(
        "Grid",
        Composition_pass_data{
            .non_mesh_vertex_count{12},
            .primitive_mode{erhe::primitive::Primitive_mode::polygon_fill},
            .filter{
                .require_all_bits_set         = 0,
                .require_at_least_one_bit_set = 0,
                .require_all_bits_clear       = 0
            },
            .shader_stages{&programs.grid.shader_stages}
        },
        { &m_pipeline_passes.grid }
    );

    auto translucent_content_fill_not_selected_positive_determinant = make_composition_pass(
        "Content fill translucent not selected positive determinant",
        content_fill_not_selected_positive_determinant,
        erhe::scene_renderer::Blending_mode_policy::translucent_primitives_only
    );

    auto translucent_content_fill_not_selected_negative_determinant = make_composition_pass(
        "Content fill translucent not selected negative determinant",
        content_fill_not_selected_negative_determinant,
        erhe::scene_renderer::Blending_mode_policy::translucent_primitives_only
    );

    auto translucent_content_fill_selected_or_hovered_filter_positive_determinant = make_composition_pass(
        "Content fill translucent selected positive determinant",
        content_fill_selected_or_hovered_filter_positive_determinant,
        erhe::scene_renderer::Blending_mode_policy::translucent_primitives_only
    );

    auto translucent_content_fill_selected_or_hovered_filter_negative_determinant = make_composition_pass(
        "Content fill translucent selected negative determinant",
        content_fill_selected_or_hovered_filter_negative_determinant,
        erhe::scene_renderer::Blending_mode_policy::translucent_primitives_only
    );

    auto brush = make_composition_pass(
        "Brush",
        Composition_pass_data{
            .mesh_layers         {Mesh_layer_id::brush},
            .blending_mode_policy{Blending_mode_policy::override_with_base_render_pipeline},
            .primitive_mode      {erhe::primitive::Primitive_mode::polygon_fill},
            .filter{
                .require_all_bits_set         = Item_flags::visible | Item_flags::brush,
                .require_at_least_one_bit_set = 0,
                .require_all_bits_clear       = 0
            },
            .shader_key_force_enable_mask{
                erhe::scene_renderer::make_shader_bool_mask(erhe::scene_renderer::Shader_bool::VARIANT_BRUSH_PREVIEW)
            }
        },
        {
            &m_pipeline_passes.brush_back,
            &m_pipeline_passes.brush_front
        }
    );

    m_graphics_settings_subscription = app_message_bus.graphics_settings.subscribe(
        [&](Graphics_settings_message& message) {
            handle_graphics_settings_changed(message.graphics_preset);
        }
    );

    debug_joint_colors.push_back(glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}); //  0
    debug_joint_colors.push_back(glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}); //  1
    debug_joint_colors.push_back(glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}); //  2
    debug_joint_colors.push_back(glm::vec4{0.0f, 0.0f, 1.0f, 1.0f}); //  3
    debug_joint_colors.push_back(glm::vec4{1.0f, 1.0f, 0.0f, 1.0f}); //  4
    debug_joint_colors.push_back(glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}); //  5
    debug_joint_colors.push_back(glm::vec4{1.0f, 0.0f, 1.0f, 1.0f}); //  6
    debug_joint_colors.push_back(glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}); //  7
    debug_joint_colors.push_back(glm::vec4{0.5f, 0.0f, 0.0f, 1.0f}); //  8
    debug_joint_colors.push_back(glm::vec4{0.0f, 0.5f, 0.0f, 1.0f}); //  9
    debug_joint_colors.push_back(glm::vec4{0.0f, 0.0f, 0.5f, 1.0f}); // 10
    debug_joint_colors.push_back(glm::vec4{0.5f, 0.5f, 0.0f, 1.0f}); // 11
    debug_joint_colors.push_back(glm::vec4{0.0f, 0.5f, 0.5f, 1.0f}); // 12
    debug_joint_colors.push_back(glm::vec4{0.5f, 0.0f, 0.5f, 1.0f}); // 13
    debug_joint_colors.push_back(glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}); // 14
    debug_joint_colors.push_back(glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}); // 15
    debug_joint_colors.push_back(glm::vec4{1.0f, 0.0f, 0.5f, 1.0f}); // 16
    debug_joint_colors.push_back(glm::vec4{0.5f, 1.0f, 0.0f, 1.0f}); // 17
    debug_joint_colors.push_back(glm::vec4{0.0f, 1.0f, 0.5f, 1.0f}); // 18
    debug_joint_colors.push_back(glm::vec4{0.5f, 0.0f, 1.0f, 1.0f}); // 19
    debug_joint_colors.push_back(glm::vec4{0.0f, 0.5f, 1.0f, 1.0f}); // 20
    debug_joint_colors.push_back(glm::vec4{1.0f, 1.0f, 0.5f, 1.0f}); // 21
    debug_joint_colors.push_back(glm::vec4{0.5f, 1.0f, 1.0f, 1.0f}); // 22
    debug_joint_colors.push_back(glm::vec4{1.0f, 0.5f, 1.0f, 1.0f}); // 23
}

auto App_rendering::create_shadow_node_for_scene_view(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    App_settings&                   app_settings,
    Scene_view&                     scene_view
) -> std::shared_ptr<Shadow_render_node>
{
    const auto& preset        = app_settings.graphics.current_graphics_preset;
    const int   resolution    = preset.shadow_enable ? preset.shadow_resolution  : 1;
    const int   light_count   = preset.shadow_enable ? preset.shadow_light_count : 1;
    log_startup->info(
        "Creating shadow render node from preset '{}': shadow_enable={} -> light_count={} resolution={} depth_bits={}",
        preset.name, preset.shadow_enable, light_count, resolution, preset.shadow_depth_bits
    );
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    auto shadow_render_node = std::make_shared<Shadow_render_node>(
        graphics_device,
        *m_context.current_command_buffer,
        rendergraph,
        m_context,
        scene_view,
        resolution,
        light_count,
        preset.shadow_depth_bits
    );
    m_all_shadow_render_nodes.push_back(shadow_render_node);
    return shadow_render_node;
}

void App_rendering::handle_graphics_settings_changed(Graphics_preset_entry* graphics_preset)
{
    const int resolution  = (graphics_preset != nullptr) && graphics_preset->shadow_enable ? graphics_preset->shadow_resolution  : 1;
    const int light_count = (graphics_preset != nullptr) && graphics_preset->shadow_enable ? graphics_preset->shadow_light_count : 1;

    if (graphics_preset != nullptr) {
        log_startup->info(
            "Reconfiguring {} shadow render node(s) from preset '{}': shadow_enable={} -> light_count={} resolution={} depth_bits={}",
            m_all_shadow_render_nodes.size(),
            graphics_preset->name, graphics_preset->shadow_enable, light_count, resolution, graphics_preset->shadow_depth_bits
        );
    }

    // Reverse-Z is a static device property (Device::get_reverse_depth()), not a
    // runtime-mutable setting, so depth state is baked at construction and there
    // is nothing to rebuild here. clip_control is likewise set once at device /
    // editor init to match native_depth_range (zero_to_one when supported); it
    // must not be toggled per preset.
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    for (const auto& node : m_all_shadow_render_nodes) {
        node->reconfigure(*m_context.graphics_device, *m_context.current_command_buffer, resolution, light_count, graphics_preset->shadow_depth_bits);
    }
}

auto App_rendering::get_shadow_node_for_view(const Scene_view& scene_view) -> std::shared_ptr<Shadow_render_node>
{
    auto i = std::find_if(
        m_all_shadow_render_nodes.begin(),
        m_all_shadow_render_nodes.end(),
        [&scene_view](const auto& entry) {
            return &entry->get_scene_view() == &scene_view;
        }
    );
    if (i == m_all_shadow_render_nodes.end()) {
        return {};
    }
    return *i;
}

auto App_rendering::get_all_shadow_nodes() -> const std::vector<std::shared_ptr<Shadow_render_node>>&
{
    return m_all_shadow_render_nodes;
}

auto App_rendering::destroy_shadow_node(const std::shared_ptr<Shadow_render_node>& shadow_render_node) -> bool
{
    if (!shadow_render_node) {
        return false;
    }
    const auto i = std::find(m_all_shadow_render_nodes.begin(), m_all_shadow_render_nodes.end(), shadow_render_node);
    if (i == m_all_shadow_render_nodes.end()) {
        return false;
    }
    m_all_shadow_render_nodes.erase(i);
    return true;
}

auto App_rendering::get_render_pipeline_state(
    const Composition_pass& composition_pass,
    const bool              selected,
    const bool              negative_determinant
) -> erhe::graphics::Base_render_pipeline*
{
    using namespace erhe::primitive;
    switch (composition_pass.data.primitive_mode) {
        case Primitive_mode::polygon_fill:
            return selected
                ? (negative_determinant
                    ? &m_pipeline_passes.polygon_fill_standard_selected_negative_determinant
                    : &m_pipeline_passes.polygon_fill_standard_selected_positive_determinant
                )
                : (negative_determinant
                    ? &m_pipeline_passes.polygon_fill_standard_negative_determinant
                    : &m_pipeline_passes.polygon_fill_standard_positive_determinant
                );

        case Primitive_mode::edge_lines:
            return &m_pipeline_passes.edge_lines;

        case Primitive_mode::corner_points    : return &m_pipeline_passes.corner_points;
        case Primitive_mode::corner_normals   : return &m_pipeline_passes.edge_lines;
        case Primitive_mode::polygon_centroids: return &m_pipeline_passes.polygon_centroids;
        default: return nullptr;
    }
}

auto App_rendering::make_composition_pass(const std::string_view name) -> std::shared_ptr<Composition_pass>
{
    auto renderpass = std::make_shared<Composition_pass>(name);
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_composer.mutex};
    m_composer.composition_passes.push_back(renderpass);
    return renderpass;
}

auto App_rendering::make_composition_pass(
    std::string_view        name,
    Composition_pass_data&& data,
    const bool              selected,
    const bool              negative_determinant
) -> std::shared_ptr<Composition_pass>
{
    std::shared_ptr<Composition_pass> renderpass = make_composition_pass(name);
    renderpass->data = std::move(data);
    renderpass->data.base_render_pipelines.push_back(
        get_render_pipeline_state(*renderpass.get(), selected, negative_determinant)
    );
    return renderpass;
}

auto App_rendering::make_composition_pass(
    std::string_view                                             name,
    Composition_pass_data&&                                      data,
    std::initializer_list<erhe::graphics::Base_render_pipeline*> pipelines
) -> std::shared_ptr<Composition_pass>
{
    std::shared_ptr<Composition_pass> renderpass = make_composition_pass(name);
    renderpass->data = std::move(data);
    renderpass->data.base_render_pipelines = pipelines;
    return renderpass;
}

auto App_rendering::make_composition_pass(
    std::string_view                           name,
    const std::shared_ptr<Composition_pass>&   base_pass,
    erhe::scene_renderer::Blending_mode_policy blending_mode_policy
) -> std::shared_ptr<Composition_pass>
{
    std::shared_ptr<Composition_pass> renderpass = make_composition_pass(name);
    renderpass->data = base_pass->data;
    renderpass->data.blending_mode_policy = blending_mode_policy;
    return renderpass;
}

auto App_rendering::composition_passes() const -> const std::vector<std::shared_ptr<Composition_pass>>&
{
    return m_composer.composition_passes;
}

using Vertex_input_state         = erhe::graphics::Vertex_input_state;
using Input_assembly_state       = erhe::graphics::Input_assembly_state;
using Multisample_state          = erhe::graphics::Multisample_state;
using Viewport_depth_range_state = erhe::graphics::Viewport_depth_range_state;
using Rasterization_state        = erhe::graphics::Rasterization_state;
using Depth_stencil_state        = erhe::graphics::Depth_stencil_state;
using Color_blend_state          = erhe::graphics::Color_blend_state;

Pipeline_renderpasses::Pipeline_renderpasses(
    erhe::graphics::Device&            graphics_device,
    erhe::scene_renderer::Mesh_memory& /*mesh_memory*/,
    Programs&                          /*programs*/,
    const bool                         reverse_depth
)
    : m_y_flip{graphics_device.get_info().coordinate_conventions.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled}
    , m_empty_vertex_input{graphics_device}
    , polygon_fill_standard_positive_determinant{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Polygon Fill Positive Determinant"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth)
        }
    }
    , polygon_fill_standard_negative_determinant{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Polygon Fill Negative Determinant"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_cw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        }
    }
    , polygon_fill_standard_selected_positive_determinant{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Polygon Fill Selected Positive Determinant"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, reverse_depth),
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::replace,
                    .z_fail_op       = erhe::graphics::Stencil_op::replace,
                    .z_pass_op       = erhe::graphics::Stencil_op::replace,
                    .function        = erhe::graphics::Compare_operation::always,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b00000000u, // always does not use
                    .write_mask      = 0b10000000u  // = 0x80 = 128
                },
                .stencil_back = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::replace,
                    .z_fail_op       = erhe::graphics::Stencil_op::replace,
                    .z_pass_op       = erhe::graphics::Stencil_op::replace,
                    .function        = erhe::graphics::Compare_operation::always,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b00000000u,
                    .write_mask      = 0b10000000u
                },
            }
        }
    }
    , polygon_fill_standard_selected_negative_determinant{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Polygon Fill Selected Negative Determinant"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_cw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, reverse_depth),
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::replace,
                    .z_fail_op       = erhe::graphics::Stencil_op::replace,
                    .z_pass_op       = erhe::graphics::Stencil_op::replace,
                    .function        = erhe::graphics::Compare_operation::always,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b00000000u, // always does not use
                    .write_mask      = 0b10000000u  // = 0x80 = 128
                },
                .stencil_back = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::replace,
                    .z_fail_op       = erhe::graphics::Stencil_op::replace,
                    .z_pass_op       = erhe::graphics::Stencil_op::replace,
                    .function        = erhe::graphics::Compare_operation::always,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b00000000u,
                    .write_mask      = 0b10000000u
                },
            }
        }
    }

    // RGB factors use CONSTANT_COLOR rather than CONSTANT_ALPHA because
    // VK_KHR_portability_subset on MoltenVK rejects CONSTANT_ALPHA in the
    // color channel (VUID-...-04454). Blend constant's RGB is set equal to
    // its alpha so CONSTANT_COLOR yields the same result as CONSTANT_ALPHA.
    , line_hidden_blend_state{
        .enabled                = true,
        .rgb = {
            .equation_mode      = erhe::graphics::Blend_equation_mode::func_add,
            .source_factor      = erhe::graphics::Blending_factor::constant_color,
            .destination_factor = erhe::graphics::Blending_factor::one_minus_constant_color
        },
        .alpha = {
            .equation_mode      = erhe::graphics::Blend_equation_mode::func_add,
            .source_factor      = erhe::graphics::Blending_factor::constant_alpha,
            .destination_factor = erhe::graphics::Blending_factor::one_minus_constant_alpha
        },
        .constant = { 0.2f, 0.2f, 0.2f, 0.2f }
    }
    , line_hidden_blend{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label             = erhe::utility::Debug_label{"Hidden lines with blending"},
            //.shader_stages           = programs.wide_lines_draw_color.shader_stages(),
            .input_assembly          = Input_assembly_state::line,
            .multisample             = Multisample_state{
                .alpha_to_coverage_enable = true
            },
            .rasterization           = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::greater, reverse_depth),
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::incr,
                    .function        = erhe::graphics::Compare_operation::equal,
                    .reference       = 0u,
                    .test_mask       = 0b11111111u,
                    .write_mask      = 0b01111111u // ignore high bit (selection)
                },
                .stencil_back = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::incr,
                    .function        = erhe::graphics::Compare_operation::equal,
                    .reference       = 0u,
                    .test_mask       = 0b11111111u,
                    .write_mask      = 0b01111111u // ignore high bit (selection)
                },
            },
            .color_blend = &line_hidden_blend_state
        }
    }

    , brush_back{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Brush back faces"},
            //.shader_stages  = programs.brush.shader_stages(),
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_front_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            //.color_blend    = &Color_blend_state::color_blend_premultiplied
        }
    }
    , brush_front{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Brush front faces"},
            //.shader_stages  = programs.brush.shader_stages(),
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            //.color_blend    = &Color_blend_state::color_blend_premultiplied
        }
    }

    , edge_lines{graphics_device, erhe::graphics::Base_render_pipeline_create_info{
        .debug_label    = erhe::utility::Debug_label{"Edge Lines"},
        //.shader_stages  = programs.wide_lines_draw_color.shader_stages(),
        .input_assembly = Input_assembly_state::line,
        .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less_or_equal, reverse_depth),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                .z_fail_op       = erhe::graphics::Stencil_op::keep,
                .z_pass_op       = erhe::graphics::Stencil_op::incr,
                .function        = erhe::graphics::Compare_operation::equal,
                .reference       = 0u,
                .test_mask       = 0b01111111u,
                .write_mask      = 0b01111111u // ignore high bit (selection)
            },
            .stencil_back = {
                .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                .z_fail_op       = erhe::graphics::Stencil_op::keep,
                .z_pass_op       = erhe::graphics::Stencil_op::incr,
                .function        = erhe::graphics::Compare_operation::equal,
                .reference       = 0u,
                .test_mask       = 0b01111111u,
                .write_mask      = 0b01111111u // ignore high bit (selection)
            }
        },
        .color_blend    = &Color_blend_state::color_blend_premultiplied
    }}
    , outline{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Outline (selection/hover)"},
            //.shader_stages  = programs.wide_lines_draw_color.shader_stages(),
            .input_assembly = Input_assembly_state::line,
            .multisample    = Multisample_state{
                .alpha_to_coverage_enable = true
            },
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil = {
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::Compare_operation::always,
                .stencil_test_enable = true, // If bit 7 in the stencil buffer is not set, draw and set it. Otherwise, skip drawing
                .stencil_front = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::replace,
                    .function        = erhe::graphics::Compare_operation::not_equal,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b10000000u,
                    .write_mask      = 0b10000000u
                },
                .stencil_back = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::replace,
                    .function        = erhe::graphics::Compare_operation::not_equal,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b10000000u,
                    .write_mask      = 0b10000000u
                }
            },
            .color_blend    = &Color_blend_state::color_blend_premultiplied
        }
    }
    , corner_points{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Corner Points"},
            //.shader_stages  = programs.points.shader_stages(),
            .input_assembly = Input_assembly_state::point,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_disabled
        }
    }
    , polygon_centroids{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Polygon Centroids"},
            //.shader_stages  = programs.points.shader_stages(),
            .input_assembly = Input_assembly_state::point,
            .rasterization  = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(m_y_flip),
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_disabled
        }
    }
    , sky{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label          = erhe::utility::Debug_label{"Sky"},
            .input_assembly       = Input_assembly_state::triangle,
            .viewport_depth_range = Viewport_depth_range_state{
                // Far-plane depth: 0.0 for reverse-Z, 1.0 for forward-Z. The sky
                // draws only where depth equals the (cleared) far plane.
                .min_depth = reverse_depth ? 0.0f : 1.0f,
                .max_depth = reverse_depth ? 0.0f : 1.0f
            },
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = Depth_stencil_state{
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::Compare_operation::equal, // Depth buffer must be cleared to the far plane value
                .stencil_test_enable = true, // Require stencil clear value 0 (to prevent overdrawing selection silhouette)
                .stencil_front = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::keep,
                    .function        = erhe::graphics::Compare_operation::equal,
                    .reference       = 0u,
                    .test_mask       = 0b11111111u,
                    .write_mask      = 0b00000000u
                },
                .stencil_back = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::keep,
                    .function        = erhe::graphics::Compare_operation::equal,
                    .reference       = 0u,
                    .test_mask       = 0b11111111u,
                    .write_mask      = 0b00000000u
                },
            }
        }
    }
    , grid{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Grid"},
            .input_assembly = Input_assembly_state::triangle,
            .rasterization  = Rasterization_state::cull_mode_none_depth_clamp,
            .depth_stencil = {
                .depth_test_enable   = true,
                .depth_write_enable  = true,
                .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less_or_equal, reverse_depth),
                .stencil_test_enable = true, // Conditionally render fragments where bit 7 is not set, without modifying the stencil buffer
                .stencil_front = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::keep,
                    .function        = erhe::graphics::Compare_operation::not_equal,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b10000000u,
                    .write_mask      = 0b10000000u
                },
                .stencil_back = {
                    .stencil_fail_op = erhe::graphics::Stencil_op::keep,
                    .z_fail_op       = erhe::graphics::Stencil_op::keep,
                    .z_pass_op       = erhe::graphics::Stencil_op::keep,
                    .function        = erhe::graphics::Compare_operation::not_equal,
                    .reference       = 0b10000000u,
                    .test_mask       = 0b10000000u,
                    .write_mask      = 0b10000000u
                }
            },
            .color_blend    = &Color_blend_state::color_blend_premultiplied
        }
    }
{
}

void App_rendering::trigger_capture()
{
    m_trigger_capture = true;
}

auto App_rendering::width() const -> int
{
    return m_context.context_window->get_width();
}

auto App_rendering::height() const -> int
{
    return m_context.context_window->get_height();
}

void App_rendering::imgui()
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("App_rendering::imgui()");

    const ImGuiTreeNodeFlags flags{
        ImGuiTreeNodeFlags_Framed            |
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    if (ImGui::TreeNodeEx("Skin Debug", flags)) {
        int index = static_cast<int>(debug_joint_indices.x);
        ImGui::SliderInt("Debug Joint Index", &index, 0, 200); // TODO correct range
        debug_joint_indices.x = index;

        for (int joint_index = 0, end = static_cast<int>(debug_joint_colors.size()); joint_index < end; ++joint_index) {
            std::string label = fmt::format("Joint {}", joint_index);
            ImGui::ColorEdit4(label.c_str(), &debug_joint_colors[joint_index].x, ImGuiColorEditFlags_Float);
        }
        ImGui::TreePop();
    }

    m_composer.imgui();
}

auto App_rendering::is_capturing() const -> bool
{
    return m_trigger_capture;
}

void App_rendering::process_start_capture()
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("App_rendering::begin_frame() (check for renderdoc frame capture)");

    if (m_trigger_capture) {
        m_context.graphics_device->start_frame_capture();
    }
}

void App_rendering::request_renderdoc_capture()
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_context.headset_view->request_renderdoc_capture();
#endif
}

void App_rendering::set_grid_visibility(bool visible)
{
    // TODO Consider using Item visibility flag and removing enabled
    if (m_grid_composition_pass != nullptr) {
        m_grid_composition_pass->data.enabled = visible;
    }
}

void App_rendering::process_end_capture()
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("App_rendering::end_frame() (check for renderdoc frame capture)");

    if (m_trigger_capture) {
        m_context.graphics_device->end_frame_capture();
        m_trigger_capture = false;
    }
}

void App_rendering::add(Renderable* renderable)
{
    ERHE_VERIFY(renderable != nullptr);

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_renderables_mutex};

#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_renderables.begin(),
        m_renderables.end(),
        [renderable](Renderable* entry) {
            return entry == renderable;
        }
    );
    if (i != m_renderables.end()) {
        log_render->error("App_rendering::add(Renderable*): renderable is already registered");
        return;
    }
#endif

    m_renderables.push_back(renderable);
}

void App_rendering::remove(Renderable* renderable)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_renderables_mutex};

    const auto i = std::find_if(
        m_renderables.begin(),
        m_renderables.end(),
        [renderable](Renderable* entry) {
            return entry == renderable;
        }
    );
    if (i == m_renderables.end()) {
        log_render->error("App_rendering::remove(Renderable*): renderable is not registered");
        return;
    }
    m_renderables.erase(i);
}

void App_rendering::update_content_wide_line_pipeline_states(erhe::scene_renderer::Content_wide_line_renderer& renderer)
{
    erhe::graphics::Shader_stages* shader_stages = renderer.get_graphics_shader_stages();
    if (shader_stages == nullptr) {
        return;
    }

    // For each composition pass that uses content_wide_line_renderer,
    // create a new pipeline state with the renderer's shader stages and vertex input
    // but keep the original depth/stencil/blend settings.
    auto update_pass = [&](Composition_pass* pass) {
        if ((pass == nullptr) || !pass->data.use_content_wide_line_renderer) {
            return;
        }
        std::vector<erhe::graphics::Base_render_pipeline*> new_states;
        for (erhe::graphics::Base_render_pipeline* original : pass->data.base_render_pipelines) {
            auto pipeline = std::make_unique<erhe::graphics::Base_render_pipeline>(
                *m_context.graphics_device,
                erhe::graphics::Base_render_pipeline_create_info{
                    .debug_label    = original->data.debug_label,
                    //.shader_stages  = shader_stages,
                    //.vertex_input   = vertex_input,
                    .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                    .multisample    = original->data.multisample,
                    .rasterization  = original->data.rasterization,
                    .depth_stencil  = original->data.depth_stencil,
                    .color_blend    = original->data.color_blend
                }
            );
            new_states.push_back(pipeline.get());
            m_compute_wide_line_pipeline_states.push_back(std::move(pipeline));
        }
        pass->data.base_render_pipelines = new_states;
    };

    update_pass(edge_lines_not_selected.get());
    update_pass(edge_lines_selected.get());
    update_pass(selection_outline.get());
    update_pass(translucent_outline.get());
}

void App_rendering::render_viewport_main(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("App_rendering::render_viewport_main()");

    render_composer(context);
}

void App_rendering::render_viewport_renderables(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(context.command_buffer != nullptr);
    erhe::graphics::Scoped_debug_group debug_group{*context.command_buffer, "App_rendering::render_viewport_renderables"};

    for (auto* renderable : m_renderables) {
        renderable->render(context);
    }
}

void App_rendering::render_composer(const Render_context& context)
{
    // log_frame->trace("App_rendering::render_composer()");

    static constexpr std::string_view c_id_main{"Main"};
    //ERHE_PROFILE_GPU_SCOPE(c_id_main);
    ERHE_VERIFY(context.command_buffer != nullptr);
    erhe::graphics::Scoped_debug_group pass_scope{*context.command_buffer, "App_rendering::render_composer()"};

    m_composer.render(context);

    ///// TODO Check m_context.graphics_device->opengl_state_tracker.depth_stencil.reset(); // workaround issue in stencil state tracking
}

void App_rendering::render_id(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    // log_frame->trace("App_rendering::render_id()");

    const auto scene_root = context.scene_view.get_scene_root();
    if (!scene_root){
        return;
    }

    const auto position_opt = context.viewport_scene_view->get_position_in_viewport();
    if (!position_opt.has_value()) {
        return;
    }
    const auto position = position_opt.value();

    const auto& layers = scene_root->layers();
    Scene_root* tool_scene_root = m_context.tools->get_tool_scene_root().get();
    if ((tool_scene_root == nullptr) || (context.camera == nullptr)) {
        return;
    }

    const Scene_layers& tool_layers = tool_scene_root->layers();

    // TODO listen to viewport changes in msg bus?
    ERHE_VERIFY(context.command_buffer != nullptr);
    m_context.id_renderer->render(
        Id_renderer::Render_parameters{
            .command_buffer     = *context.command_buffer,
            .viewport           = context.viewport,
            .camera             = *context.camera,
            .content_mesh_spans = { layers.content()->meshes, layers.rendertarget()->meshes },
            .tool_mesh_spans    = { tool_layers.tool()->meshes },
            .x                  = static_cast<int>(position.x),
            .y                  = static_cast<int>(position.y),
            .reverse_depth      = context.scene_view.get_reverse_depth(),
            .depth_range        = context.scene_view.get_depth_range(),
            .conventions        = context.scene_view.get_conventions()
        }
    );
}

}  // namespace editor
