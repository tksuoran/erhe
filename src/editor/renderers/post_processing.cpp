#include "renderers/post_processing.hpp"
#include "editor_imgui_windows.hpp"

#include "graphics/gl_context_provider.hpp"
#include "graphics/shader_monitor.hpp"

#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/texture.hpp"

#include <imgui.h>

#include <algorithm>

namespace editor
{


auto factorial(const int input) -> int
{
    int result = 1;
    for (int i = 1; i <= input; i++)
    {
        result = result * i;
    }
    return result;
}

// Computes the n-th coefficient from Pascal's triangle binomial coefficients.
auto binom(
    const int row_index,
    const int column_index = -1
) -> int
{
    return
        factorial(row_index) /
        (
            factorial(row_index - column_index) *
            factorial(column_index)
        );
}

class Kernel
{
public:
    std::vector<float> weights;
    std::vector<float> offsets;
};

// Compute discrete weights and factors
auto kernel_binom(
    const int taps,
    const int expand_by = 0,
    const int reduce_by = 0
) -> Kernel
{
    const auto row          = taps - 1 + (expand_by << 1);
    const auto coeffs_count = row + 1;
    const auto radius       = taps >> 1;

    // sanity check, avoid duped coefficients at center
    if ((coeffs_count & 1) == 0)
    {
        return {}; // ValueError("Duped coefficients at center")
    }

    // compute total weight
    // https://en.wikipedia.org/wiki/Power_of_two
    // TODO: seems to be not optimal ...
    int sum = 0;
    for (int x = 0; x < reduce_by; ++x)
    {
        sum += 2 * binom(row, x);
    }
    const auto total = float(1 << row) - sum;

    // compute final weights
    Kernel result;
    for (
        int x = reduce_by + radius;
        x > reduce_by - 1;
        --x
    )
    {
        result.weights.push_back(binom(row, x) / total);
    }
    for (
        int offset = 0;
        offset <= radius;
        ++offset
    )
    {
        result.offsets.push_back(static_cast<float>(offset));
    }
    return result;
}

// Compute linearly interpolated weights and factors
auto kernel_binom_linear(const Kernel& discrete_data) -> Kernel
{
    const auto& wd = discrete_data.weights;
    const auto& od = discrete_data.offsets;

    const int w_count = static_cast<int>(wd.size());

    // sanity checks
    const auto pairs = w_count - 1;
    if ((w_count & 1) == 0)
    {
        return {};
        //raise ValueError("Duped coefficients at center")
    }

    if ((pairs % 2 != 0))
    {
        return {};
        //raise ValueError("Can't perform bilinear reduction on non-paired texels")
    }

    Kernel result;
    result.weights.push_back(wd[0]);
    for (int x = 1; x < w_count - 1; x += 2)
    {
        result.weights.push_back(wd[x] + wd[x + 1]);
    }

    result.offsets.push_back(0);
    for (
        int x = 1;
        x < w_count - 1;
        x += 2
    )
    {
        int i = (x - 1) / 2;
        const float value =
            (
                od[x    ] * wd[x] +
                od[x + 1] * wd[x + 1]
            ) / result.weights[i + 1];
        result.offsets.push_back(value);
    }

    return result;
}

Post_processing::Post_processing()
    : Component   {c_name }
    , Imgui_window{c_title}
{
}

void Post_processing::connect()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
    m_programs               = get<Programs>();
    m_shader_monitor         = require<Shader_monitor>();

    require<Editor_imgui_windows>();
    require<Gl_context_provider >();
}

void Post_processing::initialize_component()
{
    using erhe::graphics::Shader_stages;

    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    m_fragment_outputs.add(
        "out_color",
        gl::Fragment_shader_output_type::float_vec4,
        0
    );

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>();

    m_parameter_block = std::make_unique<erhe::graphics::Shader_resource>(
        "post_processing",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    );
    m_texel_scale_offset    = m_parameter_block->add_float("texel_scale"       )->offset_in_parent();
    m_texture_count_offset  = m_parameter_block->add_uint ("texture_count"     )->offset_in_parent();
    m_reserved0_offset      = m_parameter_block->add_float("reserved0"         )->offset_in_parent();
    m_reserved1_offset      = m_parameter_block->add_float("reserver1"         )->offset_in_parent();
    m_source_texture_offset = m_parameter_block->add_uvec2("source_texture", 32)->offset_in_parent();

    const auto shader_path = fs::path("res") / fs::path("shaders");
    const fs::path vs_path         = shader_path / fs::path("post_processing.vert");
    const fs::path x_fs_path       = shader_path / fs::path("downsample_x.frag");
    const fs::path y_fs_path       = shader_path / fs::path("downsample_y.frag");
    const fs::path compose_fs_path = shader_path / fs::path("compose.frag");
    Shader_stages::Create_info x_create_info{
        .name             = "downsample_x",
        .fragment_outputs = &m_fragment_outputs,
    };
    Shader_stages::Create_info y_create_info{
        .name             = "downsample_y",
        .fragment_outputs = &m_fragment_outputs,
    };
    Shader_stages::Create_info compose_create_info{
        .name             = "compose",
        .fragment_outputs = &m_fragment_outputs,
    };

    // GL_ARB_gpu_shader_int64
    x_create_info      .extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    y_create_info      .extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    compose_create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
    x_create_info      .shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    y_create_info      .shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    compose_create_info.shaders.emplace_back(gl::Shader_type::vertex_shader,   vs_path);
    x_create_info      .shaders.emplace_back(gl::Shader_type::fragment_shader, x_fs_path);
    y_create_info      .shaders.emplace_back(gl::Shader_type::fragment_shader, y_fs_path);
    compose_create_info.shaders.emplace_back(gl::Shader_type::fragment_shader, compose_fs_path);
    x_create_info      .add_interface_block(m_parameter_block.get());
    y_create_info      .add_interface_block(m_parameter_block.get());
    compose_create_info.add_interface_block(m_parameter_block.get());

    Shader_stages::Prototype x_prototype      {x_create_info};
    Shader_stages::Prototype y_prototype      {y_create_info};
    Shader_stages::Prototype compose_prototype{compose_create_info};
    m_downsample_x_shader_stages = std::make_unique<Shader_stages>(std::move(x_prototype));
    m_downsample_y_shader_stages = std::make_unique<Shader_stages>(std::move(y_prototype));
    m_compose_shader_stages      = std::make_unique<Shader_stages>(std::move(compose_prototype));
    if (m_shader_monitor)
    {
        m_shader_monitor->add(x_create_info,       m_downsample_x_shader_stages.get());
        m_shader_monitor->add(y_create_info,       m_downsample_y_shader_stages.get());
        m_shader_monitor->add(compose_create_info, m_compose_shader_stages.get());
    }
    m_downsample_x_pipeline.data =
    {
        .name           = "Post Procesisng Downsample X",
        .shader_stages  = m_downsample_x_shader_stages.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled,
    };
    m_downsample_y_pipeline.data =
    {
        .name           = "Post Processing Downsample Y",
        .shader_stages  = m_downsample_y_shader_stages.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled,
    };
    m_compose_pipeline.data =
    {
        .name           = "Post Processing Compose",
        .shader_stages  = m_compose_shader_stages.get(),
        .vertex_input   = m_empty_vertex_input.get(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled,
    };

    create_frame_resources();

    m_gpu_timer = std::make_unique<erhe::graphics::Gpu_timer>("Post_processing");

    get<Editor_imgui_windows>()->register_imgui_window(this);
}

Rendertarget::Rendertarget(
    const std::string& label,
    const int          width,
    const int          height)
{
    using erhe::graphics::Framebuffer;
    using erhe::graphics::Texture;

    texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba16f,
            //.internal_format = gl::Internal_format::rgba32f,
            .sample_count    = 0,
            .width           = width,
            .height          = height
        }
    );
    texture->set_debug_label(label);
    //const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    //gl::clear_tex_image(
    //    texture->gl_name(),
    //    0,
    //    gl::Pixel_format::rgba,
    //    gl::Pixel_type::float_,
    //    &clear_value[0]
    //);

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, texture.get());
    framebuffer = std::make_unique<Framebuffer>(create_info);
    framebuffer->set_debug_label(label);
}

void Rendertarget::bind_framebuffer()
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
    gl::viewport        (0, 0, texture->width(), texture->height());
    const auto invalidate_attachment = gl::Invalidate_framebuffer_attachment::color_attachment0;
    gl::invalidate_framebuffer(gl::Framebuffer_target::draw_framebuffer, 1, &invalidate_attachment);
}

void Post_processing::create_frame_resources()
{
    for (size_t slot = 0; slot < s_frame_resources_count; ++slot)
    {
        m_frame_resources.emplace_back(
            m_parameter_block->size_bytes(),
            100,
            slot
        );
    }
}

void Post_processing::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_parameter_writer.reset();
}

auto Post_processing::current_frame_resources() -> Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Post_processing::imgui()
{
    //ImGui::DragInt("Taps",   &m_taps,   1.0f, 1, 32);
    //ImGui::DragInt("Expand", &m_expand, 1.0f, 0, 32);
    //ImGui::DragInt("Reduce", &m_reduce, 1.0f, 0, 32);
    //ImGui::Checkbox("Linear", &m_linear);

    //const auto discrete = kernel_binom(m_taps, m_expand, m_reduce);
    //if (ImGui::TreeNodeEx("Discrete", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    //{
    //    for (size_t i = 0; i < discrete.weights.size(); ++i)
    //    {
    //        ImGui::Text(
    //            "W: %.3f O: %.3f",
    //            discrete.weights.at(i),
    //            discrete.offsets.at(i)
    //        );
    //    }
    //    ImGui::TreePop();
    //}
    //if (m_linear)
    //{
    //    const auto linear = kernel_binom_linear(discrete);
    //    if (ImGui::TreeNodeEx("Linear", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    //    {
    //        for (size_t i = 0; i < linear.weights.size(); ++i)
    //        {
    //            ImGui::Text(
    //                "W: %.3f O: %.3f",
    //                linear.weights.at(i),
    //                linear.offsets.at(i)
    //            );
    //        }
    //        ImGui::TreePop();
    //    }
    //}

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    for (auto& rendertarget : m_rendertargets)
    {
        if (
            !rendertarget.texture                ||
            (rendertarget.texture->width () < 1) ||
            (rendertarget.texture->height() < 1)
        )
        {
            continue;
        }

        image(
            rendertarget.texture,
            rendertarget.texture->width (),
            rendertarget.texture->height()
        );
    }
    ImGui::PopStyleVar();
}

[[nodiscard]] auto Post_processing::get_output() -> std::shared_ptr<erhe::graphics::Texture>
{
    if (m_rendertargets.empty())
    {
        return {};
    }
    return m_rendertargets.front().texture;
}

void Post_processing::post_process(
    erhe::graphics::Texture* source_texture
)
{
    if (
        (source_texture == nullptr)    ||
        (source_texture->width () < 1) ||
        (source_texture->height() < 1)
    )
    {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{"Post Processing"};
    erhe::graphics::Scoped_gpu_timer   timer     {*m_gpu_timer.get()};

    if (
        (m_source_width  != source_texture->width ()) ||
        (m_source_height != source_texture->height())
    )
    {
        m_source_width  = source_texture->width ();
        m_source_height = source_texture->height();
        m_rendertargets.clear();
        int width  = source_texture->width ();
        int height = source_texture->height();
        m_rendertargets.emplace_back("Post Processing Compose", width, height);
        for (;;)
        {
            if (width > 1)
            {
                width = width / 2;
                m_rendertargets.emplace_back("Post Processing Downsample X", width, height);
                if ((width + height) == 2)
                {
                    break;
                }
            }
            if (height > 1)
            {
                height = height / 2;
                m_rendertargets.emplace_back("Post Processing Downsample Y", width, height);
                if ((width + height) == 2)
                {
                    break;
                }
            }
        }
    }

    {
        size_t i      = 1;
        int    width  = source_texture->width ();
        int    height = source_texture->height();
        const erhe::graphics::Texture* source = source_texture;
        for (;;)
        {
            if (width > 1)
            {
                width = width / 2;
                erhe::graphics::Scoped_debug_group downsample_scope{"Downsample X"};
                downsample(source, m_rendertargets.at(i), m_downsample_x_pipeline);
                source = m_rendertargets.at(i).texture.get();
                i++;
                if ((width + height) == 2)
                {
                    break;
                }
            }

            if (height > 1)
            {
                height = height / 2;
                erhe::graphics::Scoped_debug_group downsample_scope{"Downsample Y"};
                downsample(source, m_rendertargets.at(i), m_downsample_y_pipeline);
                source = m_rendertargets.at(i).texture.get();
                i++;
                if ((width + height) == 2)
                {
                    break;
                }
            }
        }
    }
    compose(source_texture);
}

void Post_processing::downsample(
    const erhe::graphics::Texture*  source_texture,
    Rendertarget&                   rendertarget,
    const erhe::graphics::Pipeline& pipeline
)
{
    auto& parameter_buffer   = current_frame_resources().parameter_buffer;
    auto  parameter_gpu_data = parameter_buffer.map();

    m_parameter_writer.begin(parameter_buffer.target());

    std::byte* const          start      = parameter_gpu_data.data() + m_parameter_writer.write_offset;
    const size_t              byte_count = parameter_gpu_data.size_bytes();
    const size_t              word_count = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    const uint64_t handle = erhe::graphics::get_handle(
        *source_texture,
        *m_programs->linear_sampler.get()
    );

    size_t word_offset = 0;
    gpu_float_data[word_offset++] = 1.0f / source_texture->width();
    gpu_uint_data [word_offset++] = 1;
    gpu_float_data[word_offset++] = 0.0f;
    gpu_float_data[word_offset++] = 0.0f;
    gpu_uint_data [word_offset++] = (handle & 0xffffffffu);
    gpu_uint_data [word_offset++] = handle >> 32u;
    m_parameter_writer.write_offset += m_parameter_block->size_bytes();
    m_parameter_writer.end();

    rendertarget.bind_framebuffer();

    m_pipeline_state_tracker->execute(pipeline);

    gl::bind_buffer_range(
        parameter_buffer.target(),
        static_cast<GLuint>    (m_parameter_block->binding_point()),
        static_cast<GLuint>    (parameter_buffer.gl_name()),
        static_cast<GLintptr>  (m_parameter_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_parameter_writer.range.byte_count)
    );
    gl::make_texture_handle_resident_arb(handle);

    gl::draw_arrays     (pipeline.data.input_assembly.primitive_topology, 0, 4);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::make_texture_handle_non_resident_arb(handle);
}

void Post_processing::compose(const erhe::graphics::Texture* source_texture)
{
    auto& parameter_buffer   = current_frame_resources().parameter_buffer;
    auto  parameter_gpu_data = parameter_buffer.map();

    m_parameter_writer.begin(parameter_buffer.target());

    std::byte* const          start      = parameter_gpu_data.data() + m_parameter_writer.write_offset;
    const size_t              byte_count = parameter_gpu_data.size_bytes();
    const size_t              word_count = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    size_t word_offset = 0;
    gpu_float_data[word_offset++] = 0.0f;
    gpu_uint_data [word_offset++] = static_cast<uint32_t>(m_rendertargets.size());
    gpu_float_data[word_offset++] = 0.0f;
    gpu_float_data[word_offset++] = 0.0f;

    {
        const uint64_t handle = erhe::graphics::get_handle(
            *source_texture,
            *m_programs->linear_sampler.get()
        );

        gl::make_texture_handle_resident_arb(handle);

        gpu_uint_data[word_offset++] = (handle & 0xffffffffu);
        gpu_uint_data[word_offset++] = handle >> 32u;
        gpu_uint_data[word_offset++] = 0; // padding in uvec2 array in uniform buffer (std140)
        gpu_uint_data[word_offset++] = 0;
    }

    for (
        size_t i = 1, end = std::min(m_rendertargets.size(), size_t{31});
        i < end;
        ++i
    )
    {
        const auto&    source = m_rendertargets.at(i);
        const uint64_t handle = erhe::graphics::get_handle(
            *source.texture.get(),
            *m_programs->linear_sampler.get()
        );

        gl::make_texture_handle_resident_arb(handle);

        gpu_uint_data[word_offset++] = (handle & 0xffffffffu);
        gpu_uint_data[word_offset++] = handle >> 32u;
        gpu_uint_data[word_offset++] = 0; // padding in uvec2 array in uniform buffer (std140)
        gpu_uint_data[word_offset++] = 0;
    }
    m_parameter_writer.write_offset += m_parameter_block->size_bytes();
    m_parameter_writer.end();

    auto&       rendertarget = m_rendertargets.at(0);
    const auto& pipeline     = m_compose_pipeline;

    rendertarget.bind_framebuffer();

    m_pipeline_state_tracker->execute(pipeline);

    gl::bind_buffer_range(
        parameter_buffer.target(),
        static_cast<GLuint>    (m_parameter_block->binding_point()),
        static_cast<GLuint>    (parameter_buffer.gl_name()),
        static_cast<GLintptr>  (m_parameter_writer.range.first_byte_offset),
        static_cast<GLsizeiptr>(m_parameter_writer.range.byte_count)
    );

    gl::draw_arrays     (pipeline.data.input_assembly.primitive_topology, 0, 4);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

    {
        const uint64_t handle = erhe::graphics::get_handle(
            *source_texture,
            *m_programs->linear_sampler.get()
        );

        gl::make_texture_handle_non_resident_arb(handle);
    }

    for (
        size_t i = 1, end = std::min(m_rendertargets.size(), size_t{32});
        i < end;
        ++i
    )
    {
        const auto&    source = m_rendertargets.at(i);
        const uint64_t handle = erhe::graphics::get_handle(
            *source.texture.get(),
            *m_programs->linear_sampler.get()
        );

        gl::make_texture_handle_non_resident_arb(handle);
    }
}

} // namespace editor
