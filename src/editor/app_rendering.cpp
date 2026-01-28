#include "app_rendering.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "tools/tools.hpp"
#include "renderable.hpp"
#include "renderers/composer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
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
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_window/renderdoc_capture.hpp"
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
    erhe::commands::Commands& commands,
    erhe::graphics::Device&   graphics_device,
    App_context&              app_context,
    App_message_bus&          app_message_bus,
    Mesh_memory&              mesh_memory,
    Programs&                 programs
)
    : m_context              {app_context}
    , m_capture_frame_command{commands, app_context}
    , m_pipeline_passes      {graphics_device, mesh_memory, programs}
    , m_composer             {"Main Composer"}
    , m_content_timer        {graphics_device, "content"}
    , m_selection_timer      {graphics_device, "selection"}
    , m_gui_timer            {graphics_device, "gui"}
    , m_brush_timer          {graphics_device, "brush"}
    , m_tools_timer          {graphics_device, "tools"}
{
    ERHE_PROFILE_FUNCTION();

    commands.register_command(&m_capture_frame_command);
    commands.bind_command_to_key(&m_capture_frame_command, erhe::window::Key_f10);
    commands.bind_command_to_menu(&m_capture_frame_command, "View.Frame");

    using Item_filter = erhe::Item_filter;
    using Item_flags  = erhe::Item_flags;
    using namespace erhe::primitive;
    const Item_filter opaque_not_selected_filter{
        .require_all_bits_set         = Item_flags::visible     | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::content     | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::translucent | Item_flags::selected | Item_flags::hovered_in_item_tree
    };
    const Item_filter opaque_not_selected_filter_positive_determinant{
        .require_all_bits_set         = Item_flags::visible     | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::content     | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::translucent | Item_flags::selected | Item_flags::hovered_in_item_tree | Item_flags::negative_determinant
    };
    const Item_filter opaque_not_selected_filter_negative_determinant{
        .require_all_bits_set         = Item_flags::visible     | Item_flags::opaque   | Item_flags::negative_determinant,
        .require_at_least_one_bit_set = Item_flags::content     | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::translucent | Item_flags::selected | Item_flags::hovered_in_item_tree
    };
    const Item_filter opaque_selected_filter{
        .require_all_bits_set         = Item_flags::content | Item_flags::visible | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::selected,
        .require_all_bits_clear       = Item_flags::translucent
    };
    const Item_filter opaque_selected_filter_positive_determinant{
        .require_all_bits_set         = Item_flags::content | Item_flags::visible | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::selected,
        .require_all_bits_clear       = Item_flags::translucent | Item_flags::negative_determinant
    };
    const Item_filter opaque_selected_filter_negative_determinant{
        .require_all_bits_set         = Item_flags::content | Item_flags::visible | Item_flags::opaque | Item_flags::negative_determinant,
        .require_at_least_one_bit_set = Item_flags::selected,
        .require_all_bits_clear       = Item_flags::translucent
    };
    const Item_filter opaque_selected_or_hovered_filter{
        .require_all_bits_set         = Item_flags::content     | Item_flags::visible | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::selected    | Item_flags::hovered_in_item_tree,
        .require_all_bits_clear       = Item_flags::translucent
    };
    const Item_filter opaque_selected_or_hovered_filter_positive_determinant{
        .require_all_bits_set         = Item_flags::content     | Item_flags::visible | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::selected    | Item_flags::hovered_in_item_tree,
        .require_all_bits_clear       = Item_flags::translucent | Item_flags::negative_determinant
    };
    const Item_filter opaque_selected_or_hovered_filter_negative_determinant{
        .require_all_bits_set         = Item_flags::content  | Item_flags::visible | Item_flags::opaque | Item_flags::negative_determinant,
        .require_at_least_one_bit_set = Item_flags::selected | Item_flags::hovered_in_item_tree,
        .require_all_bits_clear       = Item_flags::translucent
    };
    const Item_filter translucent_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::translucent,
        .require_at_least_one_bit_set = Item_flags::content | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::opaque
    };

    const auto& render_style_not_selected = [](const Render_context& context) -> const Render_style_data& {
        return context.viewport_config.render_style_not_selected;
    };

    using namespace erhe::primitive;
    using Blend_mode = erhe::renderer::Blend_mode;
    auto opaque_fill_not_selected_positive_determinant = make_composition_pass("Content fill opaque not selected positive determinant");
    opaque_fill_not_selected_positive_determinant->mesh_layers      = { Mesh_layer_id::content, Mesh_layer_id::controller };
    opaque_fill_not_selected_positive_determinant->primitive_mode   = Primitive_mode::polygon_fill;
    opaque_fill_not_selected_positive_determinant->filter           = opaque_not_selected_filter_positive_determinant;
    opaque_fill_not_selected_positive_determinant->get_render_style = render_style_not_selected;
    opaque_fill_not_selected_positive_determinant->passes           = { get_pipeline_pass(*opaque_fill_not_selected_positive_determinant.get(), Blend_mode::opaque, false, false) };

    auto opaque_fill_not_selected_negative_determinant = make_composition_pass("Content fill opaque not selected negative determinant");
    opaque_fill_not_selected_negative_determinant->mesh_layers      = { Mesh_layer_id::content, Mesh_layer_id::controller };
    opaque_fill_not_selected_negative_determinant->primitive_mode   = Primitive_mode::polygon_fill;
    opaque_fill_not_selected_negative_determinant->filter           = opaque_not_selected_filter_negative_determinant;
    opaque_fill_not_selected_negative_determinant->get_render_style = render_style_not_selected;
    opaque_fill_not_selected_negative_determinant->passes           = { get_pipeline_pass(*opaque_fill_not_selected_negative_determinant.get(), Blend_mode::opaque, false, true) };

    const auto& render_style_selected = [](const Render_context& context) -> const Render_style_data& {
        return context.viewport_config.render_style_selected;
    };

    auto opaque_fill_selected_positive_determinant = make_composition_pass("Content fill opaque selected positive determinant");
    opaque_fill_selected_positive_determinant->mesh_layers      = { Mesh_layer_id::content, Mesh_layer_id::controller };
    opaque_fill_selected_positive_determinant->primitive_mode   = Primitive_mode::polygon_fill;
    opaque_fill_selected_positive_determinant->filter           = opaque_selected_or_hovered_filter_positive_determinant;
    opaque_fill_selected_positive_determinant->get_render_style = render_style_selected;
    opaque_fill_selected_positive_determinant->passes           = { get_pipeline_pass(*opaque_fill_selected_positive_determinant.get(), Blend_mode::opaque, true, false)};

    auto opaque_fill_selected_negative_determinant = make_composition_pass("Content fill opaque selected negative determinant");
    opaque_fill_selected_negative_determinant->mesh_layers      = { Mesh_layer_id::content, Mesh_layer_id::controller };
    opaque_fill_selected_negative_determinant->primitive_mode   = Primitive_mode::polygon_fill;
    opaque_fill_selected_negative_determinant->filter           = opaque_selected_or_hovered_filter_negative_determinant;
    opaque_fill_selected_negative_determinant->get_render_style = render_style_selected;
    opaque_fill_selected_negative_determinant->passes           = { get_pipeline_pass(*opaque_fill_selected_negative_determinant.get(), Blend_mode::opaque, true, true)};

    auto opaque_edge_lines_not_selected = make_composition_pass("Content edge lines opaque not selected");
    opaque_edge_lines_not_selected->mesh_layers      = { Mesh_layer_id::content };
    opaque_edge_lines_not_selected->primitive_mode   = Primitive_mode::edge_lines;
    opaque_edge_lines_not_selected->filter           = opaque_not_selected_filter;
    opaque_edge_lines_not_selected->get_render_style = render_style_not_selected;
    opaque_edge_lines_not_selected->passes           = { get_pipeline_pass(*opaque_edge_lines_not_selected.get(), Blend_mode::opaque, false, false) };
    opaque_edge_lines_not_selected->allow_shader_stages_override = false;

    auto opaque_edge_lines_selected = make_composition_pass("Content edge lines opaque selected");
    opaque_edge_lines_selected->mesh_layers      = { Mesh_layer_id::content };
    opaque_edge_lines_selected->primitive_mode   = Primitive_mode::edge_lines;
    opaque_edge_lines_selected->filter           = opaque_selected_filter;
    opaque_edge_lines_selected->get_render_style = render_style_selected;
    opaque_edge_lines_selected->passes           = { get_pipeline_pass(*opaque_edge_lines_selected.get(), Blend_mode::opaque, true, false) };
    opaque_edge_lines_selected->allow_shader_stages_override = false;

    selection_outline = make_composition_pass("Content outline opaque selected");
    selection_outline->mesh_layers      = { Mesh_layer_id::content };
    selection_outline->primitive_mode   = Primitive_mode::polygon_fill;
    selection_outline->filter           = opaque_selected_or_hovered_filter;
    selection_outline->passes           = { &m_pipeline_passes.outline };
    selection_outline->allow_shader_stages_override = false;

    // This gets overridden in Composition_pass::render()
    // TODO Figure out a good way to route the settings
    selection_outline->primitive_settings = erhe::scene_renderer::Primitive_interface_settings{
        .constant_color0 = glm::vec4{1.0f, 0.75f, 0.0f, 1.0f},
        .constant_color1 = glm::vec4{0.0f, 0.0f,  1.0f, 1.0f},
        .constant_size   = -5.0f
    };

    auto sky = make_composition_pass("Sky");
    sky->mesh_layers           = {};
    sky->non_mesh_vertex_count = 3; // Fullscreen quad
    sky->passes                = { &m_pipeline_passes.sky };
    sky->primitive_mode        = erhe::primitive::Primitive_mode::polygon_fill;
    sky->filter = erhe::Item_filter{
        .require_all_bits_set         = 0,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    sky->allow_shader_stages_override = false;

    // Infinite plane with 4 triangles / 12 indices - https://stackoverflow.com/questions/12965161/rendering-infinitely-large-plane
    m_grid_composition_pass = make_composition_pass("Grid");
    m_grid_composition_pass->mesh_layers           = {};
    m_grid_composition_pass->non_mesh_vertex_count = 12;
    m_grid_composition_pass->passes                = { &m_pipeline_passes.grid };
    m_grid_composition_pass->primitive_mode        = erhe::primitive::Primitive_mode::polygon_fill;
    m_grid_composition_pass->filter = erhe::Item_filter{
        .require_all_bits_set         = 0,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    m_grid_composition_pass->allow_shader_stages_override = false;

    // Translucent
    auto translucent_fill = make_composition_pass("Content fill translucent");
    translucent_fill->mesh_layers    = { Mesh_layer_id::content };
    translucent_fill->primitive_mode = Primitive_mode::polygon_fill;
    translucent_fill->filter         = translucent_filter;
    translucent_fill->passes         = { get_pipeline_pass(*translucent_fill.get(), Blend_mode::translucent, false, false) };

    auto translucent_outline = make_composition_pass("Content outline translucent");
    translucent_outline->mesh_layers    = { Mesh_layer_id::content };
    translucent_outline->primitive_mode = Primitive_mode::edge_lines;
    translucent_outline->filter         = translucent_filter;
    translucent_outline->passes         = { get_pipeline_pass(*translucent_outline.get(), Blend_mode::translucent, false, false) };

    auto brush = make_composition_pass("Brush");
    brush->mesh_layers    = { Mesh_layer_id::brush };
    brush->passes         = { &m_pipeline_passes.brush_back, &m_pipeline_passes.brush_front };
    brush->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    brush->filter = erhe::Item_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::brush,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    brush->allow_shader_stages_override = false;

    auto rendertarget = make_composition_pass("Rendertarget");
    rendertarget->mesh_layers    = { Mesh_layer_id::rendertarget };
    rendertarget->passes         = { &m_pipeline_passes.rendertarget_meshes };
    rendertarget->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    rendertarget->filter = erhe::Item_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::rendertarget,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    rendertarget->allow_shader_stages_override = false;
    //rendertarget->allow_shader_stages_override = true;
    app_message_bus.add_receiver(
        [&](App_message& message) {
            using namespace erhe::utility;
            if (test_bit_set(message.update_flags, Message_flag_bit::c_flag_bit_graphics_settings)) {
                handle_graphics_settings_changed(message.graphics_preset);
            }
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
    const auto& preset      = app_settings.graphics.current_graphics_preset;
    const int   resolution  = preset.shadow_enable ? preset.shadow_resolution  : 1;
    const int   light_count = preset.shadow_enable ? preset.shadow_light_count : 1;
    auto shadow_render_node = std::make_shared<Shadow_render_node>(
        graphics_device,
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

void App_rendering::handle_graphics_settings_changed(Graphics_preset* graphics_preset)
{
    const int resolution  = (graphics_preset != nullptr) && graphics_preset->shadow_enable ? graphics_preset->shadow_resolution  : 1;
    const int light_count = (graphics_preset != nullptr) && graphics_preset->shadow_enable ? graphics_preset->shadow_light_count : 1;

    for (const auto& node : m_all_shadow_render_nodes) {
        node->reconfigure(*m_context.graphics_device, resolution, light_count, graphics_preset->shadow_depth_bits);
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

auto App_rendering::get_pipeline_pass(
    const Composition_pass&          composition_pass,
    const erhe::renderer::Blend_mode blend_mode,
    const bool                       selected,
    const bool                       negative_determinant
) -> erhe::renderer::Pipeline_pass*
{
    using namespace erhe::primitive;
    switch (composition_pass.primitive_mode) {
        case Primitive_mode::polygon_fill:
            switch (blend_mode) {
                case erhe::renderer::Blend_mode::opaque:
                    return selected
                        ? (negative_determinant
                            ? &m_pipeline_passes.polygon_fill_standard_opaque_selected_negative_determinant
                            : &m_pipeline_passes.polygon_fill_standard_opaque_selected_positive_determinant
                        )
                        : (negative_determinant
                            ? &m_pipeline_passes.polygon_fill_standard_opaque_negative_determinant
                            : &m_pipeline_passes.polygon_fill_standard_opaque_positive_determinant
                        );
                case erhe::renderer::Blend_mode::translucent:
                    return &m_pipeline_passes.polygon_fill_standard_translucent;
                default:
                    return nullptr;
            }
            break;

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

using Vertex_input_state         = erhe::graphics::Vertex_input_state;
using Input_assembly_state       = erhe::graphics::Input_assembly_state;
using Multisample_state          = erhe::graphics::Multisample_state;
using Viewport_depth_range_state = erhe::graphics::Viewport_depth_range_state;
using Rasterization_state        = erhe::graphics::Rasterization_state;
using Depth_stencil_state        = erhe::graphics::Depth_stencil_state;
using Color_blend_state          = erhe::graphics::Color_blend_state;

Pipeline_renderpasses::Pipeline_renderpasses(erhe::graphics::Device& graphics_device, Mesh_memory&mesh_memory, Programs& programs)
    : m_empty_vertex_input{graphics_device}
    , polygon_fill_standard_opaque_positive_determinant{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque Positive Determinant",
        .shader_stages  = &programs.circular_brushed_metal.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , polygon_fill_standard_opaque_negative_determinant{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque Negative Determinant",
        .shader_stages  = &programs.circular_brushed_metal.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_cw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , polygon_fill_standard_opaque_selected_positive_determinant{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque Selected Positive Determinant",
        .shader_stages  = &programs.circular_brushed_metal.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
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
        },
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , polygon_fill_standard_opaque_selected_negative_determinant{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Opaque Selected Negative Determinant",
        .shader_stages  = &programs.circular_brushed_metal.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_cw,
        .depth_stencil  = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less, true),
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
        },
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , polygon_fill_standard_translucent{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Fill Translucent",
        .shader_stages  = &programs.circular_brushed_metal.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    }}}
    , line_hidden_blend{erhe::graphics::Render_pipeline_state{{
        .name                    = "Hidden lines with blending",
        .shader_stages           = &programs.wide_lines_draw_color.shader_stages,
        .vertex_input            = &mesh_memory.vertex_input,
        .input_assembly          = Input_assembly_state::line,
        .multisample             = Multisample_state{
            .alpha_to_coverage_enable = true
        },
        .rasterization           = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::greater, true),
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
        .color_blend = {
            .enabled                = true,
            .rgb = {
                .equation_mode      = erhe::graphics::Blend_equation_mode::func_add,
                .source_factor      = erhe::graphics::Blending_factor::constant_alpha,
                .destination_factor = erhe::graphics::Blending_factor::one_minus_constant_alpha
            },
            .alpha = {
                .equation_mode      = erhe::graphics::Blend_equation_mode::func_add,
                .source_factor      = erhe::graphics::Blending_factor::constant_alpha,
                .destination_factor = erhe::graphics::Blending_factor::one_minus_constant_alpha
            },
            .constant = { 0.0f, 0.0f, 0.0f, 0.2f }
        }
    }}}
    , brush_back{erhe::graphics::Render_pipeline_state{{
        .name           = "Brush back faces",
        .shader_stages  = &programs.brush.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_front_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    }}}
    , brush_front{erhe::graphics::Render_pipeline_state{{
        .name           = "Brush front faces",
        .shader_stages  = &programs.brush.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    }}}
    , edge_lines{erhe::graphics::Render_pipeline_state{{
        .name           = "Edge Lines",
        .shader_stages  = &programs.wide_lines_draw_color.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::line,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less_or_equal, true),
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
        .color_blend    = Color_blend_state::color_blend_premultiplied
    }}}
    , outline{erhe::graphics::Render_pipeline_state{{
        .name           = "Outline (selection/hover)",
        .shader_stages  = &programs.fat_triangle.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .multisample    = Multisample_state{
            .alpha_to_coverage_enable = true
        },
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
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
        .color_blend    = Color_blend_state::color_blend_premultiplied
    }}}
    , corner_points{erhe::graphics::Render_pipeline_state{{
        .name           = "Corner Points",
        .shader_stages  = &programs.points.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::point,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , polygon_centroids{erhe::graphics::Render_pipeline_state{{
        .name           = "Polygon Centroids",
        .shader_stages  = &programs.points.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::point,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_disabled
    }}}
    , rendertarget_meshes{erhe::graphics::Render_pipeline_state{{
        .name           = "Rendertarget Meshes",
        .shader_stages  = &programs.textured.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw, 
        // Useful for debugging rendertarget meshes
        // .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    }}}
    , sky{
        erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name                 = "Sky",
                .shader_stages        = &programs.sky.shader_stages,
                .vertex_input         = &mesh_memory.vertex_input,
                .input_assembly       = Input_assembly_state::triangle,
                .viewport_depth_range = Viewport_depth_range_state{
                    .min_depth = 0.0f, // Reverse Z far plane
                    .max_depth = 0.0f  // Reverse Z far plane
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
                },
                .color_blend    = Color_blend_state::color_blend_disabled
            }
        }
    }
    , grid{
        erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Grid",
                .shader_stages  = &programs.grid.shader_stages,
                .vertex_input   = &mesh_memory.vertex_input,
                .input_assembly = Input_assembly_state::triangle,
                .rasterization  = Rasterization_state::cull_mode_none_depth_clamp,
                .depth_stencil = {
                    .depth_test_enable   = true,
                    .depth_write_enable  = true,
                    .depth_compare_op    = erhe::graphics::get_depth_function(erhe::graphics::Compare_operation::less_or_equal, true),
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

                .color_blend    = Color_blend_state::color_blend_premultiplied
            }
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

void App_rendering::begin_frame()
{
    ERHE_PROFILE_FUNCTION();

    if (m_trigger_capture) {
        erhe::window::start_frame_capture(*m_context.context_window);
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
        m_grid_composition_pass->enabled = visible;
    }
}

void App_rendering::end_frame()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_XR_LIBRARY_OPENXR)
    m_context.headset_view->end_frame();
#endif

    m_context.id_renderer->next_frame();

    if (m_trigger_capture) {
        erhe::window::end_frame_capture(*m_context.context_window);
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

void App_rendering::render_viewport_main(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    render_composer(context);
}

void App_rendering::render_viewport_renderables(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    for (auto* renderable : m_renderables) {
        renderable->render(context);
    }
}

void App_rendering::render_composer(const Render_context& context)
{
    static constexpr std::string_view c_id_main{"Main"};
    //ERHE_PROFILE_GPU_SCOPE(c_id_main);
    erhe::graphics::Scoped_gpu_timer timer{m_content_timer};
    erhe::graphics::Scoped_debug_group pass_scope{"App_rendering::render_composer()"};

    m_composer.render(context);

    ///// TODO Check m_context.graphics_device->opengl_state_tracker.depth_stencil.reset(); // workaround issue in stencil state tracking
}

void App_rendering::render_id(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

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
    m_context.id_renderer->render(
        Id_renderer::Render_parameters{
            .viewport           = context.viewport,
            .camera             = *context.camera,
            .content_mesh_spans = { layers.content()->meshes, layers.rendertarget()->meshes },
            .tool_mesh_spans    = { tool_layers.tool()->meshes },
            .x                  = static_cast<int>(position.x),
            .y                  = static_cast<int>(position.y)
        }
    );
}

}  // namespace editor
