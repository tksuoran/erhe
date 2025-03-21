#include "rendergraph/post_processing.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

namespace editor {

Post_processing::Offsets::Offsets(erhe::graphics::Shader_resource& block)
    : downsample_texture   {block.add_uvec2("downsample_texture"   )->offset_in_parent()}
    , upsample_texture     {block.add_uvec2("upsample_texture"     )->offset_in_parent()}

    , texel_scale          {block.add_vec2 ("texel_scale"          )->offset_in_parent()}
    , source_lod           {block.add_float("source_lod"           )->offset_in_parent()}
    , level_count          {block.add_float("level_count"          )->offset_in_parent()}

    , upsample_radius      {block.add_float("upsample_radius"      )->offset_in_parent()}
    , mix_weight           {block.add_float("mix_weight"           )->offset_in_parent()}
    , tonemap_luminance_max{block.add_float("tonemap_luminance_max")->offset_in_parent()}
    , tonemap_alpha        {block.add_float("tonemap_alpha"        )->offset_in_parent()}
{
}

// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/

Post_processing_node::Post_processing_node(
    erhe::graphics::Instance&       graphics_instance,
    erhe::rendergraph::Rendergraph& rendergraph,
    Post_processing&                post_processing,
    const std::string_view          name
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, name}
    , graphics_instance{graphics_instance}
    , post_processing{post_processing}
    , parameter_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::uniform_buffer,
            .capacity_byte_count = graphics_instance.align_buffer_offset(gl::Buffer_target::uniform_buffer, post_processing.get_parameter_block().size_bytes()) * 20, // max 20 levels
            .storage_mask        = gl::Buffer_storage_mask::map_write_bit,
            .access_mask         = gl::Map_buffer_access_mask::map_write_bit,
            .debug_label         = "post processing"
        }
    }
{
    register_input(erhe::rendergraph::Routing::Resource_provided_by_consumer, "viewport", erhe::rendergraph::Rendergraph_node_key::viewport);
    register_output(erhe::rendergraph::Routing::Resource_provided_by_consumer, "viewport", erhe::rendergraph::Rendergraph_node_key::viewport);
}

auto Post_processing_node::update_size() -> bool
{
    constexpr bool downsample_nodes_unchanged = true;
    constexpr bool downsample_nodes_changed   = false;

    // Output determines the size of intermediate nodes and size of the input node for the post processing render graph node.
    // Output *should* be multisample resolved
    const auto viewport = get_producer_output_viewport(erhe::rendergraph::Routing::Resource_provided_by_consumer, erhe::rendergraph::Rendergraph_node_key::viewport);

    if ((level0_width == viewport.width) && (level0_height == viewport.height)) {
        return downsample_nodes_unchanged;
    }

    downsample_texture.reset();
    upsample_texture.reset();
    downsample_framebuffers.clear();
    upsample_framebuffers.clear();
    level0_width  = viewport.width;
    level0_height = viewport.height;
    if (level0_width < 1 || level0_height < 1) {
        return downsample_nodes_changed;
    }

    // Create textures
    downsample_texture = std::make_shared<erhe::graphics::Texture>(
        erhe::graphics::Texture::Create_info{
            .instance        = graphics_instance,
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba16f, // TODO other formats
            .use_mipmaps     = true,
            .sample_count    = 0,
            .width           = level0_width,
            .height          = level0_height,
            .debug_label     = fmt::format("{} downsample", get_name())
        }
    );
    upsample_texture = std::make_shared<erhe::graphics::Texture>(
        erhe::graphics::Texture::Create_info{
            .instance        = graphics_instance,
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba16f, // TODO other formats
            .use_mipmaps     = true,
            .sample_count    = 0,
            .width           = level0_width,
            .height          = level0_height,
            .debug_label     = fmt::format("{} upsample", get_name())
        }
    );

    // Create framebuffers
    int level_width  = level0_width;
    int level_height = level0_height;
    int level = 0;
    level_widths.clear();
    level_heights.clear();
    downsample_source_levels.clear();
    upsample_source_levels.clear();
    downsample_texture_views.clear();
    upsample_texture_views.clear();
    weights.clear();
    while ((level_width >= 1) && (level_height >= 1)) {
        level_widths.push_back(level_width);
        level_heights.push_back(level_height);
        {
            erhe::graphics::Framebuffer::Create_info create_info{};
            create_info.attach(gl::Framebuffer_attachment::color_attachment0, downsample_texture.get(), level, 0);
            std::shared_ptr<erhe::graphics::Framebuffer> framebuffer = std::make_shared<erhe::graphics::Framebuffer>(create_info);
            framebuffer->set_debug_label(fmt::format("{} downsample level {}", get_name(), level));
            downsample_framebuffers.push_back(framebuffer);

            erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(graphics_instance, downsample_texture);
            texture_create_info.view_min_level = level;
            texture_create_info.level_count    = 1;
            texture_create_info.view_min_layer = 0;
            texture_create_info.width          = level_width;
            texture_create_info.height         = level_height;
            texture_create_info.debug_label    = fmt::format("Downsample level {}", level);
            downsample_texture_views.push_back(std::make_shared<erhe::graphics::Texture>(texture_create_info));
        }
        {
            erhe::graphics::Framebuffer::Create_info create_info{};
            create_info.attach(gl::Framebuffer_attachment::color_attachment0, upsample_texture.get(), level, 0);
            std::shared_ptr<erhe::graphics::Framebuffer> framebuffer = std::make_shared<erhe::graphics::Framebuffer>(create_info);
            framebuffer->set_debug_label(fmt::format("{} upsample level {}", get_name(), level));
            upsample_framebuffers.push_back(framebuffer);

            erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(graphics_instance, upsample_texture);
            texture_create_info.view_min_level = level;
            texture_create_info.level_count    = 1;
            texture_create_info.view_min_layer = 0;
            texture_create_info.width          = level_width;
            texture_create_info.height         = level_height;
            texture_create_info.debug_label    = fmt::format("Upsample level {}", level);
            upsample_texture_views.push_back(std::make_shared<erhe::graphics::Texture>(texture_create_info));
        }
        if ((level_width == 1) && (level_height == 1)) {
            break;
        }
        level_width  = std::max(1, level_width  / 2);
        level_height = std::max(1, level_height / 2);
        ++level;
    }
    size_t level_count = level_widths.size();
    for (size_t i = 0; i < level_count - 1; ++i) {
        downsample_source_levels.push_back(i);
    }
    for (size_t i = level_count - 1; i > 0; --i) {
        upsample_source_levels.push_back(i);
    }
    weights.resize(level_count);
    {
        float c0 =  0.12f; //1.0 / 128.0f;
        float c1 =  0.05f; //1.0 / 256.0f;
        float c2 = -1.0f;  // -2.00f;
        float c3 =  2.0f;  //  3.00f;
        weights[0] = c0;
        weights[1] = c0;
        float max_value = 0.0f;
        for (size_t i = 2, end = weights.size(); i < end; ++i) {
            float l = static_cast<float>(1 + level_count - i);
            float w = c3 * std::pow(l, c2);
            weights[i] = 1.0f - w;
            max_value = std::max(max_value, weights[i]);
        }
        float margin = max_value - 1.0f;
        for (size_t i = 2, end = weights.size(); i < end; ++i) {
            weights[i] = weights[i] - margin - c1;
        }
    }

    update_parameters();
    return downsample_nodes_changed;
}

void Post_processing_node::update_parameters()
{
    // Prepare parameter buffer
    const erhe::graphics::Sampler&  sampler    = post_processing.get_sampler();
    const std::size_t               entry_size = post_processing.get_parameter_block().size_bytes();
    const Post_processing::Offsets& offsets    = post_processing.get_offsets();

    const std::size_t level_offset_size = graphics_instance.align_buffer_offset(parameter_buffer.target(), entry_size);

    const uint64_t downsample_handle = graphics_instance.get_handle(*downsample_texture, sampler);
    const uint64_t upsample_handle   = graphics_instance.get_handle(*upsample_texture,   sampler);
    const uint32_t downsample_texture_handle[2] = {
        static_cast<uint32_t>((downsample_handle & 0xffffffffu)),
        static_cast<uint32_t>(downsample_handle >> 32u)
    };
    const uint32_t upsample_texture_handle[2] = {
        static_cast<uint32_t>((upsample_handle & 0xffffffffu)),
        static_cast<uint32_t>(upsample_handle >> 32u)
    };
    const std::span<const uint32_t> downsample_texture_handle_cpu_data{&downsample_texture_handle[0], 2};
    const std::span<const uint32_t> upsample_texture_handle_cpu_data  {&upsample_texture_handle[0], 2};

    size_t                       level_count        = level_widths.size();
    std::size_t                  write_offset       = 0;
    std::span<std::byte>         parameter_gpu_data = parameter_buffer.map_all_bytes(gl::Map_buffer_access_mask::map_invalidate_buffer_bit | gl::Map_buffer_access_mask::map_write_bit);
    std::byte* const             start              = parameter_gpu_data.data();
    const std::size_t            byte_count         = parameter_gpu_data.size_bytes();
    const std::size_t            word_count         = byte_count / sizeof(float);
    const std::span<float>       gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t>    gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    for (size_t source_level = 0, end = level_count; source_level < end; ++source_level) {
        using erhe::graphics::write;
        float texel_scale[2] = {
            1.0f / static_cast<float>(level_widths.at(source_level)),
            1.0f / static_cast<float>(level_heights.at(source_level))
        };
        write<uint32_t>(gpu_uint_data,  write_offset + offsets.downsample_texture,    downsample_texture_handle_cpu_data);
        write<uint32_t>(gpu_uint_data,  write_offset + offsets.upsample_texture,      upsample_texture_handle_cpu_data);
        write<float   >(gpu_float_data, write_offset + offsets.texel_scale,           std::span<float>{&texel_scale[0], 2});
        write<float   >(gpu_float_data, write_offset + offsets.source_lod,            static_cast<float>(source_level));
        write<float   >(gpu_float_data, write_offset + offsets.level_count,           static_cast<float>(level_count));
        write<float   >(gpu_float_data, write_offset + offsets.upsample_radius,       upsample_radius);
        write<float   >(gpu_float_data, write_offset + offsets.mix_weight,            weights.at(source_level));
        write<float   >(gpu_float_data, write_offset + offsets.tonemap_luminance_max, tonemap_luminance_max);
        write<float   >(gpu_float_data, write_offset + offsets.tonemap_alpha,         tonemap_alpha);
        write_offset += level_offset_size;
    }
    parameter_buffer.flush_and_unmap_bytes(write_offset);
}

void Post_processing_node::viewport_toolbar()
{
    // TODO Fix ImGui::Checkbox("Post Processing", &m_enabled);
}

auto Post_processing_node::get_consumer_input_texture(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return downsample_texture; // TODO
}

// Overridden to provide framebuffer from the first downsample node
auto Post_processing_node::get_consumer_input_framebuffer(erhe::rendergraph::Routing, int, int) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    return downsample_framebuffers.empty() ? std::shared_ptr<erhe::graphics::Framebuffer>{} : downsample_framebuffers.front();
}

auto Post_processing_node::get_consumer_input_viewport(const erhe::rendergraph::Routing resource_routing, const int key, const int depth) const -> erhe::math::Viewport
{
    return get_producer_output_viewport(resource_routing, key, depth + 1);
}

void Post_processing_node::execute_rendergraph_node()
{
    if (!m_enabled) {
        return;
    }

    static constexpr std::string_view c_post_processing{"Post_processing"};

    ERHE_PROFILE_FUNCTION();
    //ERHE_PROFILE_GPU_SCOPE(c_post_processing)

    const bool downsample_nodes_unchanged = update_size();
    if (!downsample_nodes_unchanged) {
        return;
    }

    if (!downsample_texture || !upsample_texture) {
        return;
    }

    post_processing.post_process(*this);
}

/// //////////////////////////////////////////
/// //////////////////////////////////////////

auto Post_processing::make_program(
    erhe::graphics::Instance&    graphics_instance,
    const char*                  name,
    const std::filesystem::path& fs_path
) -> erhe::graphics::Shader_stages_create_info
{
    ERHE_PROFILE_FUNCTION();

    const std::vector<std::pair<std::string, std::string>> bindless_defines{{"ERHE_BINDLESS_TEXTURE", "1"}};
    const std::vector<std::pair<std::string, std::string>> empty_defines{};

    const std::vector<erhe::graphics::Shader_stage_extension> bindless_extensions{
        {gl::Shader_type::fragment_shader, "GL_ARB_bindless_texture"}
    };
    const std::vector<erhe::graphics::Shader_stage_extension> empty_extensions{};
    const bool bindless_textures = graphics_instance.info.use_bindless_texture;

    return
        erhe::graphics::Shader_stages_create_info{
            .name                  = name,
            .defines               = bindless_textures ? bindless_defines    : empty_defines,
            .extensions            = bindless_textures ? bindless_extensions : empty_extensions,
            .interface_blocks      = { &m_parameter_block },
            .fragment_outputs      = &m_fragment_outputs,
            .default_uniform_block = bindless_textures ? nullptr : &m_default_uniform_block,
            .shaders = {
                { gl::Shader_type::vertex_shader,   m_shader_path / std::filesystem::path("post_processing.vert")},
                { gl::Shader_type::fragment_shader, m_shader_path / fs_path }
            },
            .build                 = true
        };
}

Post_processing::Post_processing(erhe::graphics::Instance& graphics_instance, Editor_context& editor_context)
    : m_context           {editor_context}
    , m_fragment_outputs  {erhe::graphics::Fragment_output{.name = "out_color", .type = gl::Fragment_shader_output_type::float_vec4, .location = 0}}
    , m_dummy_texture     {graphics_instance.create_dummy_texture()}
    , m_linear_mipmap_nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::linear,
            .debug_label = "linear_mipmap_nearest"
        }
    }
    , m_parameter_block   {graphics_instance, "post_processing", 0, erhe::graphics::Shader_resource::Type::uniform_block}
    , m_offsets           {m_parameter_block}
    , m_empty_vertex_input{}

    , m_default_uniform_block{graphics_instance}
    , m_downsample_texture_resource{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_downsample", gl::Uniform_type::sampler_2d, 0)
    }
    , m_upsample_texture_resource{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_upsample", gl::Uniform_type::sampler_2d, 1)
    }
    , m_shader_path{std::filesystem::path("res") / std::filesystem::path("shaders")}
    , m_shader_stages{
        .downsample_with_lowpass{graphics_instance, make_program(graphics_instance, "downsample_lowpass", std::filesystem::path("downsample_lowpass.frag"))},
        .downsample             {graphics_instance, make_program(graphics_instance, "downsample",         std::filesystem::path("downsample.frag"))},
        .upsample               {graphics_instance, make_program(graphics_instance, "upsample",           std::filesystem::path("upsample.frag"))}
    }
    , m_pipelines{
        .downsample_with_lowpass = erhe::graphics::Pipeline{
            erhe::graphics::Pipeline_data{
                .name           = "Downsample with Lowpass",
                .shader_stages  = &m_shader_stages.downsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .downsample = erhe::graphics::Pipeline{
            erhe::graphics::Pipeline_data{
                .name           = "Post Processing Downsample",
                .shader_stages  = &m_shader_stages.downsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample = erhe::graphics::Pipeline{
            erhe::graphics::Pipeline_data{
                .name           = "Post Processing Upsample",
                .shader_stages  = &m_shader_stages.upsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_fan,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        }
    }
    , m_gpu_timer{"Post_processing"}
{
    graphics_instance.shader_monitor.add(m_shader_stages.downsample_with_lowpass);
    graphics_instance.shader_monitor.add(m_shader_stages.downsample             );
    graphics_instance.shader_monitor.add(m_shader_stages.upsample               );
}

auto Post_processing::create_node(
    erhe::graphics::Instance&       graphics_instance,
    erhe::rendergraph::Rendergraph& rendergraph,
    const std::string_view          name
) -> std::shared_ptr<Post_processing_node>
{
    std::shared_ptr<Post_processing_node> new_node = std::make_shared<Post_processing_node>(graphics_instance, rendergraph, *this, name);
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
    const std::size_t level_offset_size = m_context.graphics_instance->align_buffer_offset(
        node.parameter_buffer.target(),
        m_parameter_block.size_bytes()
    );

    const uint64_t downsample_handle = m_context.graphics_instance->get_handle(*node.downsample_texture, m_linear_mipmap_nearest_sampler);
    const uint64_t upsample_handle   = m_context.graphics_instance->get_handle(*node.upsample_texture,   m_linear_mipmap_nearest_sampler);
    if (m_context.graphics_instance->info.use_bindless_texture) {
        ERHE_PROFILE_SCOPE("make input texture resident");
        gl::make_texture_handle_resident_arb(downsample_handle);
        gl::make_texture_handle_resident_arb(upsample_handle);
    } else {
        gl::bind_texture_unit(0, node.downsample_texture->gl_name());
        gl::bind_sampler     (0, m_linear_mipmap_nearest_sampler.gl_name());
        gl::bind_texture_unit(1, node.upsample_texture->gl_name());
        gl::bind_sampler     (1, m_linear_mipmap_nearest_sampler.gl_name());
    }

    // Downsample passes
    for (const size_t source_level : node.downsample_source_levels) {
        const size_t destination_level = source_level + 1;
        const auto& framebuffer = node.downsample_framebuffers.at(destination_level);
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
        gl::viewport(0, 0, node.level_widths.at(destination_level), node.level_heights.at(destination_level));
        gl::bind_buffer_range(
            node.parameter_buffer.target(),
            static_cast<GLuint>    (m_parameter_block.binding_point()),
            static_cast<GLuint>    (node.parameter_buffer.gl_name()),
            static_cast<GLintptr>  (source_level * level_offset_size),
            static_cast<GLsizeiptr>(m_parameter_block.size_bytes())
        );
        if (source_level == node.lowpass_count) {
            m_context.graphics_instance->opengl_state_tracker.execute(m_pipelines.downsample);
        } else if (source_level == 0) {
            m_context.graphics_instance->opengl_state_tracker.execute(m_pipelines.downsample_with_lowpass);
        }
        gl::draw_arrays(gl::Primitive_type::triangles, 0, 3);
    }

    const auto viewport = node.get_producer_output_viewport(
        erhe::rendergraph::Routing::Resource_provided_by_consumer,
        erhe::rendergraph::Rendergraph_node_key::viewport
    );

    const auto& output_framebuffer = node.get_producer_output_framebuffer(
        erhe::rendergraph::Routing::Resource_provided_by_consumer,
        erhe::rendergraph::Rendergraph_node_key::viewport
    );

    // Upsample passes
    m_context.graphics_instance->opengl_state_tracker.execute(m_pipelines.upsample);
    for (const size_t source_level : node.upsample_source_levels) {
        const size_t destination_level = source_level - 1;
        const auto&  framebuffer = destination_level == 0
            ? output_framebuffer
            : node.upsample_framebuffers.at(destination_level);
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
        gl::viewport(0, 0, node.level_widths.at(destination_level), node.level_heights.at(destination_level));
        gl::bind_buffer_range(
            node.parameter_buffer.target(),
            static_cast<GLuint>    (m_parameter_block.binding_point()),
            static_cast<GLuint>    (node.parameter_buffer.gl_name()),
            static_cast<GLintptr>  (source_level * level_offset_size),
            static_cast<GLsizeiptr>(m_parameter_block.size_bytes())
        );
        gl::draw_arrays(gl::Primitive_type::triangles, 0, 3);
    }

    {
        ERHE_PROFILE_SCOPE("bind fbo");
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    }

    if (m_context.graphics_instance->info.use_bindless_texture) {
        ERHE_PROFILE_SCOPE("make input texture non resident");
        gl::make_texture_handle_non_resident_arb(downsample_handle);
        gl::make_texture_handle_non_resident_arb(upsample_handle);
    }
}

} // namespace editor

// DOWNSAMPLING - OUR SOLUTION
//
// - Custom hand-crafted 36-texel downsample (13 bilinear fetches)
// - Created by a weighted average of one-pixel shifts of
// - Intitivel, the overlapping further breaks the grid nature of pyramid approaches
// - works better than comparaeble-sized filters
// - Eliminates the pulsating artifacts anmd temporal stability issues
//
// ┌───┬───┬───┬───┬───┬───┐      Color weight  Instances         Sample weights     
// │   │   │   │   │   │   │  1 : 0.125 = 1/8   1, a, b, M = 4    1,2,3,4 =  1/32    * 4 =  4/32
// ├───1───┼───a───┼───2───┤  2 : 0.125 = 1/8   a, b, M, c = 4    a,b,c,d =  2/32    * 4 =  8/32
// │   │   │   │   │   │   │  3 : 0.125 = 1/8   b, M, 3, d = 4    5       =  4/32    * 4 = 16/32
// ├───┼───5───┼───5───┼───┤  4 : 0.125 = 1/8   M, c, d, 4 = 4    M       =  4/32    * 1 =  4/32
// │   │   │   │   │   │   │  5 : 0.5   = 4/8   5, 5, 5, 5 = 4                       -----------
// ├───b───┼───M───┼───c───┤                                                         SUM = 32/32
// │   │   │   │   │   │   │  a : 1+2
// ├───┼───5───┼───5───┼───┤  b : 1+3
// │   │   │   │   │   │   │  c : 2+4
// ├───3───┼───d───┼───4───┤  d : 3+4
// │   │   │   │   │   │   │  M : 1+2+3+4
// └───┴───┴───┴───┴───┴───┘

// Sample locations:              Sampled by 012,567,10,11,12    Sampled by 3489                Sample weights              
//                                                                                                                          
//    −2  −1   0   1   2             −2  −1   0   1   2             −2  −1   0   1   2             −2  −1   0   1   2       
// ┌───┬───┬───┬───┬───┬───┐      ┏━━━┯━━━┳━━━┯━━━┳━━━┯━━━┓      ┌───┬───┬───┬───┬───┬───┐      ┌───┬───┬───┬───┬───┬───┐   
// │   │   │   │   │   │   │      ┃   ┆   ┃   ┆   ┃   ┆   ┃      │   │   │   │   │   │   │      │   │   │   │   │   │   │   
// ├───0───┼───1───┼───2───┤ −2   ┠┄┄┄0┄┄┄╂┄┄┄1┄┄┄╂┄┄┄2┄┄┄┨ −2   ├───0━━━┿━━━1━━━┿━━━2───┤ −2   ├───1───┼───2───┼───1───┤ −2
// │   │   │   │   │   │   │      ┃   ┆   ┃   ┆   ┃   ┆   ┃      │   ┃   │   ┃   │   ┃   │      │   │   │   │   │   │   │   
// ├───┼───3───┼───4───┼───┤ −1   ┣━━━┿━━━3━━━┿━━━4━━━┿━━━┫ −1   ├───╂───3───╂───4───╂───┤ −1   ├───┼───4───┼───4───┼───┤ −1
// │   │   │   │   │   │   │      ┃   ┆   ┃   ┆   ┃   ┆   ┃      │   ┃   │   ┃   │   ┃   │      │   │   │   │   │   │   │   
// ├───5───┼───6───┼───7───┤  0   ┠┄┄┄5┄┄┄╂┄┄┄6┄┄┄╂┄┄┄7┄┄┄┨  0   ├───5━━━┿━━━6━━━┿━━━7───┤  0   ├───2───┼───4───┼───2───┤  0
// │   │   │   │   │   │   │      ┃   ┆   ┃   ┆   ┃   ┆   ┃      │   ┃   │   ┃   │   ┃   │      │   │   │   │   │   │   │   
// ├───┼───8───┼───9───┼───┤  1   ┣━━━┿━━━8━━━┿━━━9━━━┿━━━┫  1   ├───╂───8───╂───9───╂───┤  1   ├───┼───4───┼───4───┼───┤  1
// │   │   │   │   │   │   │      ┃   ┆   ┃   ┆   ┃   ┆   ┃      │   ┃   │   ┃   │   ┃   │      │   │   │   │   │   │   │   
// ├──10───┼──11───┼──12───┤  2   ┠┄┄10┄┄┄╂┄┄11┄┄┄╂┄┄12┄┄┄┨  2   ├──10━━━┿━━11━━━┿━━12───┤  2   ├───1───┼───2───┼───1───┤  2
// │   │   │   │   │   │   │      ┃   ┆   ┃   ┆   ┃   ┆   ┃      │   │   │   │   │   │   │      │   │   │   │   │   │   │   
// └───┴───┴───┴───┴───┴───┘      ┗━━━┷━━━┻━━━┷━━━┻━━━┷━━━┛      └───┴───┴───┴───┴───┴───┘      └───┴───┴───┴───┴───┴───┘   

// UPSAMPLING - OUR SOLUTION
//
// - Progressive upsample (and sum the chain as you upscale) as found in [Mittring2012]
// - We filter during upsample (on the flight, as in the downsample case):
//      - l4' = l4
//      - l3' = l3 + blur(l4')
//      - l2' = l2 + blur(l3')
//      - l1' = l1 + blur(l2')
//      - l0' = l0 + blur(l1')   1  [ 1 2 1 ]
// - We use a 3x3 tent filter  ---- [ 2 4 2 ]
//                              16  [ 1 2 1 ]
// - Scaled by a radius parameter
//      - The filter do not map to pixels, has "holes" in it
// - Repeated convolutions converge to a Gaussian [Kraus2007]
// - Simple and fast
//
// ┌───┬───┬───┐  Sample weights
// │ 1 │ 2 │ 3 │  1,3,7,9  =  1/16 * 4  =  4/16
// ├───┼───┼───┤  2,4,6,8  =  2/16 * 4  =  8/16
// │ 4 │ 5 │ 6 │  5        =  4/16 * 4  =  4/16
// ├───┼───┼───┤
// │ 7 │ 8 │ 9 │
// └───┴───┴───┘

//  u3 = d3 + blur(d4)
//  u2 = d2 + blur(u3)
//  u1 = d1 + blur(u2)
//  u0 = d0 + blur(u1)

//  u0 = d0 + blur(d1 + blur(d2 + blur(d3 + blur(d4))))
