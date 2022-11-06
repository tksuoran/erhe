#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>

#include <iomanip>
#include <cstdarg>

namespace erhe::application
{

namespace
{

static constexpr gl::Buffer_storage_mask storage_mask{
    gl::Buffer_storage_mask::map_coherent_bit   |
    gl::Buffer_storage_mask::map_persistent_bit |
    gl::Buffer_storage_mask::map_write_bit
};
static constexpr gl::Map_buffer_access_mask access_mask{
    gl::Map_buffer_access_mask::map_coherent_bit   |
    gl::Map_buffer_access_mask::map_persistent_bit |
    gl::Map_buffer_access_mask::map_write_bit
};

}

using erhe::graphics::Shader_stages;
using glm::mat4;
using glm::vec3;
using glm::vec4;

Line_renderer_pipeline::Line_renderer_pipeline()
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , attribute_mappings{
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_position",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::position
            },
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::color
            }
        }
    }
    , vertex_format{
        erhe::graphics::Vertex_attribute{
            .usage =
            {
                .type      = erhe::graphics::Vertex_attribute::Usage_type::position
            },
            .shader_type   = gl::Attribute_type::float_vec4,
            .data_type =
            {
                .type      = gl::Vertex_attrib_type::float_,
                .dimension = 4
            }
        },
        erhe::graphics::Vertex_attribute{
            .usage =
            {
                .type       = erhe::graphics::Vertex_attribute::Usage_type::color,
            },
            .shader_type    = gl::Attribute_type::float_vec4,
            .data_type =
            {
                .type       = gl::Vertex_attrib_type::float_,
                .dimension  = 4
            }
        }
    }
{
}

Line_renderer_set::Line_renderer_set()
    : Component{c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Line_renderer_set::~Line_renderer_set() noexcept
{
}

void Line_renderer_set::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Gl_context_provider>();
    require<Configuration      >();
    require<Shader_monitor     >();
}

static constexpr std::string_view c_line_renderer_initialize_component{"Line_renderer_set::initialize_component()"};

void Line_renderer_set::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    erhe::graphics::Scoped_debug_group line_renderer_initialization{c_line_renderer_initialize_component};

    m_pipeline.initialize(get<Shader_monitor>().get());

    const Configuration& configuration = *get<Configuration>().get();

    for (unsigned int stencil_reference = 0; stencil_reference <= s_max_stencil_reference; ++stencil_reference)
    {
        visible.at(stencil_reference) = std::make_unique<Line_renderer>("visible", 8u + stencil_reference, &m_pipeline, configuration);
        hidden .at(stencil_reference) = std::make_unique<Line_renderer>("hidden",  8u + stencil_reference, &m_pipeline, configuration);
    }
}

void Line_renderer_set::post_initialize()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Line_renderer_pipeline::initialize(Shader_monitor* shader_monitor)
{
    view_block = std::make_unique<erhe::graphics::Shader_resource>(
        "view",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    );

    clip_from_world_offset        = view_block->add_mat4("clip_from_world"       )->offset_in_parent();
    view_position_in_world_offset = view_block->add_vec4("view_position_in_world")->offset_in_parent();
    viewport_offset               = view_block->add_vec4("viewport"              )->offset_in_parent();
    fov_offset                    = view_block->add_vec4("fov"                   )->offset_in_parent();

    {
        ERHE_PROFILE_SCOPE("shader");

        const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
        const std::filesystem::path vs_path = shader_path / std::filesystem::path("line.vert");
        const std::filesystem::path gs_path = shader_path / std::filesystem::path("line.geom");
        const std::filesystem::path fs_path = shader_path / std::filesystem::path("line.frag");
        Shader_stages::Create_info create_info{
            .name                      = "line",
            .defines                   = {
                { "ERHE_LINE_SHADER_SHOW_DEBUG_LINES",        "0"},
                { "ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES", "0"},
                { "ERHE_LINE_SHADER_STRIP",                   "1"}
            },
            .interface_blocks          = { view_block.get() },
            .vertex_attribute_mappings = &attribute_mappings,
            .fragment_outputs          = &fragment_outputs,
            .shaders = {
                { gl::Shader_type::vertex_shader,   vs_path },
                { gl::Shader_type::geometry_shader, gs_path },
                { gl::Shader_type::fragment_shader, fs_path }
            }
        };

        Shader_stages::Prototype prototype(create_info);
        if (prototype.is_valid())
        {
            shader_stages = std::make_unique<Shader_stages>(std::move(prototype));

            if (shader_monitor != nullptr)
            {
                shader_monitor->add(create_info, shader_stages.get());
            }
        }
        else
        {
            const auto current_path = std::filesystem::current_path();
            log_startup->error(
                "Unable to load Line_renderer shader - check working directory '{}'",
                current_path.string()
            );
        }
    }
}

void Line_renderer_set::begin()
{
    for (auto& entry : visible) entry->begin();
    for (auto& entry : hidden ) entry->begin();
}

void Line_renderer_set::end()
{
    for (auto& entry : visible) entry->end();
    for (auto& entry : hidden ) entry->end();
}

void Line_renderer_set::next_frame()
{
    for (auto& entry : visible) entry->next_frame();
    for (auto& entry : hidden ) entry->next_frame();
}

void Line_renderer_set::imgui()
{
    for (auto& entry: hidden ) entry->imgui();
    for (auto& entry: visible) entry->imgui();
}

void Line_renderer_set::render(
    const erhe::scene::Viewport viewport,
    const erhe::scene::Camera&  camera
)
{
    auto& state_tracker = *m_pipeline_state_tracker.get();
    for (auto& entry: hidden ) entry->render(state_tracker, viewport, camera, true, false);
    for (auto& entry: visible) entry->render(state_tracker, viewport, camera, true, true);
    state_tracker.depth_stencil.reset(); // workaround issue in stencil state tracking
}

[[nodiscard]] auto Line_renderer::Frame_resources::make_pipeline(
    const bool                           reverse_depth,
    erhe::graphics::Shader_stages* const shader_stages,
    const bool                           visible,
    const unsigned int                   stencil_reference
) -> erhe::graphics::Pipeline
{
    const gl::Depth_function depth_compare_op0 = visible ? gl::Depth_function::less : gl::Depth_function::gequal;
    const gl::Depth_function depth_compare_op  = reverse_depth ? erhe::graphics::reverse(depth_compare_op0) : depth_compare_op0;
    return erhe::graphics::Pipeline{
        {
            .name           = "Line Renderer depth pass",
            .shader_stages  = shader_stages,
            .vertex_input   = &vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::lines,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = depth_compare_op,
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = gl::Stencil_op::keep,
                    .z_fail_op       = gl::Stencil_op::keep,
                    .z_pass_op       = gl::Stencil_op::replace,
                    .function        = gl::Stencil_function::gequal,
                    .reference       = stencil_reference,
                    .test_mask       = 0xffu,
                    .write_mask      = 0xffu
                },
                .stencil_back = {
                    .stencil_fail_op = gl::Stencil_op::keep,
                    .z_fail_op       = gl::Stencil_op::keep,
                    .z_pass_op       = gl::Stencil_op::replace,
                    .function        = gl::Stencil_function::gequal,
                    .reference       = stencil_reference,
                    .test_mask       = 0xffu,
                    .write_mask      = 0xffu
                },
            },

            .color_blend    = visible ? erhe::graphics::Color_blend_state::color_blend_premultiplied : erhe::graphics::Color_blend_state{
                .enabled  = true,
                .rgb      = {
                    .equation_mode      = gl::Blend_equation_mode::func_add,
                    .source_factor      = gl::Blending_factor::constant_alpha,
                    .destination_factor = gl::Blending_factor::one_minus_constant_alpha
                },
                .alpha    = {
                    .equation_mode      = gl::Blend_equation_mode::func_add,
                    .source_factor      = gl::Blending_factor::constant_alpha,
                    .destination_factor = gl::Blending_factor::one_minus_constant_alpha
                },
                .constant = { 0.0f, 0.0f, 0.0f, 0.1f },
            }
        }
    };
}

Line_renderer::Frame_resources::Frame_resources(
    const unsigned int                        stencil_reference,
    const bool                                reverse_depth,
    const std::size_t                         view_stride,
    const std::size_t                         view_count,
    const std::size_t                         vertex_count,
    erhe::graphics::Shader_stages* const      shader_stages,
    erhe::graphics::Vertex_attribute_mappings attribute_mappings,
    erhe::graphics::Vertex_format&            vertex_format,
    const std::string&                        style_name,
    const std::size_t                         slot
)
    : vertex_buffer{
        gl::Buffer_target::array_buffer,
        vertex_format.stride() * vertex_count,
        storage_mask,
        access_mask
    }
    , view_buffer{
        gl::Buffer_target::uniform_buffer,
        view_stride * view_count,
        storage_mask,
        access_mask
    }
    , vertex_input{
        erhe::graphics::Vertex_input_state_data::make(
            attribute_mappings,
            vertex_format,
            &vertex_buffer,
            nullptr
        )
    }
    , pipeline_visible{make_pipeline(reverse_depth, shader_stages, true,  stencil_reference)}
    , pipeline_hidden {make_pipeline(reverse_depth, shader_stages, false, stencil_reference)}
{
    vertex_buffer.set_debug_label(fmt::format("Line Renderer {} Vertex {}", style_name, slot));
    view_buffer  .set_debug_label(fmt::format("Line Renderer {} View {}",   style_name, slot));
}

Line_renderer::Line_renderer(
    const char* const       name,
    const unsigned int      stencil_reference,
    Line_renderer_pipeline* pipeline,
    const Configuration&    configuration
)
    : m_name    {name}
    , m_pipeline{pipeline}
{
    ERHE_PROFILE_FUNCTION

    const auto            reverse_depth = configuration.graphics.reverse_depth;
    constexpr std::size_t vertex_count  = 512 * 1024;
    constexpr std::size_t view_stride   = 256;
    constexpr std::size_t view_count    = 16;
    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot)
    {
        m_frame_resources.emplace_back(
            stencil_reference,
            reverse_depth,
            view_stride,
            view_count,
            vertex_count,
            pipeline->shader_stages.get(),
            pipeline->attribute_mappings,
            pipeline->vertex_format,
            m_name,
            slot
        );
    }
}

auto Line_renderer::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Line_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_view_writer  .reset();
    m_vertex_writer.reset();
    m_line_count = 0;
}

void Line_renderer::begin()
{
    ERHE_VERIFY(!m_inside_begin_end);

    m_view_writer  .begin();
    m_vertex_writer.begin();
    m_line_count = 0;
    m_inside_begin_end = true;
}

void Line_renderer::end()
{
    ERHE_VERIFY(m_inside_begin_end);

    m_inside_begin_end = false;
    m_vertex_writer.end();
}

void Line_renderer::put(
    const glm::vec3&        point,
    const float             thickness,
    const glm::vec4&        color,
    const gsl::span<float>& gpu_float_data,
    std::size_t&            word_offset
)
{
    gpu_float_data[word_offset++] = point.x;
    gpu_float_data[word_offset++] = point.y;
    gpu_float_data[word_offset++] = point.z;
    gpu_float_data[word_offset++] = thickness;
    gpu_float_data[word_offset++] = color.r;
    gpu_float_data[word_offset++] = color.g;
    gpu_float_data[word_offset++] = color.b;
    gpu_float_data[word_offset++] = color.a;
}

void Line_renderer::add_lines(
    const glm::mat4&                  transform,
    const std::initializer_list<Line> lines
)
{
    ERHE_VERIFY(m_inside_begin_end);

    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    std::byte* const       start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    const std::size_t      byte_count = vertex_gpu_data.size_bytes();
    const std::size_t      word_count = byte_count / sizeof(float);
    const gsl::span<float> gpu_float_data{reinterpret_cast<float*   >(start), word_count};

    std::size_t word_offset = 0;
    for (const Line& line : lines)
    {
        const glm::vec4 p0{transform * glm::vec4{line.p0, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{line.p1, 1.0f}};
        put(vec3{p0} / p0.w, m_line_thickness, m_line_color, gpu_float_data, word_offset);
        put(vec3{p1} / p1.w, m_line_thickness, m_line_color, gpu_float_data, word_offset);
    }

    m_vertex_writer.write_offset += lines.size() * 2 * m_pipeline->vertex_format.stride();
    m_line_count += lines.size();
}

void Line_renderer::add_lines(
    const glm::mat4&                   transform,
    const std::initializer_list<Line4> lines
)
{
    ERHE_VERIFY(m_inside_begin_end);

    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    std::byte* const       start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    const std::size_t      byte_count = vertex_gpu_data.size_bytes();
    const std::size_t      word_count = byte_count / sizeof(float);
    const gsl::span<float> gpu_float_data{reinterpret_cast<float*   >(start), word_count};

    std::size_t word_offset = 0;
    for (const Line4& line : lines)
    {
        const glm::vec4 p0{transform * glm::vec4{glm::vec3{line.p0}, 1.0f}};
        const glm::vec4 p1{transform * glm::vec4{glm::vec3{line.p1}, 1.0f}};
        put(vec3{p0} / p0.w, line.p0.w, m_line_color, gpu_float_data, word_offset);
        put(vec3{p1} / p1.w, line.p1.w, m_line_color, gpu_float_data, word_offset);
    }

    m_vertex_writer.write_offset += lines.size() * 2 * m_pipeline->vertex_format.stride();
    m_line_count += lines.size();
}

void Line_renderer::set_line_color(const float r, const float g, const float b, const float a)
{
    ERHE_VERIFY(m_inside_begin_end);

    m_line_color = glm::vec4{r, g, b, a};
}

void Line_renderer::set_line_color(const glm::vec3& color)
{
    ERHE_VERIFY(m_inside_begin_end);

    m_line_color = glm::vec4{color, 1.0f};
}

void Line_renderer::set_line_color(const glm::vec4& color)
{
    m_line_color = color;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Line_renderer::set_line_color(const ImVec4 color)
{
    ERHE_VERIFY(m_inside_begin_end);

    m_line_color = glm::vec4{color.x, color.y, color.z, color.w};
}
#endif

void Line_renderer::set_thickness(const float thickness)
{
    ERHE_VERIFY(m_inside_begin_end);

    m_line_thickness = thickness;
}

void Line_renderer::add_lines(
    const std::initializer_list<Line> lines
)
{
    ERHE_VERIFY(m_inside_begin_end);

    auto vertex_gpu_data = current_frame_resources().vertex_buffer.map();

    std::byte* const       start      = vertex_gpu_data.data() + m_vertex_writer.write_offset;
    const std::size_t      byte_count = vertex_gpu_data.size_bytes();
    const std::size_t      word_count = byte_count / sizeof(float);
    const gsl::span<float> gpu_float_data{reinterpret_cast<float*   >(start), word_count};

    std::size_t word_offset = 0;
    for (const Line& line : lines)
    {
        put(line.p0, m_line_thickness, m_line_color, gpu_float_data, word_offset);
        put(line.p1, m_line_thickness, m_line_color, gpu_float_data, word_offset);
    }

    m_vertex_writer.write_offset += lines.size() * 2 * m_pipeline->vertex_format.stride();
    m_line_count += lines.size();
}

void Line_renderer::add_cube(
    const glm::mat4& transform,
    const glm::vec4& color,
    const glm::vec3& min_corner,
    const glm::vec3& max_corner,
    const bool       z_cross
)
{
    const auto a = min_corner;
    const auto b = max_corner;
    vec3 p[8] = {
        vec3{a.x, a.y, a.z},
        vec3{b.x, a.y, a.z},
        vec3{b.x, b.y, a.z},
        vec3{a.x, b.y, a.z},
        vec3{a.x, a.y, b.z},
        vec3{b.x, a.y, b.z},
        vec3{b.x, b.y, b.z},
        vec3{a.x, b.y, b.z}
    };
    add_lines(
        transform,
        color,
        {
            // near plane
            { p[0], p[1] },
            { p[1], p[2] },
            { p[2], p[3] },
            { p[3], p[0] },

            // far plane
            { p[4], p[5] },
            { p[5], p[6] },
            { p[6], p[7] },
            { p[7], p[4] },

            // near to far
            { p[0], p[4] },
            { p[1], p[5] },
            { p[2], p[6] },
            { p[3], p[7] }
        }
    );
    if (z_cross)
    {
        add_lines(
            transform,
            color,
            {
                // near to far middle
                { 0.5f * p[0] + 0.5f * p[1], 0.5f * p[4] + 0.5f * p[5] },
                { 0.5f * p[1] + 0.5f * p[2], 0.5f * p[5] + 0.5f * p[6] },
                { 0.5f * p[2] + 0.5f * p[3], 0.5f * p[6] + 0.5f * p[7] },
                { 0.5f * p[3] + 0.5f * p[0], 0.5f * p[7] + 0.5f * p[4] },

                // near+far/2 plane
                { 0.5f * p[0] + 0.5f * p[4], 0.5f * p[1] + 0.5f * p[5] },
                { 0.5f * p[1] + 0.5f * p[5], 0.5f * p[2] + 0.5f * p[6] },
                { 0.5f * p[2] + 0.5f * p[6], 0.5f * p[3] + 0.5f * p[7] },
                { 0.5f * p[3] + 0.5f * p[7], 0.5f * p[0] + 0.5f * p[4] },
            }
        );
    }
}

namespace {

auto safe_normalize_cross(const glm::vec3& lhs, const glm::vec3& rhs)
{
    const glm::vec3 lhs_normalized = glm::normalize(lhs);
    const glm::vec3 rhs_normalized = glm::normalize(rhs);
    const float d = glm::dot(lhs_normalized, rhs_normalized);
    if (std::abs(d) > 0.999f)
    {
        return erhe::toolkit::min_axis(lhs);
    }

    const glm::vec3 c0 = glm::cross(lhs, rhs);
    if (glm::length(c0) < glm::epsilon<float>())
    {
        return erhe::toolkit::min_axis(lhs);
    }
    return glm::normalize(c0);
}

}

void Line_renderer::imgui()
{
    for (const auto& fun : m_imgui)
    {
        fun();
    }
    m_imgui.clear();
}

void Line_renderer::add_sphere(
    const erhe::scene::Transform&       transform,
    const glm::vec4&                    edge_color,
    const glm::vec4&                    great_circle_color,
    const float                         edge_thickness,
    const float                         great_circle_thickness,
    const glm::vec3&                    local_center,
    const float                         radius,
    const erhe::scene::Transform* const camera_world_from_node,
    const int                           step_count
)
{
    const glm::mat4 m      = transform.matrix();
    const glm::vec3 center = glm::vec3{m * glm::vec4{local_center, 1.0f}};
    const glm::vec3 axis_x{radius, 0.0f, 0.0f};
    const glm::vec3 axis_y{0.0f, radius, 0.0f};
    const glm::vec3 axis_z{0.0f, 0.0f, radius};
    const glm::mat4 I{1.0f};
    set_thickness(great_circle_thickness);
    for (int i = 0; i < step_count; ++i)
    {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(step_count);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(step_count);
        add_lines(
            great_circle_color,
            {
                {
                    center + std::cos(t0) * axis_x + std::sin(t0) * axis_y,
                    center + std::cos(t1) * axis_x + std::sin(t1) * axis_y
                },
                {
                    center + std::cos(t0) * axis_y + std::sin(t0) * axis_z,
                    center + std::cos(t1) * axis_y + std::sin(t1) * axis_z
                },
                {
                    center + std::cos(t0) * axis_x + std::sin(t0) * axis_z,
                    center + std::cos(t1) * axis_x + std::sin(t1) * axis_z
                }
            }
        );
        //add_lines(
        //    0xffff0000,
        //    {
        //        {
        //            center + std::cos(t0) * axis_x + std::sin(t0) * axis_y,
        //            center + std::cos(t1) * axis_x + std::sin(t1) * axis_y
        //        }
        //    }
        //);
        //add_lines(
        //    0xff0000ff,
        //    {
        //        {
        //            center + std::cos(t0) * axis_y + std::sin(t0) * axis_z,
        //            center + std::cos(t1) * axis_y + std::sin(t1) * axis_z
        //        }
        //    }
        //);
        //add_lines(
        //    0xff00ff00,
        //    {
        //        {
        //            center + std::cos(t0) * axis_x + std::sin(t0) * axis_z,
        //            center + std::cos(t1) * axis_x + std::sin(t1) * axis_z
        //        }
        //    }
        //);
    }

    if (camera_world_from_node == nullptr)
    {
        return;
    }

    //                             C = sphere center        .
    //                             r = sphere radius        .
    //         /|                  V = camera center        .
    //        / |  .               d = distance(C, V)       .
    //      r/  |     . b          d*d = r*r + b*b          .
    //      /   |h       .         d*d - r*r = b*b          .
    //     /    |           .      b = sqrt(d*d - r*r)      .
    //    /___p_|_____q________.   h = (r*b) / d            .
    //   C      P d             V  p*p + h*h = r*r          .
    //                             p = sqrt(r*r - h*h)      .

    const glm::vec3 camera_position                 = glm::vec3{camera_world_from_node->matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    const glm::vec3 from_camera_to_sphere           = center - camera_position;
    const glm::vec3 from_sphere_to_camera           = camera_position - center;
    const glm::vec3 from_camera_to_sphere_direction = glm::normalize(from_camera_to_sphere);
    const glm::vec3 from_sphere_to_camera_direction = glm::normalize(from_sphere_to_camera);

    const float r2 = radius * radius;
    const float d2 = glm::length2(from_camera_to_sphere);
    const float d  = std::sqrt(d2);
    const float b2 = d2 - r2;
    const float b  = std::sqrt(b2);
    const float h  = radius * b / d;
    const float h2 = h * h;
    const float p  = std::sqrt(r2 - h2);

    const glm::vec3 P = center + p * from_sphere_to_camera_direction;

    const glm::vec3 up0_direction  = glm::vec3{camera_world_from_node->matrix() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 side_direction = safe_normalize_cross(from_camera_to_sphere_direction, up0_direction);
    const glm::vec3 up_direction   = safe_normalize_cross(side_direction, from_camera_to_sphere_direction);
    const glm::vec3 axis_a = h * side_direction;
    const glm::vec3 axis_b = h * up_direction;

    //// m_imgui.emplace_back(
    ////     [=]
    ////     ()
    ////     {
    ////         const float sin_alpha = radius / d;
    ////         const float alpha     = std::asin(sin_alpha);
    ////         ImGui::Text("d = %f", d);
    ////         ImGui::Text("h = %f", h);
    ////         ImGui::Text("p = %f", p);
    ////         ImGui::Text("Cone = %f rad", 2.0f * alpha);
    ////         ImGui::Text("Cone = %f deg", glm::degrees(2.0f * alpha));
    ////     }
    //// );

    set_thickness(edge_thickness);
    for (int i = 0; i < step_count; ++i)
    {
        const float t0 = glm::two_pi<float>() * static_cast<float>(i    ) / static_cast<float>(step_count);
        const float t1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(step_count);
        add_lines(
            //0xffffffff,
            edge_color,
            {
                {
                    P + std::cos(t0) * axis_a + std::sin(t0) * axis_b,
                    P + std::cos(t1) * axis_a + std::sin(t1) * axis_b
                }
            }
        );
    }
}

static constexpr std::string_view c_line_renderer_render{"Line_renderer::render()"};

void Line_renderer::render(
    erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker,
    const erhe::scene::Viewport           viewport,
    const erhe::scene::Camera&            camera,
    const bool                            show_visible_lines,
    const bool                            show_hidden_lines
)
{
    if (m_line_count == 0)
    {
        return;
    }

    ERHE_PROFILE_FUNCTION
    ERHE_PROFILE_GPU_SCOPE(c_line_renderer_render)

    erhe::graphics::Scoped_debug_group line_renderer_initialization{c_line_renderer_render};

    m_view_writer.begin();

    const auto  projection_transforms  = camera.projection_transforms(viewport);
    const mat4  clip_from_world        = projection_transforms.clip_from_world.matrix();
    const vec4  view_position_in_world = camera.position_in_world();
    const auto  fov_sides              = camera.projection()->get_fov_sides(viewport);
    auto* const view_buffer            = &current_frame_resources().view_buffer;
    const auto  view_gpu_data          = view_buffer->map();
    const float viewport_floats[4] {
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    };
    const float fov_floats[4] {
        fov_sides.left,
        fov_sides.right,
        fov_sides.up,
        fov_sides.down
    };

    using erhe::graphics::write;
    using erhe::graphics::as_span;
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->clip_from_world_offset,        as_span(clip_from_world       ));
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->view_position_in_world_offset, as_span(view_position_in_world));
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->viewport_offset,               as_span(viewport_floats       ));
    write(view_gpu_data, m_view_writer.write_offset + m_pipeline->fov_offset,                    as_span(fov_floats            ));

    m_view_writer.write_offset += m_pipeline->view_block->size_bytes();
    m_view_writer.end();

    gl::disable          (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable           (gl::Enable_cap::sample_alpha_to_coverage);
    gl::enable           (gl::Enable_cap::sample_alpha_to_one);
    gl::viewport         (viewport.x, viewport.y, viewport.width, viewport.height);
    gl::bind_buffer_range(
        view_buffer->target(),
        static_cast<GLuint>    (m_pipeline->view_block->binding_point()),
        static_cast<GLuint>    (view_buffer->gl_name()),
        static_cast<GLintptr>  (m_view_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_view_writer.range.byte_count)
    );

    const auto first = static_cast<GLint>(
        m_vertex_writer.range.first_byte_offset / m_pipeline->vertex_format.stride()
    );
    const auto count = static_cast<GLsizei>(m_line_count * 2);

    if (show_hidden_lines)
    {
        const auto& pipeline = current_frame_resources().pipeline_hidden;
        pipeline_state_tracker.execute(pipeline);

        gl::draw_arrays(
            pipeline.data.input_assembly.primitive_topology,
            first,
            count
        );
    }

    if (show_visible_lines)
    {
        const auto& pipeline = current_frame_resources().pipeline_visible;
        pipeline_state_tracker.execute(pipeline);

        gl::draw_arrays(
            pipeline.data.input_assembly.primitive_topology,
            first,
            count
        );
    }

    gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    gl::disable(gl::Enable_cap::sample_alpha_to_one);
}

} // namespace erhe::application
