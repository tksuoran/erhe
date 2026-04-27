#include "rendergraph/post_processing.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
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

Post_processing_node_texture_reference::Post_processing_node_texture_reference(
    const std::shared_ptr<Post_processing_node>& node,
    const int                                    direction,
    const size_t                                 level
)
    : m_node     {node}
    , m_direction{direction}
    , m_level    {level}
{
};

Post_processing_node_texture_reference::~Post_processing_node_texture_reference() noexcept = default;

auto Post_processing_node_texture_reference::get_referenced_texture() const -> const erhe::graphics::Texture*
{
    const std::shared_ptr<Post_processing_node> node = m_node.lock();
    if (!node) {
        return nullptr;
    }
    if (m_direction > 0) {
        return (m_level < node->upsample_textures.size())
            ? node->upsample_textures.at(m_level).get()
            : nullptr;
    }
    return (m_level < node->downsample_textures.size())
        ? node->downsample_textures.at(m_level).get()
        : nullptr;
}

// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
Post_processing_node::Post_processing_node(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    Post_processing&                post_processing,
    erhe::utility::Debug_label      debug_label
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, debug_label}
    , m_graphics_device{graphics_device}
    , m_post_processing{post_processing}
{
    register_input ("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    register_output("texture", erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

Post_processing_node::~Post_processing_node() noexcept
{
}

auto Post_processing_node::update_size() -> bool
{
    // Input determines the size of intermediate nodes and size of the input node for the post processing render graph node.
    // Input should be multisample resolved.

    const std::shared_ptr<erhe::graphics::Texture>& input_texture = get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    if (!input_texture) {
        return false; // not yet connected
    }
    const int width  = input_texture->get_width();
    const int height = input_texture->get_height();
    if ((level0_width == width) && (level0_height == height)) {
        return (width > 0) && (height > 0);
    }

    // Defer destruction of old resources until after Rendergraph::execute()
    // completes. Nodes executing later in this frame (Window_imgui_host
    // calling render_draw_data) may still reference these textures via
    // Texture_reference::get_referenced_texture().
    class Old_resources
    {
    public:
        // Destruction order is reverse of declaration: render passes first
        // (they hold raw Texture* to parent textures), then textures.
        std::vector<std::shared_ptr<erhe::graphics::Texture>>     downsample_textures;
        std::vector<std::shared_ptr<erhe::graphics::Texture>>     upsample_textures;
        std::vector<std::unique_ptr<erhe::graphics::Render_pass>> downsample_render_passes;
        std::vector<std::unique_ptr<erhe::graphics::Render_pass>> upsample_render_passes;
    };
    auto old = std::make_shared<Old_resources>();
    old->downsample_textures      = std::move(downsample_textures);
    old->upsample_textures        = std::move(upsample_textures);
    old->downsample_render_passes = std::move(downsample_render_passes);
    old->upsample_render_passes   = std::move(upsample_render_passes);
    get_rendergraph().defer_resource(std::move(old));

    level0_width  = width;
    level0_height = height;
    if (level0_width < 1 || level0_height < 1) {
        return false;
    }

    // Per-level non-mipmapped texture pyramid. Every pass reads from and writes
    // to distinct Texture objects, so no feedback loop UB regardless of the
    // backend's ARB_texture_view support.
    //
    // downsample_textures[k] holds pyramid level k+1 (pyramid starts at half
    // resolution; there is no downsample texture for level 0 which is the
    // input texture itself).
    //
    // upsample_textures[k] holds pyramid level k (the deepest pyramid level
    // is only a downsample destination -- never an upsample target -- so no
    // upsample texture for it).
    int level_width  = level0_width;
    int level_height = level0_height;
    int level        = 0;
    bool is_last_level = false;
    level_widths            .clear();
    level_heights           .clear();
    downsample_source_levels.clear();
    upsample_source_levels  .clear();
    weights                 .clear();
    while ((level_width >= 1) && (level_height >= 1)) {
        level_widths .push_back(level_width);
        level_heights.push_back(level_height);
        is_last_level = (level_width == 1) && (level_height == 1);

        // Downsample texture + render pass: only for level > 0
        // (level 0 is the input resolution; downsample starts at half).
        if (level > 0) {
            std::shared_ptr<erhe::graphics::Texture> downsample_level_texture = std::make_shared<erhe::graphics::Texture>(
                m_graphics_device,
                erhe::graphics::Texture_create_info{
                    .device       = m_graphics_device,
                    .usage_mask   =
                        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                        erhe::graphics::Image_usage_flag_bit_mask::sampled,
                    .type         = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat  = erhe::dataformat::Format::format_16_vec4_float,
                    .use_mipmaps  = false,
                    .sample_count = 0,
                    .width        = level_width,
                    .height       = level_height,
                    .level_count  = 1,
                    .debug_label  = erhe::utility::Debug_label{fmt::format("{} downsample level {}", get_name(), level)}
                }
            );

            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            render_pass_descriptor.color_attachments[0].texture       = downsample_level_texture.get();
            render_pass_descriptor.color_attachments[0].texture_level = 0;
            render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Dont_care;
            render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::undefined;
            render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
            render_pass_descriptor.render_target_width                = level_width;
            render_pass_descriptor.render_target_height               = level_height;
            render_pass_descriptor.debug_label                        = erhe::utility::Debug_label{fmt::format("{} downsample level {}", get_name(), level)};

            std::unique_ptr<erhe::graphics::Render_pass> render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
            downsample_render_passes.push_back(std::move(render_pass));
            downsample_textures.push_back(std::move(downsample_level_texture));
        }

        // Upsample texture + render pass: only for levels before the last.
        if (!is_last_level) {
            std::shared_ptr<erhe::graphics::Texture> upsample_level_texture = std::make_shared<erhe::graphics::Texture>(
                m_graphics_device,
                erhe::graphics::Texture_create_info{
                    .device       = m_graphics_device,
                    .usage_mask   =
                        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                        erhe::graphics::Image_usage_flag_bit_mask::sampled,
                    .type         = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat  = erhe::dataformat::Format::format_16_vec4_float,
                    .use_mipmaps  = false,
                    .sample_count = 0,
                    .width        = level_width,
                    .height       = level_height,
                    .level_count  = 1,
                    .debug_label  = erhe::utility::Debug_label{fmt::format("{} upsample level {}", get_name(), level)}
                }
            );

            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            render_pass_descriptor.color_attachments[0].texture       = upsample_level_texture.get();
            render_pass_descriptor.color_attachments[0].texture_level = 0;
            render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Dont_care;
            render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::undefined;
            render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
            render_pass_descriptor.render_target_width                = level_width;
            render_pass_descriptor.render_target_height               = level_height;
            render_pass_descriptor.debug_label                        = erhe::utility::Debug_label{fmt::format("{} upsample level {}", get_name(), level)};

            std::unique_ptr<erhe::graphics::Render_pass> render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
            upsample_render_passes.push_back(std::move(render_pass));
            upsample_textures.push_back(std::move(upsample_level_texture));
        }

        if (is_last_level) {
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
    return true;
}

void Post_processing_node::update_parameters()
{
    // log_frame->trace("Post_processing_node::update_parameters()");

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

    // The shader samples s_input / s_downsample / s_upsample / s_downsample_dst
    // through combined_image_sampler bindings, not through these UBO handles,
    // so the concrete texture identity here doesn't actually affect rendering.
    // Keep the fields populated (backends that only support bindless would
    // otherwise see zero handles) by using a representative per-role texture.
    const erhe::graphics::Texture& downsample_handle_texture =
        !downsample_textures.empty() ? *downsample_textures.front() : *input_texture;
    const erhe::graphics::Texture& upsample_handle_texture =
        !upsample_textures.empty() ? *upsample_textures.front() : *input_texture;
    const uint64_t input_handle      = m_graphics_device.get_handle(*input_texture.get(),   sampler_linear);
    const uint64_t downsample_handle = m_graphics_device.get_handle(downsample_handle_texture, sampler_linear_mipmap_nearest);
    const uint64_t upsample_handle   = m_graphics_device.get_handle(upsample_handle_texture,   sampler_linear_mipmap_nearest);
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

    const size_t      level_count = level_widths.size();
    const std::size_t byte_count  = level_offset_size * level_count;

    // Any prior range is left in closed+released state by the previous
    // call, so the rvalue temporary that receives those fields via
    // move-assign exchange destructs safely.
    parameter_buffer_range = m_post_processing.get_parameter_buffer_client().acquire(
        erhe::graphics::Ring_buffer_usage::CPU_write,
        byte_count
    );
    const std::span<std::byte> gpu_data   = parameter_buffer_range.get_span();
    const std::size_t          word_count = byte_count / sizeof(float);
    const std::span<float>     float_data{reinterpret_cast<float*   >(gpu_data.data()), word_count};
    const std::span<uint32_t>  uint_data {reinterpret_cast<uint32_t*>(gpu_data.data()), word_count};

    std::size_t write_offset = 0;
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
    parameter_buffer_range.bytes_written(byte_count);
    parameter_buffer_range.close();
    parameter_buffer_range.release();
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
        if (upsample_textures.empty()) {
            return {};
        }
        // upsample_textures[0] is pyramid level 0 = full resolution, written
        // by the last upsample pass. This is the post-processed image.
        return upsample_textures.front();
    }

    return {};
}

void Post_processing_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    if (!m_enabled) {
        return;
    }

    static constexpr std::string_view c_post_processing{"Post_processing"};

    ERHE_PROFILE_FUNCTION();
    //ERHE_PROFILE_GPU_SCOPE(c_post_processing)

    const bool can_post_process = update_size();
    if (!can_post_process) {
        return;
    }

    if (downsample_textures.empty() || upsample_textures.empty()) {
        return;
    }

    // Input texture may have changed
    update_parameters();

    m_post_processing.post_process(*this, command_buffer);
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
    if (flags & flag_first_pass       ) { defines.push_back({"FIRST_PASS", "1"}); }
    if (flags & flag_last_pass        ) { defines.push_back({"LAST_PASS",  "1"}); }
    if (flags & flag_source_input     ) { defines.push_back({"SOURCE", "s_input"}); }
    if (flags & flag_source_downsample) { defines.push_back({"SOURCE", "s_downsample"}); }
    if (flags & flag_source_upsample  ) { defines.push_back({"SOURCE", "s_upsample"}); }
    if (graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left) {
        defines.push_back({"FRAMEBUFFER_TOP_LEFT", "1"});
    }

    return
        erhe::graphics::Shader_stages_create_info{
            .name                = name,
            .defines             = defines,
            .extensions          = extensions,
            .interface_blocks    = { &m_parameter_block },
            .fragment_outputs    = &m_fragment_outputs,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   m_shader_path / std::filesystem::path{"post_processing.vert"}},
                { erhe::graphics::Shader_type::fragment_shader, m_shader_path / fs_path }
            },
            .extra_include_paths = {
                std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
                std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"}
            },
            .bind_group_layout     = &m_bind_group_layout
        };
}

Post_processing::Post_processing(erhe::graphics::Device& d, erhe::graphics::Command_buffer& init_command_buffer, App_context& app_context)
    : m_context         {app_context}
    , m_fragment_outputs{erhe::graphics::Fragment_output{.name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0}}
    , m_dummy_texture   {d.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_16_vec4_float)}
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
    , m_bind_group_layout{
        d,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {.binding_point = 0, .type = erhe::graphics::Binding_type::uniform_buffer},
                // s_input / s_downsample / s_upsample are scalar named samplers
                // bound via Render_command_encoder::set_sampled_image() before
                // each draw. Declared on the bind group layout so the backend
                // knows which Sampler_aspect to use, the Vulkan pipeline
                // layout has matching descriptor slots, and the shader
                // preamble gets the uniform sampler2D declarations.
                {
                    .binding_point   = s_input_texture,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_input",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                },
                {
                    .binding_point   = s_downsample_texture,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_downsample",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                },
                {
                    .binding_point   = s_upsample_texture,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_upsample",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                },
                {
                    .binding_point   = s_downsample_dst_texture,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_downsample_dst",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = false
                }
            },
            .debug_label = "Post processing"
        }
    }
    , m_parameter_buffer_client{
        d,
        m_parameter_block.get_binding_target(),
        "post_processing_parameters",
        m_parameter_block.get_binding_point()
    }
    , m_offsets           {m_parameter_block}
    , m_empty_vertex_input{d}
    , m_shader_path{std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"}}
    , m_shader_stages{
        .downsample_with_lowpass_input{d, make_program(d, "downsample_lowpass", "downsample_lowpass.frag", flag_source_input)},
        .downsample_with_lowpass      {d, make_program(d, "downsample_lowpass", "downsample_lowpass.frag", flag_source_downsample)},
        .downsample                   {d, make_program(d, "downsample",         "downsample.frag",         flag_source_downsample)},
        .upsample_first               {d, make_program(d, "upsample",           "upsample.frag",           flag_first_pass | flag_source_downsample)},
        .upsample                     {d, make_program(d, "upsample",           "upsample.frag",           flag_source_upsample)},
        .upsample_last                {d, make_program(d, "upsample",           "upsample.frag",           flag_last_pass | flag_source_upsample)}
    }
    , m_pipelines{
        .downsample_with_lowpass_input = erhe::graphics::Lazy_render_pipeline{
            d, erhe::graphics::Render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Downsample with Lowpass from input"},
                .shader_stages  = &m_shader_stages.downsample_with_lowpass_input.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle_strip,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .downsample_with_lowpass = erhe::graphics::Lazy_render_pipeline{
            d, erhe::graphics::Render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Downsample with Lowpass"},
                .shader_stages  = &m_shader_stages.downsample_with_lowpass.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .downsample = erhe::graphics::Lazy_render_pipeline{
            d, erhe::graphics::Render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Post Processing Downsample"},
                .shader_stages  = &m_shader_stages.downsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample_first = erhe::graphics::Lazy_render_pipeline{
            d, erhe::graphics::Render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Post Processing Upsample first"},
                .shader_stages  = &m_shader_stages.upsample_first.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample = erhe::graphics::Lazy_render_pipeline{
            d, erhe::graphics::Render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Post Processing Upsample"},
                .shader_stages  = &m_shader_stages.upsample.shader_stages,
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        },
        .upsample_last = erhe::graphics::Lazy_render_pipeline{
            d, erhe::graphics::Render_pipeline_create_info{
                .debug_label    = erhe::utility::Debug_label{"Post Processing Upsample last"},
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

    m_texture_heap = std::make_unique<erhe::graphics::Texture_heap>(
        d,
        *m_dummy_texture.get(),
        m_sampler_linear,
        &m_bind_group_layout
    );
}

Post_processing::~Post_processing() noexcept = default;

auto Post_processing::create_node(
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::utility::Debug_label      debug_label
) -> std::shared_ptr<Post_processing_node>
{
    std::shared_ptr<Post_processing_node> new_node = std::make_shared<Post_processing_node>(graphics_device, rendergraph, *this, debug_label);
    m_nodes.push_back(new_node);
    return new_node;
}

auto Post_processing::get_nodes() -> const std::vector<std::shared_ptr<Post_processing_node>>&
{
    return m_nodes;
}

/// //////////////////////////////////////////
/// //////////////////////////////////////////

void Post_processing::post_process(Post_processing_node& node, erhe::graphics::Command_buffer& command_buffer)
{
    // log_frame->trace("Post_processing::post_process()");
    erhe::graphics::Scoped_debug_group outer_debug_group{"post_process"};

    const std::shared_ptr<erhe::graphics::Texture> input_texture = node.get_consumer_input_texture(erhe::rendergraph::Rendergraph_node_key::viewport_texture);
    ERHE_VERIFY(input_texture);

    log_post_processing->trace(
        "post_process input='{}' {}x{} downsample_levels={} upsample_levels={}",
        input_texture->get_debug_label().string_view(),
        input_texture->get_width(), input_texture->get_height(),
        node.downsample_textures.size(),
        node.upsample_textures.size()
    );

    const std::size_t level_offset_size = erhe::utility::align_offset_power_of_two(
        m_parameter_block.get_size_bytes(),
        m_context.graphics_device->get_buffer_alignment(m_parameter_block.get_binding_target())
    );

    erhe::graphics::Ring_buffer* const parameter_ring_buffer = node.parameter_buffer_range.get_buffer();
    ERHE_VERIFY(parameter_ring_buffer != nullptr);
    erhe::graphics::Buffer* const parameter_buffer = parameter_ring_buffer->get_buffer();
    ERHE_VERIFY(parameter_buffer != nullptr);
    const std::size_t parameter_range_base_offset = node.parameter_buffer_range.get_byte_start_offset_in_buffer();

    m_texture_heap->reset_heap();


    // Downsample passes. Each pass reads pyramid level source_level and
    // writes pyramid level source_level+1 into its own dedicated texture:
    // - source_level == 0: reads input_texture, writes downsample_textures[0].
    // - source_level >  0: reads downsample_textures[source_level-1], writes
    //   downsample_textures[source_level]. Read source and write target are
    //   distinct Texture objects, so no feedback-loop UB.
    erhe::graphics::Render_pass* previous_render_pass = nullptr;
    for (const size_t source_level : node.downsample_source_levels) {
        erhe::graphics::Scoped_debug_group inner_debug_group{"downsample"};

        const size_t                 destination_level = source_level + 1;
        erhe::graphics::Render_pass* render_pass       = node.downsample_render_passes.at(destination_level - 1).get();
        const unsigned int           binding_point     = m_parameter_block.get_binding_point();

        erhe::graphics::Render_command_encoder encoder = m_context.graphics_device->make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*render_pass, command_buffer, previous_render_pass, nullptr};
        previous_render_pass = render_pass;

        encoder.set_bind_group_layout(&m_bind_group_layout);
        encoder.set_buffer(
            m_parameter_block.get_binding_target(),
            parameter_buffer,
            parameter_range_base_offset + source_level * level_offset_size,
            m_parameter_block.get_size_bytes(),
            binding_point
        );
        {
            erhe::graphics::Lazy_render_pipeline& lazy_pipeline =
                (source_level == 0) ? m_pipelines.downsample_with_lowpass_input :
                (source_level < node.lowpass_count) ? m_pipelines.downsample_with_lowpass :
                m_pipelines.downsample;
            erhe::graphics::Render_pipeline* p = lazy_pipeline.get_pipeline_for(render_pass->get_descriptor());
            if (p != nullptr) {
                encoder.set_render_pipeline(*p);
            }
        }
        m_texture_heap->bind(encoder);
        encoder.set_sampled_image(s_input_texture, *input_texture, m_sampler_linear);
        // source_level == 0 reads from s_input via SOURCE; s_downsample is
        // unused. Otherwise read the previous pyramid level's dedicated texture.
        if (source_level == 0) {
            encoder.set_sampled_image(s_downsample_texture, *m_dummy_texture, m_sampler_linear);
        } else {
            encoder.set_sampled_image(s_downsample_texture, *node.downsample_textures.at(source_level - 1), m_sampler_linear);
        }
        // s_upsample and s_downsample_dst are never read by downsample shaders.
        encoder.set_sampled_image(s_upsample_texture,      *m_dummy_texture, m_sampler_linear);
        encoder.set_sampled_image(s_downsample_dst_texture, *m_dummy_texture, m_sampler_linear);
        {
            const int w = render_pass->get_render_target_width();
            const int h = render_pass->get_render_target_height();
            encoder.set_viewport_rect(0, 0, w, h);
            encoder.set_scissor_rect(0, 0, w, h);
        }
        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    }

    erhe::math::Viewport viewport{
        .x      = 0,
        .y      = 0,
        .width  = input_texture->get_width(),
        .height = input_texture->get_height()
    };

    // Upsample passes. Each pass reads two pyramid levels and writes one:
    // - curr: the destination-level content, bound to s_downsample_dst (or
    //   s_input for the final pass that writes pyramid level 0).
    // - SOURCE: the source-level content, bound to s_downsample (first pass,
    //   which has no upsample predecessor to read from) or s_upsample
    //   (later passes, which read the previous upsample iteration's output).
    // Render target and sampled textures are always distinct Texture objects
    // so no feedback-loop UB.
    for (const size_t source_level : node.upsample_source_levels) {
        erhe::graphics::Scoped_debug_group inner_debug_group{"upsample"};
        const size_t destination_level = source_level - 1;
        erhe::graphics::Render_pass* render_pass = node.upsample_render_passes.at(destination_level).get();
        const unsigned int binding_point = m_parameter_block.get_binding_point();

        erhe::graphics::Render_command_encoder encoder = m_context.graphics_device->make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*render_pass, command_buffer, previous_render_pass, nullptr};
        previous_render_pass = render_pass;

        encoder.set_bind_group_layout(&m_bind_group_layout);
        const bool is_first_upsample_pass = (source_level == node.upsample_source_levels.front());
        const bool is_last_upsample_pass  = (source_level == node.upsample_source_levels.back());
        {
            erhe::graphics::Lazy_render_pipeline& lazy_pipeline =
                is_first_upsample_pass ? m_pipelines.upsample_first :
                is_last_upsample_pass  ? m_pipelines.upsample_last  :
                m_pipelines.upsample;
            erhe::graphics::Render_pipeline* p = lazy_pipeline.get_pipeline_for(render_pass->get_descriptor());
            if (p != nullptr) {
                encoder.set_render_pipeline(*p);
            }
        }

        m_texture_heap->bind(encoder);
        encoder.set_sampled_image(s_input_texture, *input_texture, m_sampler_linear);

        // s_downsample is only read by the first upsample pass (as SOURCE),
        // where it reads pyramid level source_level = downsample_textures[source_level - 1].
        if (is_first_upsample_pass) {
            encoder.set_sampled_image(s_downsample_texture, *node.downsample_textures.at(source_level - 1), m_sampler_linear);
        } else {
            encoder.set_sampled_image(s_downsample_texture, *m_dummy_texture, m_sampler_linear);
        }

        // s_upsample is read by every upsample pass except the first as SOURCE.
        // It points to the previous iteration's output = upsample_textures[source_level].
        if (is_first_upsample_pass) {
            encoder.set_sampled_image(s_upsample_texture, *m_dummy_texture, m_sampler_linear);
        } else {
            encoder.set_sampled_image(s_upsample_texture, *node.upsample_textures.at(source_level), m_sampler_linear);
        }

        // s_downsample_dst is read by every upsample pass except the last as curr.
        // It holds pyramid level destination_level = downsample_textures[destination_level - 1]
        // = downsample_textures[source_level - 2].
        if (is_last_upsample_pass) {
            encoder.set_sampled_image(s_downsample_dst_texture, *m_dummy_texture, m_sampler_linear);
        } else {
            encoder.set_sampled_image(s_downsample_dst_texture, *node.downsample_textures.at(source_level - 2), m_sampler_linear);
        }

        const int render_pass_width  = render_pass->get_render_target_width();
        const int render_pass_height = render_pass->get_render_target_height();
        ERHE_VERIFY(render_pass_width  == node.level_widths .at(destination_level));
        ERHE_VERIFY(render_pass_height == node.level_heights.at(destination_level));
        encoder.set_buffer(
            m_parameter_block.get_binding_target(),
            parameter_buffer,
            parameter_range_base_offset + source_level * level_offset_size,
            m_parameter_block.get_size_bytes(),
            binding_point
        );
        encoder.set_viewport_rect(0, 0, render_pass_width, render_pass_height);
        encoder.set_scissor_rect(0, 0, render_pass_width, render_pass_height);
        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
    }

    m_texture_heap->unbind();
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
