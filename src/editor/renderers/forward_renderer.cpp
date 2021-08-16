#include "renderers/forward_renderer.hpp"
#include "gl_context_provider.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/program_interface.hpp"
#include "renderers/shadow_renderer.hpp"
#include "scene/scene_manager.hpp"

#include "erhe/geometry/shapes/cone.hpp"
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
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace editor
{

using namespace erhe::toolkit;
using namespace erhe::graphics;
using namespace erhe::primitive;
using namespace erhe::scene;
using namespace gl;
using namespace glm;
using namespace std;

Forward_renderer::Forward_renderer()
    : Component(c_name)
{
}

Forward_renderer::~Forward_renderer() = default;

void Forward_renderer::connect()
{
    base_connect(this);

    require<Gl_context_provider>();
    require<Program_interface  >();

    m_mesh_memory            = require<Mesh_memory>();
    m_programs               = require<Programs   >();
    m_pipeline_state_tracker = get<OpenGL_state_tracker>();
    m_shadow_renderer        = get<Shadow_renderer>();
}

static constexpr std::string_view c_forward_renderer_initialize_component{"Forward_renderer::initialize_component()"};
void Forward_renderer::initialize_component()
{
    ZoneScoped;

    Scoped_gl_context gl_context(Component::get<Gl_context_provider>().get());

    gl::push_debug_group(gl::Debug_source::debug_source_application,
                         0,
                         static_cast<GLsizei>(c_forward_renderer_initialize_component.length()),
                         c_forward_renderer_initialize_component.data());

    create_frame_resources(256, 256, 256, 8000, 8000);

    m_vertex_input = std::make_unique<Vertex_input_state>(get<Program_interface>()->attribute_mappings,
                                                          m_mesh_memory->gl_vertex_format(),
                                                          m_mesh_memory->gl_vertex_buffer.get(),
                                                          m_mesh_memory->gl_index_buffer.get());

    m_pipeline_fill.shader_stages  = m_programs->standard.get();
    m_pipeline_fill.vertex_input   = m_vertex_input.get();
    m_pipeline_fill.input_assembly = &Input_assembly_state::triangles;
    m_pipeline_fill.rasterization  = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_fill.depth_stencil  = &Depth_stencil_state::depth_test_enabled_stencil_test_disabled;
    m_pipeline_fill.color_blend    = &Color_blend_state::color_blend_disabled;
    m_pipeline_fill.viewport       = nullptr;

    m_depth_stencil_tool_set_hidden = Depth_stencil_state{
        true,                     // depth test enabled
        false,                    // depth writes disabled
        Maybe_reversed::greater,  // where depth is further
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
        Maybe_reversed::lequal, // where depth is closer
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
        Maybe_reversed::lequal,   //
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
        Maybe_reversed::lequal,   //
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
        Maybe_reversed::greater,  // where depth is further
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
    m_pipeline_tool_hidden_stencil_pass.shader_stages  = m_programs->tool.get();
    m_pipeline_tool_hidden_stencil_pass.vertex_input   = m_vertex_input.get();
    m_pipeline_tool_hidden_stencil_pass.input_assembly = &Input_assembly_state::triangles;
    m_pipeline_tool_hidden_stencil_pass.rasterization  = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_tool_hidden_stencil_pass.depth_stencil  = &m_depth_stencil_tool_set_hidden;
    m_pipeline_tool_hidden_stencil_pass.color_blend    = &Color_blend_state::color_writes_disabled;
    m_pipeline_tool_hidden_stencil_pass.viewport       = nullptr;

    // Tool pass two: For visible tool parts, set stencil to 2.
    // Only reads depth buffer, only writes stencil buffer.
    m_pipeline_tool_visible_stencil_pass.shader_stages  = m_programs->tool.get();
    m_pipeline_tool_visible_stencil_pass.vertex_input   = m_vertex_input.get();
    m_pipeline_tool_visible_stencil_pass.input_assembly = &Input_assembly_state::triangles;
    m_pipeline_tool_visible_stencil_pass.rasterization  = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_tool_visible_stencil_pass.depth_stencil  = &m_depth_stencil_tool_set_visible;
    m_pipeline_tool_visible_stencil_pass.color_blend    = &Color_blend_state::color_writes_disabled;
    m_pipeline_tool_visible_stencil_pass.viewport       = nullptr;

    // Tool pass three: Set depth to fixed value (with depth range)
    // Only writes depth buffer, depth test always.
    m_pipeline_tool_depth_clear_pass.shader_stages      = m_programs->tool.get();
    m_pipeline_tool_depth_clear_pass.vertex_input       = m_vertex_input.get();
    m_pipeline_tool_depth_clear_pass.input_assembly     = &Input_assembly_state::triangles;
    m_pipeline_tool_depth_clear_pass.rasterization      = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_tool_depth_clear_pass.depth_stencil      = &Depth_stencil_state::depth_test_always_stencil_test_disabled;
    m_pipeline_tool_depth_clear_pass.color_blend        = &Color_blend_state::color_writes_disabled;
    m_pipeline_tool_depth_clear_pass.viewport           = nullptr;

    // Tool pass four: Set depth to proper tool depth
    // Normal depth buffer update with depth test.
    m_pipeline_tool_depth_pass.shader_stages            = m_programs->tool.get();
    m_pipeline_tool_depth_pass.vertex_input             = m_vertex_input.get();
    m_pipeline_tool_depth_pass.input_assembly           = &Input_assembly_state::triangles;
    m_pipeline_tool_depth_pass.rasterization            = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_tool_depth_pass.depth_stencil            = &Depth_stencil_state::depth_test_enabled_stencil_test_disabled;
    m_pipeline_tool_depth_pass.color_blend              = &Color_blend_state::color_writes_disabled;
    m_pipeline_tool_depth_pass.viewport                 = nullptr;

    // Tool pass five: Render visible tool parts
    // Normal depth test, stencil test require 1, color writes enabled, no blending
    m_pipeline_tool_visible_color_pass.shader_stages    = m_programs->tool.get();
    m_pipeline_tool_visible_color_pass.vertex_input     = m_vertex_input.get();
    m_pipeline_tool_visible_color_pass.input_assembly   = &Input_assembly_state::triangles;
    m_pipeline_tool_visible_color_pass.rasterization    = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_tool_visible_color_pass.depth_stencil    = &m_depth_stencil_tool_test_for_visible;
    m_pipeline_tool_visible_color_pass.color_blend      = &Color_blend_state::color_blend_disabled;
    m_pipeline_tool_visible_color_pass.viewport         = nullptr;

    // Tool pass six: Render hidden tool parts
    // Normal depth test, stencil test requires 2, color writes enabled, blending
    m_pipeline_tool_hidden_color_pass.shader_stages     = m_programs->tool.get();
    m_pipeline_tool_hidden_color_pass.vertex_input      = m_vertex_input.get();
    m_pipeline_tool_hidden_color_pass.input_assembly    = &Input_assembly_state::triangles;
    m_pipeline_tool_hidden_color_pass.rasterization     = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_tool_hidden_color_pass.depth_stencil     = &m_depth_stencil_tool_test_for_hidden;
    m_pipeline_tool_hidden_color_pass.color_blend       = &m_color_blend_constant_point_six;
    m_pipeline_tool_hidden_color_pass.viewport          = nullptr;

    //m_pipeline_edge_lines.shader_stages  = m_programs->edge_lines.get();
    m_pipeline_edge_lines.shader_stages  = m_programs->wide_lines.get();
    m_pipeline_edge_lines.vertex_input   = m_vertex_input.get();
    m_pipeline_edge_lines.input_assembly = &Input_assembly_state::lines;
    m_pipeline_edge_lines.rasterization  = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_edge_lines.depth_stencil  = &Depth_stencil_state::depth_test_enabled_stencil_test_disabled;
    m_pipeline_edge_lines.color_blend    = &Color_blend_state::color_blend_premultiplied;
    m_pipeline_edge_lines.viewport       = nullptr;

    m_pipeline_points.shader_stages  = m_programs->points.get();
    m_pipeline_points.vertex_input   = m_vertex_input.get();
    m_pipeline_points.input_assembly = &Input_assembly_state::points;
    m_pipeline_points.rasterization  = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_points.depth_stencil  = &Depth_stencil_state::depth_test_enabled_stencil_test_disabled;
    m_pipeline_points.color_blend    = &Color_blend_state::color_blend_disabled;
    m_pipeline_points.viewport       = nullptr;

    m_pipeline_line_hidden_blend.shader_stages  = m_programs->wide_lines.get();
    m_pipeline_line_hidden_blend.vertex_input   = m_vertex_input.get();
    m_pipeline_line_hidden_blend.input_assembly = &Input_assembly_state::lines;
    m_pipeline_line_hidden_blend.rasterization  = &Rasterization_state::cull_mode_back_ccw;
    m_pipeline_line_hidden_blend.depth_stencil  = &m_depth_hidden;
    m_pipeline_line_hidden_blend.color_blend    = &m_color_blend_constant_point_two;
    m_pipeline_line_hidden_blend.viewport       = nullptr;

    gl::pop_debug_group();
}

auto Forward_renderer::select_pipeline(Pass pass) const -> const erhe::graphics::Pipeline*
{
    switch (pass)
    {
        case Pass::polygon_fill                              : return &m_pipeline_fill;
        case Pass::edge_lines                                : return &m_pipeline_edge_lines;
        case Pass::polygon_centroids                         : return &m_pipeline_points;
        case Pass::corner_points                             : return &m_pipeline_points;
        case Pass::tag_depth_hidden_with_stencil             : return &m_pipeline_tool_hidden_stencil_pass;
        case Pass::tag_depth_visible_with_stencil            : return &m_pipeline_tool_visible_stencil_pass;
        case Pass::clear_depth                               : return &m_pipeline_tool_depth_clear_pass;
        case Pass::depth_only                                : return &m_pipeline_tool_depth_pass;
        case Pass::require_stencil_tag_depth_visible         : return &m_pipeline_tool_visible_color_pass;
        case Pass::require_stencil_tag_depth_hidden_and_blend: return &m_pipeline_tool_hidden_color_pass;
        case Pass::hidden_line_with_blend                    : return &m_pipeline_line_hidden_blend;
        default:
            FATAL("bad pass\n");
    }
}

auto Forward_renderer::select_primitive_mode(Pass pass) const -> erhe::primitive::Primitive_mode
{
    switch (pass)
    {
        case Pass::polygon_fill                              : return Primitive_mode::polygon_fill;
        case Pass::edge_lines                                : return Primitive_mode::edge_lines;
        case Pass::polygon_centroids                         : return Primitive_mode::polygon_centroids;
        case Pass::corner_points                             : return Primitive_mode::corner_points;
        case Pass::tag_depth_hidden_with_stencil             : return Primitive_mode::polygon_fill;
        case Pass::tag_depth_visible_with_stencil            : return Primitive_mode::polygon_fill;
        case Pass::clear_depth                               : return Primitive_mode::polygon_fill;
        case Pass::depth_only                                : return Primitive_mode::polygon_fill;
        case Pass::require_stencil_tag_depth_visible         : return Primitive_mode::polygon_fill;
        case Pass::require_stencil_tag_depth_hidden_and_blend: return Primitive_mode::polygon_fill;
        case Pass::hidden_line_with_blend                    : return Primitive_mode::edge_lines;
        default:
            FATAL("bad pass\n");
    }
}

static constexpr std::string_view c_forward_renderer_render{"Forward_renderer::render()"};
void Forward_renderer::render(Viewport                          viewport,
                              ICamera&                          camera,
                              Layer_collection&                 layers,
                              const Material_collection&        materials,
                              const std::initializer_list<Pass> passes,
                              const uint64_t                    visibility_mask)
{
    ZoneScoped;

    gl::push_debug_group(gl::Debug_source::debug_source_application,
                         0,
                         static_cast<GLsizei>(c_forward_renderer_render.length()),
                         c_forward_renderer_render.data());
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
        gl::push_debug_group(gl::Debug_source::debug_source_application,
                             0,
                             static_cast<GLsizei>(pass_name.length()),
                             pass_name.data());

        m_pipeline_state_tracker->execute(pipeline);
        gl::program_uniform_1i(pipeline->shader_stages->gl_name(),
                               m_programs->shadow_sampler_location,
                               shadow_texture_unit);
        update_material_buffer(materials);
        update_camera_buffer  (camera, viewport);
        bind_material_buffer();
        bind_camera_buffer();
        for (auto layer : layers)
        {
            TracyGpuZone(c_forward_renderer_render.data())

            update_light_buffer    (layer->lights, m_shadow_renderer->viewport(), layer->ambient_light);
            update_primitive_buffer(layer->meshes, visibility_mask);
            const auto draw_indirect_buffer_range = update_draw_indirect_buffer(layer->meshes, primitive_mode, visibility_mask);

            bind_light_buffer();
            bind_primitive_buffer();
            bind_draw_indirect_buffer();

            gl::multi_draw_elements_indirect(pipeline->input_assembly->primitive_topology,
                                             m_mesh_memory->gl_index_type(),
                                             reinterpret_cast<const void *>(draw_indirect_buffer_range.range.first_byte_offset),
                                             static_cast<GLsizei>(draw_indirect_buffer_range.draw_indirect_count),
                                             static_cast<GLsizei>(sizeof(gl::Draw_elements_indirect_command)));
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
