#include "renderers/forward_renderer.hpp"
#include "configuration.hpp"
#include "graphics/gl_context_provider.hpp"

#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/shadow_renderer.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <functional>

namespace editor
{

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

Forward_renderer::Forward_renderer()
    : Component    {c_name}
    , Base_renderer{std::string{c_name}}
{
}

Forward_renderer::~Forward_renderer()
{
}

void Forward_renderer::connect()
{
    base_connect(this);

    require<Gl_context_provider>();
    require<Program_interface  >();

    m_configuration          = require<Configuration>();
    m_mesh_memory            = require<Mesh_memory>();
    m_programs               = require<Programs   >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_shadow_renderer        = get<Shadow_renderer>();
}

static constexpr std::string_view c_forward_renderer_initialize_component{"Forward_renderer::initialize_component()"};
void Forward_renderer::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_forward_renderer_initialize_component.length()),
        c_forward_renderer_initialize_component.data()
    );

    create_frame_resources(256, 256, 256, 8000, 8000);

    const auto& shader_resources = *get<Program_interface>()->shader_resources.get();

    m_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>(
        shader_resources.attribute_mappings,
        m_mesh_memory->gl_vertex_format(),
        m_mesh_memory->gl_vertex_buffer.get(),
        m_mesh_memory->gl_index_buffer.get()
    );

    m_pipeline_fill.shader_stages  = m_programs->standard.get();
    m_pipeline_fill.vertex_input   = m_vertex_input.get();
    m_pipeline_fill.input_assembly = &Input_assembly_state::triangles;
    m_pipeline_fill.rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth);
    m_pipeline_fill.depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth);
    m_pipeline_fill.color_blend    = &Color_blend_state::color_blend_disabled;
    m_pipeline_fill.viewport       = nullptr;

    m_pipeline_gui.shader_stages  = m_programs->textured.get();
    m_pipeline_gui.vertex_input   = m_vertex_input.get();
    m_pipeline_gui.input_assembly = &Input_assembly_state::triangles;
    m_pipeline_gui.rasterization  = &Rasterization_state::cull_mode_none;
    m_pipeline_gui.depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth);
    m_pipeline_gui.color_blend    = &Color_blend_state::color_blend_premultiplied;
    m_pipeline_gui.viewport       = nullptr;

    m_depth_stencil_tool_set_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        false,                    // depth writes disabled
        m_configuration->depth_function(gl::Depth_function::greater),  // where depth is further
        true,                     // enable stencil operation
        {
            gl::Stencil_op::keep,         // on stencil test fail, do nothing
            gl::Stencil_op::keep,         // on depth test fail, do nothing
            gl::Stencil_op::replace,      // on depth test pass, set stencil to 1
            gl::Stencil_function::always, // stencil test always passes
            1u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::replace,
            gl::Stencil_function::always,
            1u,
            0xffffu,
            0xffffu
        },
    };

    m_depth_stencil_tool_set_visible = Depth_stencil_state{
        true,                   // depth test enabled
        false,                  // depth writes disabled
        m_configuration->depth_function(gl::Depth_function::lequal), // where depth is closer
        true,                   // enable stencil operation
        {
            gl::Stencil_op::keep,         // on stencil test fail, do nothing
            gl::Stencil_op::keep,         // on depth test fail, do nothing
            gl::Stencil_op::replace,      // on depth test pass, set stencil
            gl::Stencil_function::always, // stencil test always passes
            2u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::replace,
            gl::Stencil_function::always,
            2u,
            0xffffu,
            0xffffu
        },
    };

    m_depth_stencil_tool_test_for_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        true,                     //
        m_configuration->depth_function(gl::Depth_function::lequal),   //
        true,                     // enable stencil operation
        {
            gl::Stencil_op::keep,        // on stencil test fail, do nothing
            gl::Stencil_op::keep,        // on depth test fail, do nothing
            gl::Stencil_op::keep,        // on depth test pass, do nothing
            gl::Stencil_function::equal, // stencil test requires exact value 1
            1u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::replace,
            gl::Stencil_function::always,
            1u,
            0xffffu,
            0xffffu
        },
    };

    m_depth_stencil_tool_test_for_visible = Depth_stencil_state{
        true,                     // depth test enabled
        true,                     //
        m_configuration->depth_function(gl::Depth_function::lequal),   //
        true,                     // enable stencil operation
        {
            gl::Stencil_op::keep,        // on stencil test fail, do nothing
            gl::Stencil_op::keep,        // on depth test fail, do nothing
            gl::Stencil_op::keep,        // on depth test pass, do nothing
            gl::Stencil_function::equal, // stencil test requires exact value 2
            2u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_function::equal,
            2u,
            0xffffu,
            0xffffu
        },
    };

    m_color_blend_constant_point_six = Color_blend_state {
        true,
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        glm::vec4{0.0f, 0.0f, 0.0f, 0.6f},
        true,
        true,
        true,
        true
    };

    m_color_blend_constant_point_two = Color_blend_state {
        true,
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        {
            gl::Blend_equation_mode::func_add,
            gl::Blending_factor::constant_alpha,
            gl::Blending_factor::one_minus_constant_alpha
        },
        glm::vec4{0.0f, 0.0f, 0.0f, 0.2f},
        true,
        true,
        true,
        true
    };

    m_depth_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        false,                    // depth writes disabled
        m_configuration->depth_function(gl::Depth_function::greater),  // where depth is further
        false,                    // no stencil test
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_function::always,
            0u,
            0xffffu,
            0xffffu
        },
        {
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_op::keep,
            gl::Stencil_function::always,
            0u,
            0xffffu,
            0xffffu
        },
    };

    // Tool pass one: For hidden tool parts, set stencil to 1.
    // Only reads depth buffer, only writes stencil buffer.
    m_pipeline_tool_hidden_stencil_pass = {
        .shader_stages  = m_programs->tool.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = &m_depth_stencil_tool_set_hidden,
        .color_blend    = &Color_blend_state::color_writes_disabled
    };

    // Tool pass two: For visible tool parts, set stencil to 2.
    // Only reads depth buffer, only writes stencil buffer.
    m_pipeline_tool_visible_stencil_pass = {
        .shader_stages  = m_programs->tool.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = &m_depth_stencil_tool_set_visible,
        .color_blend    = &Color_blend_state::color_writes_disabled
    };

    // Tool pass three: Set depth to fixed value (with depth range)
    // Only writes depth buffer, depth test always.
    m_pipeline_tool_depth_clear_pass = {
        .shader_stages  = m_programs->tool.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = &Depth_stencil_state::depth_test_always_stencil_test_disabled,
        .color_blend    = &Color_blend_state::color_writes_disabled
    };

    // Tool pass four: Set depth to proper tool depth
    // Normal depth buffer update with depth test.
    m_pipeline_tool_depth_pass = {
        .shader_stages  = m_programs->tool.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
        .color_blend    = &Color_blend_state::color_writes_disabled
    };

    // Tool pass five: Render visible tool parts
    // Normal depth test, stencil test require 1, color writes enabled, no blending
    m_pipeline_tool_visible_color_pass = {
        .shader_stages  = m_programs->tool.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = &m_depth_stencil_tool_test_for_visible,
        .color_blend    = &Color_blend_state::color_blend_disabled
    };

    // Tool pass six: Render hidden tool parts
    // Normal depth test, stencil test requires 2, color writes enabled, blending
    m_pipeline_tool_hidden_color_pass = {
        .shader_stages  = m_programs->tool.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = &m_depth_stencil_tool_test_for_hidden,
        .color_blend    = &m_color_blend_constant_point_six
    };

    m_pipeline_edge_lines = {
        .shader_stages  = m_programs->wide_lines.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::lines,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
        .color_blend    = &Color_blend_state::color_blend_premultiplied
    };

    m_pipeline_points = {
        .shader_stages  = m_programs->points.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::points,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
        .color_blend    = &Color_blend_state::color_blend_disabled
    };

    m_pipeline_line_hidden_blend = {
        .shader_stages  = m_programs->wide_lines.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::lines,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = &m_depth_hidden,
        .color_blend    = &m_color_blend_constant_point_two
    };

    m_pipeline_brush_back = {
        .shader_stages  = m_programs->brush.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_front_cw(m_configuration->reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
        .color_blend    = &Color_blend_state::color_blend_premultiplied,
    };

    m_pipeline_brush_front = {
        .shader_stages  = m_programs->brush.get(),
        .vertex_input   = m_vertex_input.get(),
        .input_assembly = &Input_assembly_state::triangles,
        .rasterization  = Rasterization_state::cull_mode_back_ccw(m_configuration->reverse_depth),
        .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(m_configuration->reverse_depth),
        .color_blend    = &Color_blend_state::color_blend_premultiplied
    };

    gl::pop_debug_group();
}

auto Forward_renderer::select_pipeline(const Pass pass) const -> const erhe::graphics::Pipeline*
{
    switch (pass)
    {
        using enum Pass;
        case brush_back                                : return &m_pipeline_brush_back;
        case brush_front                               : return &m_pipeline_brush_front;
        case clear_depth                               : return &m_pipeline_tool_depth_clear_pass;
        case corner_points                             : return &m_pipeline_points;
        case depth_only                                : return &m_pipeline_tool_depth_pass;
        case edge_lines                                : return &m_pipeline_edge_lines;
        case gui                                       : return &m_pipeline_gui;
        case hidden_line_with_blend                    : return &m_pipeline_line_hidden_blend;
        case polygon_centroids                         : return &m_pipeline_points;
        case polygon_fill                              : return &m_pipeline_fill;
        case require_stencil_tag_depth_hidden_and_blend: return &m_pipeline_tool_hidden_color_pass;
        case require_stencil_tag_depth_visible         : return &m_pipeline_tool_visible_color_pass;
        case tag_depth_hidden_with_stencil             : return &m_pipeline_tool_hidden_stencil_pass;
        case tag_depth_visible_with_stencil            : return &m_pipeline_tool_visible_stencil_pass;
        default:
            ERHE_FATAL("bad pass %04x\n", static_cast<unsigned int>(pass));
    }
}

auto Forward_renderer::select_primitive_mode(const Pass pass) const -> erhe::primitive::Primitive_mode
{
    switch (pass)
    {
        using enum Pass;
        case brush_back                                : return erhe::primitive::Primitive_mode::polygon_fill;
        case brush_front                               : return erhe::primitive::Primitive_mode::polygon_fill;
        case clear_depth                               : return erhe::primitive::Primitive_mode::polygon_fill;
        case corner_points                             : return erhe::primitive::Primitive_mode::corner_points;
        case depth_only                                : return erhe::primitive::Primitive_mode::polygon_fill;
        case edge_lines                                : return erhe::primitive::Primitive_mode::edge_lines;
        case gui                                       : return erhe::primitive::Primitive_mode::polygon_fill;
        case hidden_line_with_blend                    : return erhe::primitive::Primitive_mode::edge_lines;
        case polygon_centroids                         : return erhe::primitive::Primitive_mode::polygon_centroids;
        case polygon_fill                              : return erhe::primitive::Primitive_mode::polygon_fill;
        case require_stencil_tag_depth_hidden_and_blend: return erhe::primitive::Primitive_mode::polygon_fill;
        case require_stencil_tag_depth_visible         : return erhe::primitive::Primitive_mode::polygon_fill;
        case tag_depth_hidden_with_stencil             : return erhe::primitive::Primitive_mode::polygon_fill;
        case tag_depth_visible_with_stencil            : return erhe::primitive::Primitive_mode::polygon_fill;
        default:
            ERHE_FATAL("bad pass %04x\n", static_cast<unsigned int>(pass));
    }
}

static constexpr std::string_view c_forward_renderer_render{"Forward_renderer::render()"};

void Forward_renderer::render(
    const erhe::scene::Viewport           viewport,
    const erhe::scene::ICamera&           camera,
    const Mesh_layer_collection&          mesh_layers,
    const erhe::scene::Light_layer&       light_layer,
    const Material_collection&            materials,
    const std::initializer_list<Pass>     passes,
    const erhe::scene::Visibility_filter& visibility_filter
)
{
    ERHE_PROFILE_FUNCTION

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(c_forward_renderer_render.length()),
        c_forward_renderer_render.data()
    );

    const unsigned int shadow_texture_unit = 0;
    const unsigned int shadow_texture_name = m_shadow_renderer->texture()->gl_name();
    gl::bind_sampler (shadow_texture_unit, m_programs->nearest_sampler->gl_name());
    gl::bind_textures(shadow_texture_unit, 1, &shadow_texture_name);
    gl::viewport     (viewport.x, viewport.y, viewport.width, viewport.height);
    for (auto& pass : passes)
    {
        const auto* const pipeline = select_pipeline(pass);
        if (pipeline == nullptr)
        {
            return;
        }

        const auto primitive_mode = select_primitive_mode(pass);

        if (pass == Pass::clear_depth)
        {
            gl::depth_range(0.0f, 0.0f);
        }

        const auto& pass_name = c_pass_strings[static_cast<size_t>(pass)];
        gl::push_debug_group(
            gl::Debug_source::debug_source_application,
            0,
            static_cast<GLsizei>(pass_name.length()),
            pass_name.data()
        );

        m_pipeline_state_tracker->execute(pipeline);
        gl::program_uniform_1i(
            pipeline->shader_stages->gl_name(),
            m_programs->shadow_sampler_location,
            shadow_texture_unit
        );
        update_material_buffer(materials);
        update_camera_buffer  (camera, viewport);
        bind_material_buffer  ();
        bind_camera_buffer    ();
        update_light_buffer   (light_layer, m_shadow_renderer->viewport());
        for (auto mesh_layer : mesh_layers)
        {
            ERHE_PROFILE_GPU_SCOPE(c_forward_renderer_render);

            update_primitive_buffer(*mesh_layer, visibility_filter);
            const auto draw_indirect_buffer_range = update_draw_indirect_buffer(*mesh_layer, primitive_mode, visibility_filter);
            if (draw_indirect_buffer_range.draw_indirect_count == 0)
            {
                continue;
            }

            bind_light_buffer();
            bind_primitive_buffer();
            bind_draw_indirect_buffer();

            gl::multi_draw_elements_indirect(
                pipeline->input_assembly->primitive_topology,
                m_mesh_memory->gl_index_type(),
                reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command))
            );
        }

        if (pass == Pass::clear_depth)
        {
            gl::depth_range(0.0f, 1.0f);
        }

        gl::pop_debug_group();
    }
    gl::pop_debug_group();

    // state leak insurance
    const unsigned int zero{0};
    gl::bind_sampler(shadow_texture_unit, 0);
    gl::bind_textures(shadow_texture_unit, 1, &zero);
}

} // namespace editor
