#include "rendergraph/post_processing.hpp"

#include "app_context.hpp"

#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/align.hpp"

#include <fmt/format.h>

namespace editor {

Post_processing::Offsets::Offsets(erhe::graphics::Shader_resource& block)
    : input_texture        {block.add_uvec2("input_texture"        )->get_offset_in_parent()}
    , downsample_texture   {block.add_uvec2("downsample_texture"   )->get_offset_in_parent()}

    , upsample_texture     {block.add_uvec2("upsample_texture"     )->get_offset_in_parent()}
    , texel_scale          {block.add_vec2 ("texel_scale"          )->get_offset_in_parent()}

    , source_lod           {block.add_float("source_lod"           )->get_offset_in_parent()}
    , level_count          {block.add_float("level_count"          )->get_offset_in_parent()}
    , upsample_radius      {block.add_float("upsample_radius"      )->get_offset_in_parent()}
    , mix_weight           {block.add_float("mix_weight"           )->get_offset_in_parent()}

    , tonemap_luminance_max{block.add_float("tonemap_luminance_max")->get_offset_in_parent()}
    , tonemap_alpha        {block.add_float("tonemap_alpha"        )->get_offset_in_parent()}
{
}

// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/

Post_processing_node::Post_processing_node(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    Post_processing&                post_processing,
    const std::string_view          name
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, name}
    , parameter_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count =
                erhe::utility::align_offset_power_of_two(
                    post_processing.get_parameter_block().get_size_bytes(),
                    graphics_device.get_buffer_alignment(post_processing.get_parameter_block().get_binding_target())
                ) * Post_processing::s_max_mipmap_levels,
            .usage               = get_buffer_usage(post_processing.get_parameter_block().get_binding_target()),
            .direction           = erhe::graphics::Buffer_direction::gpu_only,
            .cache_mode          = erhe::graphics::Buffer_cache_mode::default_,
            .mapping             = erhe::graphics::Buffer_mapping::not_mappable,
            .coherency           = erhe::graphics::Buffer_coherency::off,
            .debug_label         = "post processing"
        }
    }
    , m_graphics_device{graphics_device}
    , m_post_processing{post_processing}
{
    register_input ("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    register_output("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

Post_processing_node::~Post_processing_node()
{
}

auto Post_processing_node::update_size() -> bool
{
    constexpr bool downsample_nodes_unchanged = true;
    constexpr bool downsample_nodes_changed   = false;

    // Input determines the size of intermediate nodes and size of the input node for the post processing render graph node.
    // Input should be multisample resolved.

    const std::shared_ptr<erhe::graphics::Texture> input_texture = get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    if (!input_texture) {
        return downsample_nodes_unchanged; // not yet connected
    }
    const int width  = input_texture->get_width();
    const int height = input_texture->get_height();
    if ((level0_width == width) && (level0_height == height)) {
        return downsample_nodes_unchanged;
    }

    downsample_texture.reset();
    upsample_texture.reset();
    downsample_render_passes.clear();
    upsample_render_passes.clear();
    level0_width  = width;
    level0_height = height;
    if (level0_width < 1 || level0_height < 1) {
        return downsample_nodes_changed;
    }

    // Create textures
    downsample_texture = std::make_shared<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture::Create_info{
            .device       = m_graphics_device,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = erhe::dataformat::Format::format_16_vec4_float, // TODO other formats
            .use_mipmaps  = true,
            .sample_count = 0,
            .width        = level0_width,
            .height       = level0_height,
            .debug_label  = fmt::format("{} downsample", get_name())
        }
    );
    upsample_texture = std::make_shared<erhe::graphics::Texture>(
        m_graphics_device,
        erhe::graphics::Texture::Create_info{
            .device       = m_graphics_device,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = erhe::dataformat::Format::format_16_vec4_float, // TODO other formats
            .use_mipmaps  = true,
            .sample_count = 0,
            .width        = level0_width,
            .height       = level0_height,
            .debug_label  = fmt::format("{} upsample", get_name())
        }
    );

    // Create framebuffers
    int level_width  = level0_width;
    int level_height = level0_height;
    int level        = 0;
    level_widths            .clear();
    level_heights           .clear();
    downsample_source_levels.clear();
    upsample_source_levels  .clear();
    downsample_texture_views.clear();
    upsample_texture_views  .clear();
    weights                 .clear();
    while ((level_width >= 1) && (level_height >= 1)) {
        level_widths .push_back(level_width);
        level_heights.push_back(level_height);
        {
            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            render_pass_descriptor.color_attachments[0].texture       = downsample_texture.get();
            render_pass_descriptor.color_attachments[0].texture_level = level;
            render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Dont_care;
            render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            render_pass_descriptor.render_target_width                = downsample_texture->get_width(level);
            render_pass_descriptor.render_target_height               = downsample_texture->get_height(level);
            render_pass_descriptor.debug_label                        = fmt::format("{} downsample level {}", get_name(), level);

            ERHE_VERIFY(render_pass_descriptor.render_target_width  == level_width);
            ERHE_VERIFY(render_pass_descriptor.render_target_height == level_height);

            std::unique_ptr<erhe::graphics::Render_pass> render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
            downsample_render_passes.push_back(std::move(render_pass));

            erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(m_graphics_device, downsample_texture);
            texture_create_info.view_base_level       = level;
            texture_create_info.level_count           = 1;
            texture_create_info.view_base_array_layer = 0;
            texture_create_info.width                 = level_width;
            texture_create_info.height                = level_height;
            texture_create_info.debug_label           = fmt::format("Downsample level {}", level);
            downsample_texture_views.push_back(std::make_shared<erhe::graphics::Texture>(m_graphics_device, texture_create_info));
        }
        {
            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            render_pass_descriptor.color_attachments[0].texture       = upsample_texture.get();
            render_pass_descriptor.color_attachments[0].texture_level = level;
            render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Dont_care;
            render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            render_pass_descriptor.render_target_width                = downsample_texture->get_width(level);
            render_pass_descriptor.render_target_height               = downsample_texture->get_height(level);
            render_pass_descriptor.debug_label                        = fmt::format("{} upsample level {}", get_name(), level);

            ERHE_VERIFY(render_pass_descriptor.render_target_width == level_width);
            ERHE_VERIFY(render_pass_descriptor.render_target_height == level_height);

            {
                std::unique_ptr<erhe::graphics::Render_pass> render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
                upsample_render_passes.push_back(std::move(render_pass));
            }

            erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(m_graphics_device, upsample_texture);
            texture_create_info.view_base_level       = level;
            texture_create_info.level_count           = 1;
            texture_create_info.view_base_array_layer = 0;
            texture_create_info.width                 = level_width;
            texture_create_info.height                = level_height;
            texture_create_info.debug_label           = fmt::format("Upsample level {}", level);
            upsample_texture_views.push_back(std::make_shared<erhe::graphics::Texture>(m_graphics_device, texture_create_info));
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
    const erhe::graphics::Sampler&  sampler_linear                = m_post_processing.get_sampler_linear();
    const erhe::graphics::Sampler&  sampler_linear_mipmap_nearest = m_post_processing.get_sampler_linear_mipmap_nearest();
    const std::size_t               entry_size                    = m_post_processing.get_parameter_block().get_size_bytes();
    const Post_processing::Offsets& offsets                       = m_post_processing.get_offsets();

    const std::size_t level_offset_size = erhe::utility::align_offset_power_of_two(
        entry_size,
        m_graphics_device.get_buffer_alignment(m_post_processing.get_parameter_block().get_binding_target())
    );

    const std::shared_ptr<erhe::graphics::Texture> input_texture = get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    ERHE_VERIFY(input_texture);

    const uint64_t input_handle      = m_graphics_device.get_handle(*input_texture.get(), sampler_linear);
    const uint64_t downsample_handle = m_graphics_device.get_handle(*downsample_texture,  sampler_linear_mipmap_nearest);
    const uint64_t upsample_handle   = m_graphics_device.get_handle(*upsample_texture,    sampler_linear_mipmap_nearest);
    const uint32_t input_texture_handle[2] = {
        static_cast<uint32_t>((input_handle & 0xffffffffu)),
        static_cast<uint32_t>(input_handle >> 32u)
    };
    const uint32_t downsample_texture_handle[2] = {
        static_cast<uint32_t>((downsample_handle & 0xffffffffu)),
        static_cast<uint32_t>(downsample_handle >> 32u)
    };
    const uint32_t upsample_texture_handle[2] = {
        static_cast<uint32_t>((upsample_handle & 0xffffffffu)),
        static_cast<uint32_t>(upsample_handle >> 32u)
    };
    const std::span<const uint32_t> input_texture_handle_cpu_data     {&input_texture_handle     [0], 2};
    const std::span<const uint32_t> downsample_texture_handle_cpu_data{&downsample_texture_handle[0], 2};
    const std::span<const uint32_t> upsample_texture_handle_cpu_data  {&upsample_texture_handle  [0], 2};

    // NOTE: parameter_buffer is only rarely updated.
    //       It can happen when texture size changed.
    // TODO Should we consider what GPU might be currently doing?
    //      Should we use GPU ring buffer instead, to handle that?
    size_t                    level_count  = level_widths.size();
    std::size_t               write_offset = 0;
    const std::size_t         byte_count   = level_offset_size * Post_processing::s_max_mipmap_levels;
    std::vector<std::byte>    parameter_data(byte_count);
    std::byte* const          start        = parameter_data.data();
    const std::size_t         word_count   = byte_count / sizeof(float);
    const std::span<float>    float_data{reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t> uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    for (size_t source_level = 0, end = level_count; source_level < end; ++source_level) {
        using erhe::graphics::write;
        float texel_scale[2] = {
            1.0f / static_cast<float>(level_widths.at(source_level)),
            1.0f / static_cast<float>(level_heights.at(source_level))
        };
        write<uint32_t>(uint_data,  write_offset + offsets.input_texture,         input_texture_handle_cpu_data);
        write<uint32_t>(uint_data,  write_offset + offsets.downsample_texture,    downsample_texture_handle_cpu_data);
        write<uint32_t>(uint_data,  write_offset + offsets.upsample_texture,      upsample_texture_handle_cpu_data);
        write<float   >(float_data, write_offset + offsets.texel_scale,           std::span<float>{&texel_scale[0], 2});
        write<float   >(float_data, write_offset + offsets.source_lod,            static_cast<float>(source_level));
        write<float   >(float_data, write_offset + offsets.level_count,           static_cast<float>(level_count));
        write<float   >(float_data, write_offset + offsets.upsample_radius,       upsample_radius);
        write<float   >(float_data, write_offset + offsets.mix_weight,            weights.at(source_level));
        write<float   >(float_data, write_offset + offsets.tonemap_luminance_max, tonemap_luminance_max);
        write<float   >(float_data, write_offset + offsets.tonemap_alpha,         tonemap_alpha);
        write_offset += level_offset_size;
    }
    m_graphics_device.upload_to_buffer(parameter_buffer, 0, start, byte_count);
}

void Post_processing_node::viewport_toolbar()
{
    // TODO Fix ImGui::Checkbox("Post Processing", &m_enabled);
}

auto Post_processing_node::get_producer_output_texture(const int key, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    if (
        (key == erhe::rendergraph::Rendergraph_node_key::viewport_texture) ||
        (key == erhe::rendergraph::Rendergraph_node_key::wildcard)
    ) {
        if (upsample_texture_views.empty()) {
            return {};
        } else {
            return upsample_texture_views.at(0);
        }
    }

    return {};
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

    // Input texture may have changed
    update_parameters();

    m_post_processing.post_process(*this);
}

/// //////////////////////////////////////////
/// //////////////////////////////////////////

auto Post_processing::make_program(
    erhe::graphics::Device& graphics_device,
    const char*             name,
    const std::string&      fs_path,
    const unsigned int      flags
) -> erhe::graphics::Shader_stages_create_info
{
    ERHE_PROFILE_FUNCTION();

    std::vector<erhe::graphics::Shader_stage_extension> extensions; 
    std::vector<std::pair<std::string, std::string>>    defines;
    const bool bindless_textures = graphics_device.get_info().use_bindless_texture;
    if (flags & flag_first_pass       ) { defines.push_back({"FIRST_PASS", "1"}); }
    if (flags & flag_last_pass        ) { defines.push_back({"LAST_PASS",  "1"}); }
    if (flags & flag_source_input     ) { defines.push_back({"SOURCE", "s_input"}); }
    if (flags & flag_source_downsample) { defines.push_back({"SOURCE", "s_downsample"}); }
    if (flags & flag_source_upsample  ) { defines.push_back({"SOURCE", "s_upsample"}); }

    return
        erhe::graphics::Shader_stages_create_info{
            .name                  = name,
            .defines               = defines,
            .extensions            = extensions,
            .interface_blocks      = { &m_parameter_block },
            .fragment_outputs      = &m_fragment_outputs,
            .default_uniform_block = bindless_textures ? nullptr : &m_default_uniform_block,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   m_shader_path / std::filesystem::path("post_processing.vert")},
                { erhe::graphics::Shader_type::fragment_shader, m_shader_path / fs_path }
            },
            .build                 = true
        };
}

Post_processing::Post_processing(erhe::graphics::Device& d, App_context& app_context)
    : m_context         {app_context}
    , m_fragment_outputs{erhe::graphics::Fragment_output{.name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0}}
    , m_dummy_texture   {d.create_dummy_texture()}
    , m_sampler_linear{
        d,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "linear"
        }
    }
    , m_sampler_linear_mipmap_nearest{
        d,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "linear_mipmap_nearest"
        }
    }
    , m_parameter_block   {d, "post_processing", 0, erhe::graphics::Shader_resource::Type::uniform_block}
    , m_offsets           {m_parameter_block}
    , m_empty_vertex_input{d}

    , m_default_uniform_block{d}
    , m_input_texture_resource{
        d.get_info().use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_input", erhe::graphics::Glsl_type::sampler_2d, s_input_texture)
    }
    , m_downsample_texture_resource{
        d.get_info().use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_downsample", erhe::graphics::Glsl_type::sampler_2d, s_downsample_texture)
    }
    , m_upsample_texture_resource{
        d.get_info().use_bindless_texture
            ? nullptr
            : m_default_uniform_block.add_sampler("s_upsample", erhe::graphics::Glsl_type::sampler_2d, s_upsample_texture)
    }
    , m_shader_path{std::filesystem::path("res") / std::filesystem::path("shaders")}
    , m_shader_stages{
        .downsample_with_lowpass_input{d, make_program(d, "downsample_lowpass", "downsample_lowpass.frag", flag_source_input)},
        .downsample_with_lowpass      {d, make_program(d, "downsample_lowpass", "downsample_lowpass.frag", flag_source_downsample)},
        .downsample                   {d, make_program(d, "downsample",         "downsample.frag",         flag_source_downsample)},
        .upsample_first               {d, make_program(d, "upsample",           "upsample.frag",           flag_first_pass | flag_source_downsample)},
        .upsample                     {d, make_program(d, "upsample",           "upsample.frag",           flag_source_upsample)},
        .upsample_last                {d, make_program(d, "upsample",           "upsample.frag",           flag_last_pass | flag_source_upsample)}
    }
    , m_pipelines{
        .downsample_with_lowpass_input = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Downsample with Lowpass from input",
                .shader_stages  = &m_shader_stages.downsample_with_lowpass_input.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_strip,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .downsample_with_lowpass = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Downsample with Lowpass",
                .shader_stages  = &m_shader_stages.downsample_with_lowpass.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .downsample = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Post Processing Downsample",
                .shader_stages  = &m_shader_stages.downsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample_first = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Post Processing Upsample first",
                .shader_stages  = &m_shader_stages.upsample_first.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Post Processing Upsample",
                .shader_stages  = &m_shader_stages.upsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample_last = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .name           = "Post Processing Upsample last",
                .shader_stages  = &m_shader_stages.upsample_last.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        }
    }
    , m_gpu_timer{d, "Post_processing"}
{
    d.get_shader_monitor().add(m_shader_stages.downsample_with_lowpass_input);
    d.get_shader_monitor().add(m_shader_stages.downsample_with_lowpass      );
    d.get_shader_monitor().add(m_shader_stages.downsample                   );
    d.get_shader_monitor().add(m_shader_stages.upsample                     );
    d.get_shader_monitor().add(m_shader_stages.upsample_last                );
}

auto Post_processing::create_node(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    const std::string_view          name
) -> std::shared_ptr<Post_processing_node>
{
    std::shared_ptr<Post_processing_node> new_node = std::make_shared<Post_processing_node>(graphics_device, rendergraph, *this, name);
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
    erhe::graphics::Device& graphics_device = *m_context.graphics_device;

    const std::shared_ptr<erhe::graphics::Texture> input_texture = node.get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    ERHE_VERIFY(input_texture);

    const std::size_t level_offset_size = erhe::utility::align_offset_power_of_two(
        m_parameter_block.get_size_bytes(),
        m_context.graphics_device->get_buffer_alignment(m_parameter_block.get_binding_target())
    );

    erhe::graphics::Texture_heap texture_heap{graphics_device, *m_dummy_texture.get(), m_sampler_linear, s_reserved_texture_slot_count};

    texture_heap.assign(s_input_texture,      input_texture.get(),           &m_sampler_linear);
    texture_heap.assign(s_downsample_texture, node.downsample_texture.get(), &m_sampler_linear_mipmap_nearest);
    texture_heap.assign(s_upsample_texture,   node.upsample_texture.get(),   &m_sampler_linear_mipmap_nearest);

    texture_heap.bind();

    // Downsample passes
    for (const size_t source_level : node.downsample_source_levels) {
        const size_t                 destination_level = source_level + 1;
        erhe::graphics::Render_pass* render_pass       = node.downsample_render_passes.at(destination_level).get();
        const unsigned int           binding_point     = m_parameter_block.get_binding_point();

        erhe::graphics::Render_command_encoder encoder = m_context.graphics_device->make_render_command_encoder(*render_pass);

        encoder.set_buffer(
            m_parameter_block.get_binding_target(),
            &node.parameter_buffer,
            source_level * level_offset_size,
            m_parameter_block.get_size_bytes(),
            binding_point
        );
        if (source_level == 0) {
            encoder.set_render_pipeline_state(m_pipelines.downsample_with_lowpass_input);
        } else if (source_level < node.lowpass_count) {
            encoder.set_render_pipeline_state(m_pipelines.downsample_with_lowpass);
        } else {
            encoder.set_render_pipeline_state(m_pipelines.downsample);
        }
        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    }

    erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = input_texture->get_width(),
        .height = input_texture->get_height()
    };

    // Upsample passes
    for (const size_t source_level : node.upsample_source_levels) {
        const size_t destination_level = source_level - 1;
        erhe::graphics::Render_pass* render_pass = node.upsample_render_passes.at(destination_level).get();
        const unsigned int binding_point = m_parameter_block.get_binding_point();

        erhe::graphics::Render_command_encoder encoder = m_context.graphics_device->make_render_command_encoder(*render_pass);

        if (source_level == node.upsample_source_levels.front()) {
            encoder.set_render_pipeline_state(m_pipelines.upsample_first);
        } else if (source_level == node.upsample_source_levels.back()) {
            encoder.set_render_pipeline_state(m_pipelines.upsample_last);
        } else {
            encoder.set_render_pipeline_state(m_pipelines.upsample);
        }

        const int render_pass_width  = render_pass->get_render_target_width();
        const int render_pass_height = render_pass->get_render_target_height();
        ERHE_VERIFY(render_pass_width  == node.level_widths .at(destination_level));
        ERHE_VERIFY(render_pass_height == node.level_heights.at(destination_level));
        encoder.set_buffer(
            m_parameter_block.get_binding_target(),
            &node.parameter_buffer,
            source_level * level_offset_size,
            m_parameter_block.get_size_bytes(),
            binding_point
        );
        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    }

    texture_heap.unbind();
}

}

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
