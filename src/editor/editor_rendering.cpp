#include "editor_rendering.hpp"

#include "editor_log.hpp"
#include "renderers/composer.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/pipeline_renderpass.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/shadow_renderer.hpp"
#include "renderers/viewport_config.hpp"
#include "rendergraph/post_processing.hpp"
#include "rendertarget_imgui_viewport.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "windows/debug_view_window.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif

#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <initializer_list>

namespace editor {

using erhe::graphics::Color_blend_state;


#pragma region Commands
class Capture_frame_command
    : public erhe::application::Command
{
public:
    Capture_frame_command();
    auto try_call() -> bool override;
};

Capture_frame_command::Capture_frame_command()
    : Command{"editor.capture_frame"}
{
}

auto Capture_frame_command::try_call() -> bool
{
    g_editor_rendering->trigger_capture();
    return true;
}
#pragma endregion Commands

class Pipeline_renderpasses
{
public:
    void initialize();

    std::unique_ptr<erhe::graphics::Vertex_input_state> m_empty_vertex_input;
    Pipeline_renderpass polygon_fill_standard_opaque;
    Pipeline_renderpass polygon_fill_standard_translucent;
    Pipeline_renderpass tool1_hidden_stencil;
    Pipeline_renderpass tool2_visible_stencil;
    Pipeline_renderpass tool3_depth_clear;
    Pipeline_renderpass tool4_depth;
    Pipeline_renderpass tool5_visible_color;
    Pipeline_renderpass tool6_hidden_color;
    Pipeline_renderpass line_hidden_blend;
    Pipeline_renderpass brush_back;
    Pipeline_renderpass brush_front;
    Pipeline_renderpass edge_lines;
    Pipeline_renderpass corner_points;
    Pipeline_renderpass polygon_centroids;
    Pipeline_renderpass rendertarget_meshes;
    Pipeline_renderpass sky;
};

IEditor_rendering::~IEditor_rendering() noexcept = default;

class Editor_rendering_impl
    : public IEditor_rendering
{
public:
    Editor_rendering_impl();
    ~Editor_rendering_impl() noexcept override;

    void post_initialize();

    // Implements
    void trigger_capture     () override;
    void render              () override;
    void render_viewport_main(const Render_context& context, bool has_pointer) override;
    void render_composer     (const Render_context& context) override;
    void render_id           (const Render_context& context) override;
    void begin_frame         () override;
    void end_frame           () override;

private:
    auto make_renderpass        (const std::string_view name) -> std::shared_ptr<Renderpass>;
    auto get_pipeline_renderpass(const Renderpass& renderpass, Blend_mode blend_mode) -> Pipeline_renderpass*;
    void setup_renderpasses     ();
    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    // Commands
    Capture_frame_command m_capture_frame_command;

    bool                  m_trigger_capture{false};
    Pipeline_renderpasses m_pipeline_renderpasses;
    Composer              m_composer;

    std::unique_ptr<erhe::graphics::Gpu_timer> m_content_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_selection_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_gui_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_brush_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_tools_timer;
};

IEditor_rendering* g_editor_rendering{nullptr};

Editor_rendering::Editor_rendering()
    : erhe::components::Component{c_type_name}
{
}

Editor_rendering::~Editor_rendering() noexcept
{
    ERHE_VERIFY(g_editor_rendering == nullptr);
}

void Editor_rendering::deinitialize_component()
{
    m_impl.reset();
}

void Editor_rendering::declare_required_components()
{
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<Programs   >();
    require<Mesh_memory>();
    require<Tools      >();
}

void Editor_rendering::initialize_component()
{
    m_impl = std::make_unique<Editor_rendering_impl>();
}

void Editor_rendering::post_initialize()
{
    m_impl->post_initialize();
}

//

Editor_rendering_impl::Editor_rendering_impl()
    : m_composer{"Main Composer"}
{
    ERHE_VERIFY(g_editor_rendering == nullptr);
    g_editor_rendering = this;
    erhe::application::g_commands->register_command(&m_capture_frame_command);
    erhe::application::g_commands->bind_command_to_key(&m_capture_frame_command, erhe::toolkit::Key_f10);

    using Item_filter = erhe::scene::Item_filter;
    using Item_flags  = erhe::scene::Item_flags;
    using namespace erhe::primitive;
    const Item_filter opaque_not_selected_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::opaque,
        .require_at_least_one_bit_set = Item_flags::content | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::translucent | Item_flags::selected
    };
    const Item_filter opaque_selected_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::opaque | Item_flags::selected,
        .require_at_least_one_bit_set = Item_flags::content | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::translucent
    };
    const Item_filter translucent_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::translucent,
        .require_at_least_one_bit_set = Item_flags::content | Item_flags::controller,
        .require_all_bits_clear       = Item_flags::opaque
    };

    const auto render_style_selected = [](const Render_context& context)
    {
        return &context.viewport_config->render_style_selected;
    };
    const auto render_style_not_selected = [](const Render_context& context)
    {
        return &context.viewport_config->render_style_not_selected;
    };

    using namespace erhe::primitive;
    auto opaque_fill_not_selected = make_renderpass("Content fill opaque not selected");
    opaque_fill_not_selected->mesh_layers      = { Mesh_layer_id::content, Mesh_layer_id::controller };
    opaque_fill_not_selected->primitive_mode   = Primitive_mode::polygon_fill;
    opaque_fill_not_selected->filter           = opaque_not_selected_filter;
    opaque_fill_not_selected->get_render_style = render_style_not_selected;
    opaque_fill_not_selected->passes           = { get_pipeline_renderpass(*opaque_fill_not_selected.get(), Blend_mode::opaque) };

    auto opaque_fill_selected = make_renderpass("Content fill opaque selected");
    opaque_fill_selected->mesh_layers      = { Mesh_layer_id::content, Mesh_layer_id::controller };
    opaque_fill_selected->primitive_mode   = Primitive_mode::polygon_fill;
    opaque_fill_selected->filter           = opaque_selected_filter;
    opaque_fill_selected->get_render_style = render_style_selected;
    opaque_fill_selected->passes           = { get_pipeline_renderpass(*opaque_fill_selected.get(), Blend_mode::opaque) };

    auto opaque_outline_not_selected = make_renderpass("Content outline opaque not selected");
    opaque_outline_not_selected->mesh_layers      = { Mesh_layer_id::content };
    opaque_outline_not_selected->primitive_mode   = Primitive_mode::edge_lines;
    opaque_outline_not_selected->filter           = opaque_not_selected_filter;
    opaque_outline_not_selected->begin            = []() { gl::enable (gl::Enable_cap::sample_alpha_to_coverage); };
    opaque_outline_not_selected->end              = []() { gl::disable(gl::Enable_cap::sample_alpha_to_coverage); };
    opaque_outline_not_selected->get_render_style = render_style_not_selected;
    opaque_outline_not_selected->passes           = { get_pipeline_renderpass(*opaque_outline_not_selected.get(), Blend_mode::opaque) };
    opaque_outline_not_selected->allow_shader_stages_override = false;

    auto opaque_outline_selected = make_renderpass("Content outline opaque selected");
    opaque_outline_selected->mesh_layers      = { Mesh_layer_id::content };
    opaque_outline_selected->primitive_mode   = Primitive_mode::edge_lines;
    opaque_outline_selected->filter           = opaque_selected_filter;
    opaque_outline_selected->begin            = []() { gl::enable (gl::Enable_cap::sample_alpha_to_coverage); };
    opaque_outline_selected->end              = []() { gl::disable(gl::Enable_cap::sample_alpha_to_coverage); };
    opaque_outline_selected->get_render_style = render_style_selected;
    opaque_outline_selected->passes           = { get_pipeline_renderpass(*opaque_outline_selected.get(), Blend_mode::opaque) };
    opaque_outline_selected->allow_shader_stages_override = false;

    auto sky = make_renderpass("Sky");
    sky->mesh_layers    = {};
    sky->passes         = { &m_pipeline_renderpasses.sky };
    sky->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    sky->filter = erhe::scene::Item_filter{
        .require_all_bits_set         = 0,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    sky->allow_shader_stages_override = false;

    // Translucent
    auto translucent_fill = make_renderpass("Content fill translucent");
    translucent_fill->mesh_layers    = { Mesh_layer_id::content };
    translucent_fill->primitive_mode = Primitive_mode::polygon_fill;
    translucent_fill->filter         = translucent_filter;
    translucent_fill->passes         = { get_pipeline_renderpass(*translucent_fill.get(), Blend_mode::translucent) };

    auto translucent_outline = make_renderpass("Content outline translucent");
    translucent_outline->mesh_layers    = { Mesh_layer_id::content };
    translucent_outline->primitive_mode = Primitive_mode::edge_lines;
    translucent_outline->filter         = translucent_filter;
    translucent_outline->begin          = []() { gl::enable (gl::Enable_cap::sample_alpha_to_coverage); };
    translucent_outline->end            = []() { gl::disable(gl::Enable_cap::sample_alpha_to_coverage); };
    translucent_outline->passes         = { get_pipeline_renderpass(*translucent_outline.get(), Blend_mode::translucent) };

    auto brush = make_renderpass("Brush");
    brush->mesh_layers    = { Mesh_layer_id::brush };
    brush->passes         = { &m_pipeline_renderpasses.brush_back, &m_pipeline_renderpasses.brush_front };
    brush->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    brush->filter = erhe::scene::Item_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::brush,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    brush->allow_shader_stages_override = false;

    auto rendertarget = make_renderpass("Rendertarget");
    rendertarget->mesh_layers    = { Mesh_layer_id::rendertarget };
    rendertarget->passes         = { &m_pipeline_renderpasses.rendertarget_meshes };
    rendertarget->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    rendertarget->filter = erhe::scene::Item_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::rendertarget,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    rendertarget->allow_shader_stages_override = false;

    auto tool = make_renderpass("Tool");
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
    tool->filter         = erhe::scene::Item_filter{
        .require_all_bits_set         = Item_flags::visible | Item_flags::tool,
        .require_at_least_one_bit_set = 0,
        .require_all_bits_clear       = 0
    };
    tool->override_scene_root = g_tools->get_tool_scene_root();
    tool->allow_shader_stages_override = false;
}

Editor_rendering_impl::~Editor_rendering_impl() noexcept
{
    ERHE_VERIFY(g_editor_rendering == this);
    g_editor_rendering = nullptr;
}

auto Editor_rendering_impl::get_pipeline_renderpass(
    const Renderpass& renderpass,
    const Blend_mode  blend_mode
) -> Pipeline_renderpass*
{
    using namespace erhe::primitive;
    switch (renderpass.primitive_mode) {
        case Primitive_mode::polygon_fill:
            switch (blend_mode) {
                case Blend_mode::opaque:      return &m_pipeline_renderpasses.polygon_fill_standard_opaque;
                case Blend_mode::translucent: return &m_pipeline_renderpasses.polygon_fill_standard_translucent;
                default:                      return nullptr;
            }
            break;

        case Primitive_mode::edge_lines:
            return &m_pipeline_renderpasses.edge_lines;

        case Primitive_mode::corner_points    : return &m_pipeline_renderpasses.corner_points;
        case Primitive_mode::corner_normals   : return &m_pipeline_renderpasses.edge_lines;
        case Primitive_mode::polygon_centroids: return &m_pipeline_renderpasses.polygon_centroids;
        default: return nullptr;
    }
}

auto Editor_rendering_impl::make_renderpass(const std::string_view name) -> std::shared_ptr<Renderpass>
{
    auto renderpass = std::make_shared<Renderpass>(name);
    m_composer.renderpasses.push_back(renderpass);
    return renderpass;
}

void Editor_rendering_impl::post_initialize()
{
    setup_renderpasses();

    // Init here (in main thread) so no scoped GL context needed
    m_content_timer = std::make_unique<erhe::graphics::Gpu_timer>("Content");
}

void Editor_rendering_impl::setup_renderpasses()
{
    m_pipeline_renderpasses.initialize();
}

void Pipeline_renderpasses::initialize()
{
    ERHE_PROFILE_FUNCTION();

    const bool reverse_depth = erhe::application::g_configuration->graphics.reverse_depth;

    auto* vertex_input = g_mesh_memory->vertex_input.get();

    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    polygon_fill_standard_opaque.pipeline.data = {
        .name           = "Polygon Fill Opaque",
        .shader_stages  = g_programs->circular_brushed_metal.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_disabled
    };

    polygon_fill_standard_translucent.pipeline.data = {
        .name           = "Polygon Fill Translucent",
        .shader_stages  = g_programs->circular_brushed_metal.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>();

    sky.pipeline.data = {
        .name           = "Sky",
        .shader_stages  = g_programs->sky.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state{
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = gl::Depth_function::equal, // Depth buffer must be cleared to the far plane value
            .stencil_test_enable = false
        },
        .color_blend    = Color_blend_state::color_blend_disabled
    };
    sky.begin = [](){ gl::depth_range(0.0f, 0.0f); };
    sky.end   = [](){ gl::depth_range(0.0f, 1.0f); };

    // Tool pass one: For hidden tool parts, set stencil to 1.
    // Only reads depth buffer, only writes stencil buffer.
    tool1_hidden_stencil.pipeline.data = {
        .name                    = "Tool pass 1: Tag depth hidden with stencil = 1",
        .shader_stages           = g_programs->tool.get(),
        .vertex_input            = vertex_input,
        .input_assembly          = Input_assembly_state::triangles,
        .rasterization           = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::greater),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_hidden,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_hidden,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
        },
        .color_blend             = Color_blend_state::color_writes_disabled
    };

    // Tool pass two: For visible tool parts, set stencil to 2.
    // Only reads depth buffer, only writes stencil buffer.
    tool2_visible_stencil.pipeline.data = {
        .name                    = "Tool pass 2: Tag visible tool parts with stencil = 2",
        .shader_stages           = g_programs->tool.get(),
        .vertex_input            = vertex_input,
        .input_assembly          = Input_assembly_state::triangles,
        .rasterization           = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = false,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::replace,
                .function        = gl::Stencil_function::always,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
        },
        .color_blend             = Color_blend_state::color_writes_disabled
    };

    // Tool pass three: Set depth to fixed value (with depth range)
    // Only writes depth buffer, depth test always.
    tool3_depth_clear.pipeline.data = {
        .name           = "Tool pass 3: Set depth to fixed value",
        .shader_stages  = g_programs->tool.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_always_stencil_test_disabled,
        .color_blend    = Color_blend_state::color_writes_disabled
    };
    tool3_depth_clear.begin = [](){ gl::depth_range(0.0f, 0.0f); };
    tool3_depth_clear.end   = [](){ gl::depth_range(0.0f, 1.0f); };

    // Tool pass four: Set depth to proper tool depth
    // Normal depth buffer update with depth test.
    tool4_depth.pipeline.data = {
        .name           = "Tool pass 4: Set depth to proper tool depth",
        .shader_stages  = g_programs->tool.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_writes_disabled
    };

    // Tool pass five: Render visible tool parts
    // Normal depth test, stencil test require 2, color writes enabled, no blending
    tool5_visible_color.pipeline.data = {
        .name                    = "Tool pass 5: Render visible tool parts",
        .shader_stages           = g_programs->tool.get(),
        .vertex_input            = vertex_input,
        .input_assembly          = Input_assembly_state::triangles,
        .rasterization           = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::keep,
                .function        = gl::Stencil_function::equal,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::keep,
                .function        = gl::Stencil_function::equal,
                .reference       = s_stencil_tool_mesh_visible,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
        },
        .color_blend             = Color_blend_state::color_blend_disabled
    };

    // Tool pass six: Render hidden tool parts
    // Normal depth test, stencil test requires 1, color writes enabled, blending
    tool6_hidden_color.pipeline.data = {
        .name                       = "Tool pass 6: Render hidden tool parts",
        .shader_stages              = g_programs->tool.get(),
        .vertex_input               = vertex_input,
        .input_assembly             = Input_assembly_state::triangles,
        .rasterization              = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil = {
            .depth_test_enable      = true,
            .depth_write_enable     = true,
            .depth_compare_op       = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable    = true,
            .stencil_front = {
                .stencil_fail_op    = gl::Stencil_op::keep,
                .z_fail_op          = gl::Stencil_op::keep,
                .z_pass_op          = gl::Stencil_op::keep,
                .function           = gl::Stencil_function::equal,
                .reference          = s_stencil_tool_mesh_hidden,
                .test_mask          = 0xffu,
                .write_mask         = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op    = gl::Stencil_op::keep,
                .z_fail_op          = gl::Stencil_op::keep,
                .z_pass_op          = gl::Stencil_op::replace,
                .function           = gl::Stencil_function::always,
                .reference          = s_stencil_tool_mesh_hidden,
                .test_mask          = 0xffu,
                .write_mask         = 0xffu
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
    };

    edge_lines.pipeline.data = {
        .name           = "Edge Lines",
        .shader_stages  = g_programs->wide_lines_draw_color.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::lines,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        //.depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .depth_stencil = {
            .depth_test_enable   = true,
            .depth_write_enable  = true,
            .depth_compare_op    = erhe::application::g_configuration->depth_function(gl::Depth_function::lequal),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            }
        },

        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    corner_points.pipeline.data = {
        .name           = "Corner Points",
        .shader_stages  = g_programs->points.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::points,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_disabled
    };

    polygon_centroids.pipeline.data = {
        .name           = "Polygon Centroids",
        .shader_stages  = g_programs->points.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::points,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_disabled
    };

    line_hidden_blend.pipeline.data = {
        .name                       = "Hidden lines with blending",
        .shader_stages              = g_programs->wide_lines_draw_color.get(),
        .vertex_input               = vertex_input,
        .input_assembly             = Input_assembly_state::lines,
        .rasterization              = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = {
            .depth_test_enable      = true,
            .depth_write_enable     = false,
            .depth_compare_op       = erhe::application::g_configuration->depth_function(gl::Depth_function::greater),
            .stencil_test_enable = true,
            .stencil_front = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
            },
            .stencil_back = {
                .stencil_fail_op = gl::Stencil_op::keep,
                .z_fail_op       = gl::Stencil_op::keep,
                .z_pass_op       = gl::Stencil_op::incr,
                .function        = gl::Stencil_function::equal,
                .reference       = 0,
                .test_mask       = 0xffu,
                .write_mask      = 0xffu
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
            .constant = { 0.0f, 0.0f, 0.0f, 0.2f }
        }
    };

    brush_back.pipeline.data = {
        .name           = "Brush back faces",
        .shader_stages  = g_programs->brush.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_front_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    brush_front.pipeline.data = {
        .name           = "Brush front faces",
        .shader_stages  = g_programs->brush.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    rendertarget_meshes.pipeline.data = {
        .name           = "Rendertarget Meshes",
        .shader_stages  = g_programs->textured.get(),
        .vertex_input   = vertex_input,
        .input_assembly = Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_none,
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = Color_blend_state::color_blend_premultiplied
    };

    // const erhe::application::Scoped_gl_context gl_context{
    //     Component::get<erhe::application::Gl_context_provider>()
    // };
    //
    // m_content_timer   = std::make_unique<erhe::graphics::Gpu_timer>("Content");
    //m_selection_timer = std::make_unique<erhe::graphics::Gpu_timer>("Selection");
    //m_gui_timer       = std::make_unique<erhe::graphics::Gpu_timer>("Gui");
    //m_brush_timer     = std::make_unique<erhe::graphics::Gpu_timer>("Brush");
    //m_tools_timer     = std::make_unique<erhe::graphics::Gpu_timer>("Tools");
}

void Editor_rendering_impl::trigger_capture()
{
    m_trigger_capture = true;
}

auto Editor_rendering_impl::width() const -> int
{
    return erhe::application::g_view->width();
}

auto Editor_rendering_impl::height() const -> int
{
    return erhe::application::g_view->height();
}

void Editor_rendering_impl::begin_frame()
{
    ERHE_PROFILE_FUNCTION();

    if (m_trigger_capture) {
        erhe::application::g_window->begin_renderdoc_capture();
    }

    erhe::application::Window_imgui_viewport* imgui_viewport = erhe::application::g_imgui_windows->get_window_viewport().get();

    g_viewport_windows->update_hover(imgui_viewport);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (g_headset_view != nullptr) {
        g_headset_view->begin_frame();
    }
#endif
}

void Editor_rendering_impl::end_frame()
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (g_headset_view != nullptr) {
        g_headset_view->end_frame();
    }
#endif

    if (g_post_processing != nullptr) {
        g_post_processing->next_frame();
    }

    if (erhe::application::g_line_renderer_set != nullptr) erhe::application::g_line_renderer_set->next_frame();
    if (erhe::application::g_text_renderer     != nullptr) erhe::application::g_text_renderer    ->next_frame();

    if (g_forward_renderer != nullptr) g_forward_renderer ->next_frame();
    if (g_id_renderer      != nullptr) g_id_renderer      ->next_frame();
    if (g_post_processing  != nullptr) g_post_processing  ->next_frame();
    if (g_shadow_renderer  != nullptr) g_shadow_renderer  ->next_frame();

    if (m_trigger_capture) {
        erhe::application::g_window->end_renderdoc_capture();
        m_trigger_capture = false;
    }
}

void Editor_rendering_impl::render()
{
    ERHE_PROFILE_FUNCTION();

    Expects(erhe::application::g_view != nullptr);

    begin_frame();
}

void Editor_rendering_impl::render_viewport_main(
    const Render_context& context,
    const bool            has_pointer
)
{
    ERHE_PROFILE_FUNCTION();
    static_cast<void>(has_pointer);

    gl::enable(gl::Enable_cap::scissor_test);
    gl::scissor(context.viewport.x, context.viewport.y, context.viewport.width, context.viewport.height);

    erhe::graphics::g_opengl_state_tracker->shader_stages.reset();
    erhe::graphics::g_opengl_state_tracker->color_blend.execute(Color_blend_state::color_blend_disabled);

    const auto& clear_color = context.viewport_config->clear_color;
    gl::clear_color(
        clear_color[0],
        clear_color[1],
        clear_color[2],
        clear_color[3]
    );
    gl::clear_stencil(0);
    gl::clear_depth_f(*erhe::application::g_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit |
        gl::Clear_buffer_mask::stencil_buffer_bit
    );

    if (g_forward_renderer) {
        render_composer(context);
    }

    if (
        (erhe::application::g_line_renderer_set != nullptr) &&
        (context.camera != nullptr)
    ) {
        erhe::application::g_line_renderer_set->begin();
        g_tools->render_tools(context);
        erhe::application::g_line_renderer_set->end();
        erhe::application::g_line_renderer_set->render(context.viewport, *context.camera);
    }

    if (erhe::application::g_text_renderer != nullptr) {
        erhe::application::g_text_renderer->render(context.viewport);
    }

    gl::disable(gl::Enable_cap::scissor_test);
}

void Editor_rendering_impl::render_composer(const Render_context& context)
{
    static constexpr std::string_view c_id_main{"Main"};
    ERHE_PROFILE_GPU_SCOPE(c_id_main);
    erhe::graphics::Scoped_gpu_timer timer{*m_content_timer.get()};
    erhe::graphics::Scoped_debug_group pass_scope{c_id_main};

    m_composer.render(context);

    erhe::graphics::g_opengl_state_tracker->depth_stencil.reset(); // workaround issue in stencil state tracking
}

void Editor_rendering_impl::render_id(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (
        (g_id_renderer      == nullptr) ||
        (context.scene_view == nullptr) ||
        (context.camera     == nullptr)
    ) {
        return;
    }

    const auto scene_root = context.scene_view->get_scene_root();
    if (!scene_root)
    {
        return;
    }

    const auto position_opt = context.viewport_window->get_position_in_viewport();
    if (!position_opt.has_value()) {
        return;
    }
    const auto position = position_opt.value();

    const auto& layers          = scene_root->layers();
    const auto& tool_scene_root = g_tools->get_tool_scene_root();
    if (!tool_scene_root) {
        return;
    }

    const auto& tool_layers = tool_scene_root->layers();

    // TODO listen to viewport changes in msg bus?
    g_id_renderer->render(
        Id_renderer::Render_parameters{
            .viewport           = context.viewport,
            .camera             = context.camera,
            .content_mesh_spans = { layers.content()->meshes, layers.rendertarget()->meshes },
            .tool_mesh_spans    = { tool_layers.tool()->meshes },
            .x                  = static_cast<int>(position.x),
            .y                  = static_cast<int>(position.y)
        }
    );
}

}  // namespace editor
