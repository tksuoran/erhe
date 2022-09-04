#include "rendergraph/post_processing.hpp"
#include "editor_log.hpp"
#include "renderers/programs.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <algorithm>

namespace editor
{

Post_processing::Offsets::Offsets(
    erhe::graphics::Shader_resource& block,
    const std::size_t                source_texture_count
)
    : texel_scale   {block.add_float("texel_scale"                         )->offset_in_parent()}
    , texture_count {block.add_uint ("texture_count"                       )->offset_in_parent()}
    , reserved0     {block.add_float("reserved0"                           )->offset_in_parent()}
    , reserved1     {block.add_float("reserved1"                           )->offset_in_parent()}
    , source_texture{block.add_uvec2("source_texture", source_texture_count)->offset_in_parent()}
{
}

/// //////////////////////////////////////////

Downsample_node::Downsample_node()
{
}

Downsample_node::Downsample_node(
    const std::string& label,
    const int          width,
    const int          height,
    const int          axis
)
    : axis{axis}
{
    ERHE_VERIFY(width > 0);
    ERHE_VERIFY(height > 0);
    using erhe::graphics::Framebuffer;
    using erhe::graphics::Texture;

    texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba16f, // TODO other formats
            .sample_count    = 0,
            .width           = width,
            .height          = height
        }
    );
    texture->set_debug_label(label);

    // TODO initial clear?
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
    framebuffer = std::make_shared<Framebuffer>(create_info);
    framebuffer->set_debug_label(label);
}

void Downsample_node::bind_framebuffer() const
{
    ERHE_PROFILE_FUNCTION

    {
        ERHE_PROFILE_SCOPE("bind");

        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
    }

    {
        static constexpr std::string_view c_invalidate_framebuffer{"invalidate framebuffer"};
        ERHE_PROFILE_SCOPE("viewport");
        ERHE_PROFILE_GPU_SCOPE(c_invalidate_framebuffer)

        gl::viewport(0, 0, texture->width(), texture->height());
    }

    //{
    //    ERHE_PROFILE_SCOPE("invalidate");
    //    const auto invalidate_attachment = gl::Invalidate_framebuffer_attachment::color_attachment0;
    //    gl::invalidate_framebuffer(gl::Framebuffer_target::draw_framebuffer, 1, &invalidate_attachment);
    //}
}

/// //////////////////////////////////////////

Post_processing_node::Post_processing_node(
    const std::string_view name,
    Post_processing&       post_processing
)
    : erhe::application::Rendergraph_node{name}
    , m_post_processing{post_processing}
{
    register_input (
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );
    register_output(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        "viewport",
        erhe::application::Rendergraph_node_key::viewport
    );
}

auto Post_processing_node::update_downsample_nodes() -> bool
{
    constexpr bool downsample_nodes_unchanged = true;
    constexpr bool downsample_nodes_changed   = false;

    // Output determines the size of intermediate nodes
    // and size of the input node for the post processing
    // render graph node.
    //
    // Output *should* be multisample resolved
    const auto viewport = get_producer_output_viewport(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );

    if (
        (m_width  == viewport.width) &&
        (m_height == viewport.height)
    )
    {
        return downsample_nodes_unchanged; // post processing nodes are
    }

    m_downsample_nodes.clear();

    if (
        (viewport.width  < 1) ||
        (viewport.height < 1)
    )
    {
        log_post_processing->info(
            "Resizing Post_processing_node '{}' to 0 x 0",
            name()
        );
        if (
            (m_width  != 0) ||
            (m_height != 0)
        )
        {
            m_width  = 0;
            m_height = 0;
            return downsample_nodes_changed;
        }
        return downsample_nodes_unchanged;
    }

    m_width  = viewport.width;
    m_height = viewport.height;

    int width  = m_width;
    int height = m_height;

    log_post_processing->info(
        "Resizing Post_processing_node '{}' to {} x {}",
        name(),
        m_width,
        m_height
    );

    // First node is used the post processing input texture
    m_downsample_nodes.emplace_back("Post Processing Input", width, height, -1);

    for (;;)
    {
        if (
            (width == 1) &&
            (height == 1)
        )
        {
            break;
        }
        if (
            (width >= height) &&
            (width > 1)
        )
        {
            width = width / 2;
            m_downsample_nodes.emplace_back("Post Processing Downsample X", width, height, 0);
        }
        if (
            (height > width) &&
            (height > 1)
        )
        {
            height = height / 2;
            m_downsample_nodes.emplace_back("Post Processing Downsample Y", width, height, 1);
            if ((width + height) == 2)
            {
                break;
            }
        }
    }

    return downsample_nodes_changed;
}

void Post_processing_node::imgui_toolbar()
{
    ImGui::Checkbox("Post Processing", &m_enabled);
}

[[nodiscard]] auto Post_processing_node::get_consumer_input_texture(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(key); // TODO Validate
    static_cast<void>(depth);
    return !m_downsample_nodes.empty()
        ? m_downsample_nodes.front().texture
        : std::shared_ptr<erhe::graphics::Texture>{};
}

// Overridden to provide framebuffer from the first downsample node
[[nodiscard]] auto Post_processing_node::get_consumer_input_framebuffer(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(key); // TODO Validate
    static_cast<void>(depth);
    return !m_downsample_nodes.empty()
        ? m_downsample_nodes.front().framebuffer
        : std::shared_ptr<erhe::graphics::Framebuffer>{};
}

[[nodiscard]] auto Post_processing_node::get_consumer_input_viewport(
    const erhe::application::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::scene::Viewport
{
    return get_producer_output_viewport(resource_routing, key, depth + 1);
}

void Post_processing_node::execute_rendergraph_node()
{
    if (!m_enabled)
    {
        return;
    }

    static constexpr std::string_view c_post_processing{"Post_processing"};

    ERHE_PROFILE_FUNCTION
    ERHE_PROFILE_GPU_SCOPE(c_post_processing)

    const bool downsample_nodes_unchanged = update_downsample_nodes();
    if (!downsample_nodes_unchanged)
    {
        return;
    }

    m_post_processing.post_process(*this);
}

[[nodiscard]] auto Post_processing_node::get_downsample_nodes() -> const std::vector<Downsample_node>&
{
    return m_downsample_nodes;
}

/// //////////////////////////////////////////
/// //////////////////////////////////////////

Post_processing::Post_processing()
    : Component{c_type_name}
    , m_parameter_block{
        "post_processing",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_offsets{m_parameter_block, m_source_texture_count}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
{
}

void Post_processing::declare_required_components()
{
    require<erhe::application::Configuration      >();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Shader_monitor     >();
    require<Programs>();
}

void Post_processing::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    using erhe::graphics::Shader_stages;

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    m_linear_sampler  = get<Programs>()->linear_sampler.get();
    m_nearest_sampler = get<Programs>()->nearest_sampler.get();

    m_empty_vertex_input = std::make_unique<erhe::graphics::Vertex_input_state>();

    {
        ERHE_PROFILE_SCOPE("shader");

        const auto shader_path = fs::path("res") / fs::path("shaders");
        const fs::path vs_path         = shader_path / fs::path("post_processing.vert");
        const fs::path x_fs_path       = shader_path / fs::path("downsample_x.frag");
        const fs::path y_fs_path       = shader_path / fs::path("downsample_y.frag");
        const fs::path compose_fs_path = shader_path / fs::path("compose.frag");

        Shader_stages::Create_info x_create_info{
            .name             = "downsample_x",
            .interface_blocks = { &m_parameter_block },
            .fragment_outputs = &m_fragment_outputs,
            .shaders          = {
                { gl::Shader_type::vertex_shader,   vs_path   },
                { gl::Shader_type::fragment_shader, x_fs_path }
            }
        };
        Shader_stages::Create_info y_create_info{
            .name             = "downsample_y",
            .interface_blocks = { &m_parameter_block },
            .fragment_outputs = &m_fragment_outputs,
            .shaders          = {
                { gl::Shader_type::vertex_shader,   vs_path   },
                { gl::Shader_type::fragment_shader, y_fs_path }
            }
        };
        Shader_stages::Create_info compose_create_info{
            .name             = "compose",
            .interface_blocks = { &m_parameter_block },
            .fragment_outputs = &m_fragment_outputs,
            .shaders          = {
                { gl::Shader_type::vertex_shader,   vs_path         },
                { gl::Shader_type::fragment_shader, compose_fs_path }
            }
        };

        if (erhe::graphics::Instance::info.use_bindless_texture)
        {
            x_create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
            y_create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
            compose_create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
            x_create_info      .extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
            y_create_info      .extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
            compose_create_info.extensions.push_back({gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"});
        }
        else
        {
            m_source_texture_count = std::min(
                m_source_texture_count,
                static_cast<size_t>(erhe::graphics::Instance::limits.max_texture_image_units)
            );

            m_downsample_source_texture = m_downsample_default_uniform_block.add_sampler(
                "s_source",
                gl::Uniform_type::sampler_2d,
                0
            );
            m_compose_source_textures = m_compose_default_uniform_block.add_sampler(
                "s_source_textures",
                gl::Uniform_type::sampler_2d,
                0,
                m_source_texture_count
            );

            m_dummy_texture = erhe::graphics::create_dummy_texture();
            x_create_info.default_uniform_block       = &m_downsample_default_uniform_block;
            y_create_info.default_uniform_block       = &m_downsample_default_uniform_block;
            compose_create_info.default_uniform_block = &m_compose_default_uniform_block;
        }

        Shader_stages::Prototype x_prototype      {x_create_info};
        Shader_stages::Prototype y_prototype      {y_create_info};
        Shader_stages::Prototype compose_prototype{compose_create_info};
        m_downsample_x_shader_stages = std::make_unique<Shader_stages>(std::move(x_prototype));
        m_downsample_y_shader_stages = std::make_unique<Shader_stages>(std::move(y_prototype));
        m_compose_shader_stages      = std::make_unique<Shader_stages>(std::move(compose_prototype));
        const auto shader_monitor = get<erhe::application::Shader_monitor>();
        if (shader_monitor)
        {
            shader_monitor->add(x_create_info,       m_downsample_x_shader_stages.get());
            shader_monitor->add(y_create_info,       m_downsample_y_shader_stages.get());
            shader_monitor->add(compose_create_info, m_compose_shader_stages.get());
        }
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
}

void Post_processing::post_initialize()
{
    m_configuration          = get<erhe::application::Configuration>();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Post_processing::create_frame_resources()
{
    m_parameter_buffer.allocate(
        gl::Buffer_target::uniform_buffer,
        m_parameter_block.binding_point(),
        m_parameter_block.size_bytes() * 100
    );
}

void Post_processing::next_frame()
{
    m_parameter_buffer.next_frame();
}

auto Post_processing::create_node(
    const std::string_view name
) -> std::shared_ptr<Post_processing_node>
{
    auto new_node = std::make_shared<Post_processing_node>(name, *this);
    m_nodes.push_back(new_node);
    return new_node;
}

auto Post_processing::get_nodes() -> const std::vector<std::shared_ptr<Post_processing_node>>&
{
    return m_nodes;
}

/// //////////////////////////////////////////
/// //////////////////////////////////////////

void Post_processing::post_process(Post_processing_node& node)
{
    erhe::graphics::Scoped_debug_group pass_scope{"Post Processing"};
    erhe::graphics::Scoped_gpu_timer   timer     {*m_gpu_timer.get()};

    const auto& downsample_nodes = node.get_downsample_nodes();

    if (downsample_nodes.empty())
    {
        return;
    }

    if (downsample_nodes.size() > 1)
    {
        for (
            std::size_t i = 0,
            end = downsample_nodes.size() - 2;
            i < end;
            ++i
        )
        {
            const Downsample_node&               source_downsample_node = downsample_nodes.at(i);
            const Downsample_node&               downsample_node        = downsample_nodes.at(i + 1);
            const erhe::graphics::Texture* const source_texture         = source_downsample_node.texture.get();
            const int width  = source_texture->width();
            const int height = source_texture->width();

            if (
                (width  == 1) &&
                (height == 1)
            )
            {
                break;
            }

            if (downsample_node.axis == 0)
            {
                erhe::graphics::Scoped_debug_group downsample_scope{"Downsample X"};
                downsample(source_texture, downsample_node, m_downsample_x_pipeline);
            }
            else if (downsample_node.axis == 1)
            {
                erhe::graphics::Scoped_debug_group downsample_scope{"Downsample Y"};
                downsample(source_texture, downsample_node, m_downsample_y_pipeline);
            }
            else
            {
                ERHE_FATAL("bad downsample axis");
            }
        }
    }

    compose(node);
}

static constexpr std::string_view c_downsample{"Post_processing::downsample"};

void Post_processing::downsample(
    const erhe::graphics::Texture*  source_texture,
    const Downsample_node&          downsample_node,
    const erhe::graphics::Pipeline& pipeline
)
{
    ERHE_PROFILE_FUNCTION
    ERHE_PROFILE_GPU_SCOPE(c_downsample)

    auto& parameter_buffer   = m_parameter_buffer.current_buffer();
    auto& parameter_writer   = m_parameter_buffer.writer();
    auto  parameter_gpu_data = parameter_buffer.map();

    parameter_writer.begin(parameter_buffer.target());

    std::byte* const          start      = parameter_gpu_data.data();
    const std::size_t         byte_count = parameter_gpu_data.size_bytes();
    const std::size_t         word_count = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    const uint64_t handle = erhe::graphics::get_handle(
        *source_texture,
        *m_linear_sampler
    );
    const uint32_t texture_handle[2] =
    {
        static_cast<uint32_t>((handle & 0xffffffffu)),
        static_cast<uint32_t>(handle >> 32u)
    };
    const gsl::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};

    using erhe::graphics::write;
    write<float   >(gpu_float_data, parameter_writer.write_offset + m_offsets.texel_scale,   1.0f / source_texture->width());
    write<uint32_t>(gpu_uint_data,  parameter_writer.write_offset + m_offsets.texture_count, 1);
    write<float   >(gpu_float_data, parameter_writer.write_offset + m_offsets.reserved0,     0.0f);
    write<float   >(gpu_float_data, parameter_writer.write_offset + m_offsets.reserved1,     0.0f);
    write<uint32_t>(gpu_uint_data,  parameter_writer.write_offset + m_offsets.source_texture,     texture_handle_cpu_data);
    write<uint32_t>(gpu_uint_data,  parameter_writer.write_offset + m_offsets.source_texture + 2, 0);
    write<uint32_t>(gpu_uint_data,  parameter_writer.write_offset + m_offsets.source_texture + 3, 0);
    parameter_writer.write_offset += m_parameter_block.size_bytes();
    parameter_writer.end();

    downsample_node.bind_framebuffer();

    m_pipeline_state_tracker->execute(pipeline);

    {
        ERHE_PROFILE_SCOPE("bind parameter buffer");

        gl::bind_buffer_range(
            parameter_buffer.target(),
            static_cast<GLuint>    (m_parameter_block.binding_point()),
            static_cast<GLuint>    (parameter_buffer.gl_name()),
            static_cast<GLintptr>  (parameter_writer.range.first_byte_offset),
            static_cast<GLsizeiptr>(parameter_writer.range.byte_count)
        );
    }

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        ERHE_PROFILE_SCOPE("make input texture resident");
        gl::make_texture_handle_resident_arb(handle);
    }
    else
    {
        gl::bind_texture_unit(0, source_texture->gl_name());
        gl::bind_sampler     (0, m_linear_sampler->gl_name());
    }

    {
        static constexpr std::string_view c_draw_arrays{"draw arrays"};

        ERHE_PROFILE_GPU_SCOPE(c_draw_arrays)
        gl::draw_arrays(pipeline.data.input_assembly.primitive_topology, 0, 3);
    }
    {
        ERHE_PROFILE_SCOPE("bind fbo");
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    }

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        ERHE_PROFILE_SCOPE("make input texture non resident");
        gl::make_texture_handle_non_resident_arb(handle);
    }
}

void Post_processing::compose(Post_processing_node& node)
{
    static constexpr std::string_view c_compose{"Post_processing::compose"};

    erhe::graphics::Scoped_debug_group downsample_scope{"Compose"};

    ERHE_PROFILE_FUNCTION
    ERHE_PROFILE_GPU_SCOPE(c_compose)

    auto& parameter_buffer   = m_parameter_buffer.current_buffer();
    auto& parameter_writer   = m_parameter_buffer.writer();
    auto  parameter_gpu_data = parameter_buffer.map();

    parameter_writer.begin(parameter_buffer.target());

    std::byte* const          start      = parameter_gpu_data.data();
    const std::size_t         byte_count = parameter_gpu_data.size_bytes();
    const std::size_t         word_count = byte_count / sizeof(float);
    const gsl::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const gsl::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    using erhe::graphics::write;
    //const uint32_t texture_count = static_cast<uint32_t>(m_rendertargets.size());

    const auto& downsample_nodes = node.get_downsample_nodes();

    write(gpu_float_data, parameter_writer.write_offset + m_offsets.texel_scale,   0.0f);
    write(gpu_uint_data,  parameter_writer.write_offset + m_offsets.texture_count, static_cast<uint32_t>(downsample_nodes.size()));
    write(gpu_float_data, parameter_writer.write_offset + m_offsets.reserved0,     0.0f);
    write(gpu_float_data, parameter_writer.write_offset + m_offsets.reserved1,     0.0f);

    constexpr std::size_t uvec4_size = 4 * sizeof(uint32_t);

    {
        ERHE_PROFILE_SCOPE("make textures resident");
        std::size_t texture_slot = 0;
        for (const Downsample_node& downsample_node : downsample_nodes)
        {
            const erhe::graphics::Texture* const texture = downsample_node.texture.get();
            const uint64_t handle = erhe::graphics::get_handle(
                *texture,
                *m_linear_sampler
            );

            const uint32_t texture_handle[2] =
            {
                static_cast<uint32_t>((handle & 0xffffffffu)),
                static_cast<uint32_t>(handle >> 32u)
            };
            const gsl::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};

            if (erhe::graphics::Instance::info.use_bindless_texture)
            {
                gl::make_texture_handle_resident_arb(handle);
            }
            else
            {
                gl::bind_texture_unit(static_cast<GLuint>(texture_slot), texture->gl_name());
                gl::bind_sampler     (static_cast<GLuint>(texture_slot), m_linear_sampler->gl_name());
            }

            write<uint32_t>(gpu_uint_data, parameter_writer.write_offset + (texture_slot * uvec4_size) + m_offsets.source_texture,     texture_handle_cpu_data);
            write<uint32_t>(gpu_uint_data, parameter_writer.write_offset + (texture_slot * uvec4_size) + m_offsets.source_texture + 2, 0);
            write<uint32_t>(gpu_uint_data, parameter_writer.write_offset + (texture_slot * uvec4_size) + m_offsets.source_texture + 3, 0);
            ++texture_slot;
        }
        if (!erhe::graphics::Instance::info.use_bindless_texture)
        {
            for (; texture_slot < m_source_texture_count; ++texture_slot)
            {
                gl::bind_texture_unit(static_cast<GLuint>(texture_slot), m_dummy_texture->gl_name());
                gl::bind_sampler     (static_cast<GLuint>(texture_slot), m_nearest_sampler->gl_name());
            }
        }
    }
    parameter_writer.write_offset += m_parameter_block.size_bytes();
    parameter_writer.end();

    const auto viewport = node.get_producer_output_viewport(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );

    const auto& output_framebuffer = node.get_producer_output_framebuffer(
        erhe::application::Resource_routing::Resource_provided_by_consumer,
        erhe::application::Rendergraph_node_key::viewport
    );
    const GLuint framebuffer_name = output_framebuffer ? output_framebuffer->gl_name() : 0;

    gl::bind_framebuffer(
        gl::Framebuffer_target::draw_framebuffer,
        framebuffer_name
    );

    // TODO Add destination viewport Rendergraph_node
    gl::viewport(
        viewport.x,
        viewport.y,
        viewport.width,
        viewport.height
    );

    gl::clear_color(1.0f, 1.0f, 0.0f, 0.0f);
    gl::clear      (gl::Clear_buffer_mask::color_buffer_bit);

    const auto& pipeline = m_compose_pipeline;
    m_pipeline_state_tracker->execute(pipeline);

    {
        ERHE_PROFILE_SCOPE("bind parameter buffer");
        gl::bind_buffer_range(
            parameter_buffer.target(),
            static_cast<GLuint>    (m_parameter_block.binding_point()),
            static_cast<GLuint>    (parameter_buffer.gl_name()),
            static_cast<GLintptr>  (parameter_writer.range.first_byte_offset),
            static_cast<GLsizeiptr>(parameter_writer.range.byte_count)
        );
    }

    {
        ERHE_PROFILE_SCOPE("draw arrays");
        gl::draw_arrays(pipeline.data.input_assembly.primitive_topology, 0, 3);
    }

    {
        ERHE_PROFILE_SCOPE("unbind fbo");
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    }

    if (erhe::graphics::Instance::info.use_bindless_texture)
    {
        ERHE_PROFILE_SCOPE("make textures non-resident");

        for (const Downsample_node& downsample_node : downsample_nodes)
        {
            const erhe::graphics::Texture* const texture = downsample_node.texture.get();
            const uint64_t handle = erhe::graphics::get_handle(
                *texture,
                *m_linear_sampler
            );

            const uint32_t texture_handle[2] =
            {
                static_cast<uint32_t>((handle & 0xffffffffu)),
                static_cast<uint32_t>(handle >> 32u)
            };
            const gsl::span<const uint32_t> texture_handle_cpu_data{&texture_handle[0], 2};

            if (erhe::graphics::Instance::info.use_bindless_texture)
            {
                gl::make_texture_handle_non_resident_arb(handle);
            }
        }
    }
}

} // namespace editor





#if 0
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
#endif
