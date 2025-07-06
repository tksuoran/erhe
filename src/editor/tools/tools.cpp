#include "tools/tools.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_root.hpp"
#include "tools/tool.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"

namespace editor {

using Vertex_input_state   = erhe::graphics::Vertex_input_state;
using Input_assembly_state = erhe::graphics::Input_assembly_state;
using Rasterization_state  = erhe::graphics::Rasterization_state;
using Depth_stencil_state  = erhe::graphics::Depth_stencil_state;
using Color_blend_state    = erhe::graphics::Color_blend_state;

Tools_pipeline_renderpasses::Tools_pipeline_renderpasses(erhe::graphics::Device& graphics_device, Mesh_memory& mesh_memory, Programs& programs)
    // Tool pass one: For hidden tool parts, set stencil to s_stencil_tool_mesh_hidden.
    // Only reads depth buffer, only writes stencil buffer.
    : tool1_hidden_stencil{erhe::graphics::Render_pipeline_state{{
        .name                    = "Tool pass 1: Tag depth hidden `s_stencil_tool_mesh_hidden`",
        .shader_stages           = &programs.tool.shader_stages,
        .vertex_input            = &mesh_memory.vertex_input,
        .input_assembly          = Input_assembly_state::triangle,
        .rasterization           = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = graphics_device.depth_function(gl::Depth_function::greater),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_hidden,
                .test_mask       = 0b00000000u,
                .write_mask      = 0b11111111u
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_hidden,
                .test_mask       = 0b00000000u,
                .write_mask      = 0b11111111u
            },
        },
        .color_blend             = Color_blend_state::color_writes_disabled
    }}}

    // Tool pass two: For visible tool parts, set stencil to s_stencil_tool_mesh_visible.
    // Only reads depth buffer, only writes stencil buffer.
    , tool2_visible_stencil{erhe::graphics::Render_pipeline_state{{
        .name                    = "Tool pass 2: Tag visible tool parts `s_stencil_tool_mesh_visible`",
        .shader_stages           = &programs.tool.shader_stages,
        .vertex_input            = &mesh_memory.vertex_input,
        .input_assembly          = erhe::graphics::Input_assembly_state::triangle,
        .rasterization           = erhe::graphics::Rasterization_state::cull_mode_back_ccw,
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = graphics_device.depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0b00000000u,
                .write_mask      = 0b11111111u
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0b00000000u,
                .write_mask      = 0b11111111u
            },
        },
        .color_blend             = Color_blend_state::color_writes_disabled
    }}}

    // Tool pass three: Set depth to fixed value (with depth range)
    // Only writes depth buffer, depth test always.
    , tool3_depth_clear{
        erhe::graphics::Render_pipeline_state{
            {
                .name           = "Tool pass 3: Set depth to fixed value",
                .shader_stages  = &programs.tool.shader_stages,
                .vertex_input   = &mesh_memory.vertex_input,
                .input_assembly = Input_assembly_state::triangle,
                .rasterization  = Rasterization_state::cull_mode_back_ccw,
                .depth_stencil  = Depth_stencil_state::depth_test_always_stencil_test_disabled,
                .color_blend    = Color_blend_state::color_writes_disabled
            }
        },
        [](){ gl::depth_range(0.0f, 0.0f); },
        [](){ gl::depth_range(0.0f, 1.0f); }
    }

    // Tool pass four: Set depth to proper tool depth
    // Normal depth buffer update with depth test.
    , tool4_depth{erhe::graphics::Render_pipeline_state{{
        .name           = "Tool pass 4: Set depth to proper tool depth",
        .shader_stages  = &programs.tool.shader_stages,
        .vertex_input   = &mesh_memory.vertex_input,
        .input_assembly = Input_assembly_state::triangle,
        .rasterization  = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
        .color_blend    = Color_blend_state::color_writes_disabled
    }}}

    // Tool pass five: Render visible tool parts
    // Normal depth test, stencil test require s_stencil_tool_mesh_visible, color writes enabled, no blending
    , tool5_visible_color{erhe::graphics::Render_pipeline_state{{
        .name                    = "Tool pass 5: Render visible tool parts, require `s_stencil_tool_mesh_visible`",
        .shader_stages           = &programs.tool.shader_stages,
        .vertex_input            = &mesh_memory.vertex_input,
        .input_assembly          = Input_assembly_state::triangle,
        .rasterization           = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = graphics_device.depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::keep,
                .function        = gl::Stencil_function::equal,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0b11111111u,
                .write_mask      = 0b11111111u
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::keep,
                .function        = gl::Stencil_function::equal,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0b11111111u,
                .write_mask      = 0b11111111u
            }
        },
        .color_blend             = Color_blend_state::color_blend_disabled
    }}}

    // Tool pass six: Render hidden tool parts
    // Normal depth test, stencil test requires s_stencil_tool_mesh_hidden, color writes enabled, blending
    , tool6_hidden_color{erhe::graphics::Render_pipeline_state{{
        .name                       = "Tool pass 6: Render hidden tool parts, require `s_stencil_tool_mesh_hidden`",
        .shader_stages              = &programs.tool.shader_stages,
        .vertex_input               = &mesh_memory.vertex_input,
        .input_assembly             = Input_assembly_state::triangle,
        .rasterization              = Rasterization_state::cull_mode_back_ccw,
        .depth_stencil = {
            .depth_test_enable      = true,
            .depth_write_enable     = true,
            .depth_compare_op       = graphics_device.depth_function(gl::Depth_function::lequal),
            .stencil_test_enable    = true,
            .stencil_front = {
                .stencil_fail_op    = gl::Stencil_op::keep,
                .z_fail_op          = gl::Stencil_op::keep,
                .z_pass_op          = gl::Stencil_op::keep,
                .function           = gl::Stencil_function::equal,
                .reference          = s_stencil_tool_mesh_hidden,
                .test_mask          = 0b11111111u,
                .write_mask         = 0b11111111u
            },
            .stencil_back = {
                .stencil_fail_op    = gl::Stencil_op::keep,
                .z_fail_op          = gl::Stencil_op::keep,
                .z_pass_op          = gl::Stencil_op::replace,
                .function           = gl::Stencil_function::always,
                .reference          = s_stencil_tool_mesh_hidden,
                .test_mask          = 0b00000000u,
                .write_mask         = 0b11111111u,
            },
        },
        .color_blend = {
            .enabled                = true,
            .rgb = {
                .equation_mode      = gl::Blend_equation_mode::func_add,
                .source_factor      = gl::Blending_factor::constant_alpha,
                .destination_factor = gl::Blending_factor::one_minus_constant_alpha
            },
            .alpha = {
                .equation_mode      = gl::Blend_equation_mode::func_add,
                .source_factor      = gl::Blending_factor::constant_alpha,
                .destination_factor = gl::Blending_factor::one_minus_constant_alpha
            },
            .constant               = { 0.0f, 0.0f, 0.0f, 0.6f }
        }
    }}}
{
}

Tools::Tools(
    erhe::imgui::Imgui_renderer&    imgui_renderer,
    erhe::imgui::Imgui_windows&     imgui_windows,
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    app_context,
    App_rendering&                  app_rendering,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : m_context              {app_context}
    , m_pipeline_renderpasses{graphics_device, mesh_memory, programs}
{
    ERHE_PROFILE_FUNCTION();

    const auto tools_content_library = std::make_shared<Content_library>();

    if (app_context.developer_mode) {
        m_content_library_tree_window = std::make_shared<Item_tree_window>(
            imgui_renderer,
            imgui_windows,
            app_context,
            "Tools Library",
            "tools_content_library"
        );
        m_content_library_tree_window->set_root(tools_content_library->root);
        m_content_library_tree_window->set_item_filter(
            erhe::Item_filter{
                .require_all_bits_set           = 0,
                .require_at_least_one_bit_set   = 0,
                .require_all_bits_clear         = 0,
                .require_at_least_one_bit_clear = 0
            }
        );
        m_content_library_tree_window->set_developer();
    }

    m_scene_root = std::make_shared<Scene_root>(
        nullptr,
        nullptr,
        scene_message_bus,
        nullptr,
        nullptr, // Do not process editor messages
        nullptr, // Do not register to App_scenes
        tools_content_library,
        "Tool scene"
    );

    m_scene_root->get_scene().disable_flag_bits(erhe::Item_flags::show_in_ui);

    for (const auto& tool : m_tools) {
        const auto priority = tool->get_priority();
        tool->handle_priority_update(priority + 1, priority);
    }

    using Item_flags = erhe::Item_flags;
    auto tool = app_rendering.make_renderpass("Tool");
    tool->mesh_layers    = { Mesh_layer_id::tool };
    tool->passes         = {
        &m_pipeline_renderpasses.tool1_hidden_stencil,   // tag_depth_hidden_with_stencil
        &m_pipeline_renderpasses.tool2_visible_stencil,  // tag_depth_visible_with_stencil
        &m_pipeline_renderpasses.tool3_depth_clear,      // clear_depth
        &m_pipeline_renderpasses.tool4_depth,            // depth_only
        &m_pipeline_renderpasses.tool5_visible_color,    // require_stencil_tag_depth_visible
        &m_pipeline_renderpasses.tool6_hidden_color      // require_stencil_tag_depth_hidden_and_blend,
    };
    tool->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    tool->filter         = erhe::Item_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::tool,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    tool->override_scene_root = get_tool_scene_root();
    tool->allow_shader_stages_override = false;
}

auto Tools::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

void Tools::register_tool(Tool* tool)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    const auto flags = tool->get_flags();
    if (erhe::bit::test_all_rhs_bits_set(flags, Tool_flags::background)) {
        m_background_tools.emplace_back(tool);
    } else {
        m_tools.emplace_back(tool);
    }
}

void Tools::render_viewport_tools(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& tool : m_background_tools) {
        tool->tool_render(context);
    }
    for (const auto& tool : m_tools) {
        tool->tool_render(context);
    }
}

void Tools::set_priority_tool(Tool* priority_tool)
{
    if (m_priority_tool == priority_tool) {
        return;
    }

    if (m_priority_tool != nullptr) {
        log_tools->trace("de-prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(0);
    }

    m_priority_tool = priority_tool;

    if (m_priority_tool != nullptr) {
        log_tools->trace("prioritizing tool {}", m_priority_tool->get_description());
        m_priority_tool->set_priority_boost(100);
    } else {
        log_tools->trace("active tool reset");
    }

    {
        using namespace erhe::bit;
        const bool allow_secondary =
            (m_priority_tool != nullptr) &&
            test_all_rhs_bits_set(m_priority_tool->get_flags(), Tool_flags::allow_secondary);
        log_tools->trace("Update tools: allow_secondary = {}", allow_secondary);
        for (auto* tool : m_tools) {
            const auto flags = tool->get_flags();
            if (test_all_rhs_bits_set(flags, Tool_flags::toolbox)) {
                const bool is_priority_tool = (tool == m_priority_tool);
                const bool is_secondary     = test_all_rhs_bits_set(flags, Tool_flags::secondary);
                const bool enable           = is_priority_tool || (allow_secondary && is_secondary);
                tool->set_enabled(enable);
                log_tools->trace(
                    "{} {}{}{}", tool->get_description(),
                    is_priority_tool ? "priority " : "",
                    is_secondary     ? "secondary " : "",
                    enable           ? "-> enabled" : "-> disabled"
                );
            }
        }
    }

    m_context.commands->sort_bindings();
    m_context.app_message_bus->send_message(
        App_message{
            .update_flags = Message_flag_bit::c_flag_bit_tool_select
        }
    );
}

auto Tools::get_priority_tool() const -> Tool*
{
    return m_priority_tool;
}

auto Tools::get_tools() const -> const std::vector<Tool*>&
{
    return m_tools;
}

}  // namespace editor
