#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"
#include "gpu_test_results.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

Compute_program::Compute_program() = default;
Compute_program::~Compute_program() noexcept = default;
Compute_program::Compute_program(Compute_program&&) noexcept = default;
auto Compute_program::operator=(Compute_program&&) noexcept -> Compute_program& = default;

auto Compute_program::is_valid() const -> bool
{
    return (shader_stages != nullptr) && (pipeline != nullptr) && pipeline->is_valid();
}

void Gpu_test::SetUp()
{
    Gpu_test_environment::get().clear_messages();
    Gpu_test_results::get().clear();
}

void Gpu_test::TearDown()
{
    erhe::graphics::Device& graphics_device = device();
    graphics_device.wait_idle();
    const std::vector<Gpu_test_environment::Message> messages = Gpu_test_environment::get().take_messages();
    for (const Gpu_test_environment::Message& message : messages) {
        if (message.first) {
            ADD_FAILURE() << "Device validation error during test: " << message.second;
        } else {
            // Best-practices / advisory warnings are surfaced but not fatal,
            // matching the editor's device-message policy (error -> fatal,
            // warning -> log).
            std::cerr << "[ vk-warn  ] " << message.second << "\n";
        }
    }
}

auto Gpu_test::device() -> erhe::graphics::Device&
{
    return Gpu_test_environment::get().device();
}

void Gpu_test::submit_and_wait(const std::function<void(erhe::graphics::Command_buffer&)>& record_fn)
{
    erhe::graphics::Device& graphics_device = device();

    const bool wait_frame_ok = graphics_device.wait_frame();
    ASSERT_TRUE(wait_frame_ok);

    erhe::graphics::Command_buffer& command_buffer = graphics_device.get_command_buffer(0);
    command_buffer.begin();
    record_fn(command_buffer);
    command_buffer.end();

    erhe::graphics::Command_buffer* command_buffers[] = { &command_buffer };
    graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{command_buffers});
    graphics_device.wait_idle();

    const bool end_frame_ok = graphics_device.end_frame();
    ASSERT_TRUE(end_frame_ok);
}

auto Gpu_test::make_color_target(
    const int                      width,
    const int                      height,
    const erhe::dataformat::Format format,
    const bool                     include_transfer_dst
) -> std::shared_ptr<erhe::graphics::Texture>
{
    erhe::graphics::Device& graphics_device = device();
    const uint64_t usage_mask =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled          |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_src      |
        (include_transfer_dst ? erhe::graphics::Image_usage_flag_bit_mask::transfer_dst : uint64_t{0});
    const erhe::graphics::Texture_create_info create_info{
        .device      = graphics_device,
        .usage_mask  = usage_mask,
        .type        = erhe::graphics::Texture_type::texture_2d,
        .pixelformat = format,
        .width       = width,
        .height      = height,
        .debug_label = erhe::utility::Debug_label{"gpu_test color target"}
    };
    std::shared_ptr<erhe::graphics::Texture> texture =
        std::make_shared<erhe::graphics::Texture>(graphics_device, create_info);

    // Move the fresh texture from UNDEFINED into transfer_src_optimal so render
    // passes can uniformly declare usage_before / layout_before = transfer_src /
    // transfer_src_optimal (the Id_renderer readback pattern), and so
    // read_texture_rgba8's blit always finds it in transfer_src_optimal.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.transition_texture_layout(*texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );
    return texture;
}

auto Gpu_test::make_host_buffer(
    const std::size_t            byte_count,
    erhe::graphics::Buffer_usage usage,
    const char*                  debug_label
) -> std::shared_ptr<erhe::graphics::Buffer>
{
    erhe::graphics::Device& graphics_device = device();
    const erhe::graphics::Buffer_create_info create_info{
        .capacity_byte_count                    = byte_count,
        .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
        .usage                                  = usage,
        // host_read/host_write are required; host_coherent + host_persistent are
        // only preferred (the proven Ring_buffer split). Requiring coherent aborts
        // on backends/modes that cannot provide it -- e.g. OpenGL without direct
        // state access, where persistent mapping (and thus coherent) is unavailable.
        .required_memory_property_bit_mask      =
            erhe::graphics::Memory_property_flag_bit_mask::host_read |
            erhe::graphics::Memory_property_flag_bit_mask::host_write,
        .preferred_memory_property_bit_mask     =
            erhe::graphics::Memory_property_flag_bit_mask::host_coherent |
            erhe::graphics::Memory_property_flag_bit_mask::host_persistent,
        .debug_label = erhe::utility::Debug_label{debug_label}
    };
    return std::make_shared<erhe::graphics::Buffer>(graphics_device, create_info);
}

auto Gpu_test::make_readback_buffer(const std::size_t byte_count, const char* debug_label)
    -> std::shared_ptr<erhe::graphics::Buffer>
{
    return make_host_buffer(
        byte_count,
        erhe::graphics::Buffer_usage::transfer_dst | erhe::graphics::Buffer_usage::storage,
        debug_label
    );
}

auto Gpu_test::read_buffer(erhe::graphics::Buffer& buffer, std::size_t byte_count)
    -> std::vector<std::byte>
{
    if (byte_count == 0) {
        byte_count = buffer.get_capacity_byte_count();
    }
    // make_readback_buffer prefers host_coherent + host_persistent: when the device
    // honors them, GPU writes are visible once the GPU is idle and map_bytes hands
    // back the persistent mapping -- no vkInvalidateMappedMemoryRanges is needed (and
    // invalidate would require the range to be a multiple of nonCoherentAtomSize,
    // which an arbitrary byte_count is not -- VUID-VkMappedMemoryRange-size-01390).
    // When persistent/coherent mapping is unavailable (e.g. OpenGL without direct
    // state access), map_bytes performs a fresh transient mapping that reads the
    // committed data after the GPU is idle.
    const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
    std::vector<std::byte> out(byte_count);
    std::memcpy(out.data(), mapped.data(), byte_count);
    buffer.unmap();
    return out;
}

auto Gpu_test::read_texture_rgba8(const erhe::graphics::Texture& texture)
    -> std::vector<uint8_t>
{
    const int         width         = texture.get_width();
    const int         height        = texture.get_height();
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_texture_rgba8");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                erhe::graphics::Texture_location{.texture = &texture, .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Buffer_texel_location{.buffer = readback.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
            );
        }
    );

    const std::vector<std::byte> raw = read_buffer(*readback, byte_count);
    std::vector<uint8_t> out(byte_count);
    std::memcpy(out.data(), raw.data(), byte_count);
    return out;
}

auto Gpu_test::read_texture_color_bytes(const erhe::graphics::Texture& texture, const std::size_t bytes_per_texel)
    -> std::vector<std::byte>
{
    const int         width         = texture.get_width();
    const int         height        = texture.get_height();
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * bytes_per_texel;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_texture_color_bytes");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                erhe::graphics::Texture_location{.texture = &texture, .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Buffer_texel_location{.buffer = readback.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
            );
        }
    );

    return read_buffer(*readback, byte_count);
}

auto Gpu_test::read_texture_level_bytes(
    const erhe::graphics::Texture& texture,
    const unsigned int             level,
    const std::size_t              bytes_per_texel
) -> std::vector<std::byte>
{
    const int         width         = texture.get_width(level);
    const int         height        = texture.get_height(level);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * bytes_per_texel;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_texture_level_bytes");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                erhe::graphics::Texture_location{.texture = &texture, .slice = 0, .level = static_cast<std::uintptr_t>(level), .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Buffer_texel_location{.buffer = readback.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
            );
        }
    );

    return read_buffer(*readback, byte_count);
}

auto Gpu_test::read_subresource_rgba8(const erhe::graphics::Texture& texture, const unsigned int layer, const unsigned int level)
    -> std::vector<uint8_t>
{
    const int         width         = texture.get_width(level);
    const int         height        = texture.get_height(level);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    const std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_subresource_rgba8");
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                erhe::graphics::Texture_location{.texture = &texture, .slice = static_cast<std::uintptr_t>(layer), .level = static_cast<std::uintptr_t>(level), .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Buffer_texel_location{.buffer = readback.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
            );
        }
    );
    const std::vector<std::byte> raw = read_buffer(*readback, byte_count);
    std::vector<uint8_t> out(byte_count);
    std::memcpy(out.data(), raw.data(), byte_count);
    return out;
}

void Gpu_test::seed_subresource_rgba8(
    const erhe::graphics::Texture& texture,
    const unsigned int             layer,
    const unsigned int             level,
    const std::span<const uint8_t> texels
)
{
    const int         width         = texture.get_width(level);
    const int         height        = texture.get_height(level);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);
    ASSERT_EQ(texels.size(), byte_count) << "seed texel count does not match the subresource";

    const std::shared_ptr<erhe::graphics::Buffer> source =
        make_host_buffer(byte_count, erhe::graphics::Buffer_usage::transfer_src, "seed_subresource_rgba8");
    {
        const std::span<std::byte> mapped = source->map_bytes(0, byte_count);
        std::memcpy(mapped.data(), texels.data(), byte_count);
        source->unmap();
    }
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                erhe::graphics::Buffer_texel_location{.buffer = source.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)},
                glm::ivec3{width, height, 1},
                erhe::graphics::Texture_location{.texture = &texture, .slice = static_cast<std::uintptr_t>(layer), .level = static_cast<std::uintptr_t>(level), .origin = glm::ivec3{0, 0, 0}}
            );
        }
    );
}

auto Gpu_test::read_texture_rgba32f(const erhe::graphics::Texture& texture)
    -> std::vector<float>
{
    const int                    width  = texture.get_width();
    const int                    height = texture.get_height();
    const std::size_t            texels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::vector<std::byte> raw    = read_texture_color_bytes(texture, 4u * sizeof(float));
    std::vector<float> out(texels * 4u);
    std::memcpy(out.data(), raw.data(), out.size() * sizeof(float));
    return out;
}

auto Gpu_test::read_texture_depth32f(const erhe::graphics::Texture& texture)
    -> std::vector<float>
{
    const int         width         = texture.get_width();
    const int         height        = texture.get_height();
    const std::size_t texels        = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * sizeof(float);
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_texture_depth32f");

    // The texture->buffer copy_from_texture overload picks VK_IMAGE_ASPECT_DEPTH
    // for depth formats and reads the tracked layout, so the depth texture must
    // already be in transfer_src_optimal (set via the render pass usage_after /
    // layout_after).
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                erhe::graphics::Texture_location{.texture = &texture, .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Buffer_texel_location{.buffer = readback.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
            );
        }
    );

    const std::vector<std::byte> raw = read_buffer(*readback, byte_count);
    std::vector<float> out(texels);
    std::memcpy(out.data(), raw.data(), out.size() * sizeof(float));
    return out;
}

auto Gpu_test::draw_fullscreen_triangle(
    const char*                                fragment_color_glsl,
    const erhe::graphics::Rasterization_state& rasterization,
    const erhe::graphics::Color_blend_state&   color_blend,
    std::array<double, 4>                      clear_value,
    const int                                  width,
    const int                                  height
) -> std::vector<uint8_t>
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"fullscreen empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    static constexpr const char* vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";
    static constexpr const char* fragment_source = R"glsl(
void main()
{
    out_color = FRAG_COLOR;
}
)glsl";

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "fullscreen_triangle",
        .defines          = { { "FRAG_COLOR", fragment_color_glsl } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    if (!prototype.is_valid()) {
        ADD_FAILURE() << "draw_fullscreen_triangle: shader failed to compile/link";
        return {};
    }
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = clear_value;
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"fullscreen triangle"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = rasterization;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    pipeline_create_info.base.color_blend                       = &color_blend;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    if (!pipeline.is_valid()) {
        ADD_FAILURE() << "draw_fullscreen_triangle: pipeline is not valid";
        return {};
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    return read_texture_rgba8(*color_target);
}

auto Gpu_test::find_depth32f_format() -> erhe::dataformat::Format
{
    erhe::graphics::Device& graphics_device = device();
    const std::vector<erhe::dataformat::Format> supported = graphics_device.get_supported_depth_stencil_formats();
    for (const erhe::dataformat::Format candidate : supported) {
        if ((erhe::dataformat::get_depth_size_bits(candidate) == 32) &&
            (erhe::dataformat::get_stencil_size_bits(candidate) == 0) &&
            graphics_device.get_format_properties(candidate).depth_renderable) {
            return candidate;
        }
    }
    return erhe::dataformat::Format::format_undefined;
}

auto Gpu_test::make_sampled_texture(
    const erhe::graphics::Texture_type type,
    const int                          width,
    const int                          height,
    const int                          depth,
    const int                          array_layer_count,
    const char*                        debug_label
) -> std::shared_ptr<erhe::graphics::Texture>
{
    erhe::graphics::Device& graphics_device = device();
    return std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = type,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .width             = width,
            .height            = height,
            .depth             = depth,
            .array_layer_count = array_layer_count,
            .debug_label       = erhe::utility::Debug_label{debug_label}
        }
    );
}

auto Gpu_test::make_compute_program(
    const char* const                                          name,
    const std::string_view                                     compute_source,
    const std::vector<std::pair<std::string, std::string>>&    defines,
    const std::vector<const erhe::graphics::Shader_resource*>& struct_types,
    const std::vector<const erhe::graphics::Shader_resource*>& interface_blocks,
    const erhe::graphics::Bind_group_layout&                   layout,
    const std::vector<std::string>&                            extensions
) -> Compute_program
{
    Compute_program program{};
    std::vector<erhe::graphics::Shader_stage_extension> stage_extensions;
    for (const std::string& extension : extensions) {
        stage_extensions.push_back(erhe::graphics::Shader_stage_extension{erhe::graphics::Shader_type::compute_shader, extension});
    }
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name              = name,
        .defines           = defines,
        .extensions        = stage_extensions,
        .struct_types      = struct_types,
        .interface_blocks  = interface_blocks,
        .shaders           = { { erhe::graphics::Shader_type::compute_shader, compute_source } },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    if (!prototype.is_valid()) {
        ADD_FAILURE() << name << ": compute shader failed to compile/link";
        return program;
    }
    program.shader_stages = std::make_unique<erhe::graphics::Shader_stages>(device(), std::move(prototype));
    program.pipeline = std::make_unique<erhe::graphics::Compute_pipeline>(
        device(),
        erhe::graphics::Compute_pipeline_data{
            .name              = name,
            .shader_stages     = program.shader_stages.get(),
            .bind_group_layout = &layout
        }
    );
    if (!program.pipeline->is_valid()) {
        ADD_FAILURE() << name << ": compute pipeline is not valid";
    }
    return program;
}

auto Gpu_test::render_fullscreen_pass(
    const erhe::graphics::Bind_group_layout&                layout,
    const std::string_view                                  fragment_source,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const std::span<const Sampled_image>                    images,
    const int                                               width,
    const int                                               height,
    const erhe::dataformat::Format                          format
) -> std::shared_ptr<erhe::graphics::Texture>
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height, format);

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    static constexpr const char* vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

    // gl_FragCoord counts rows from the device's texture origin; image rows
    // count from the image top (NDC +y), so a bottom-left origin flips.
    const bool row0_is_top =
        (device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);
    std::vector<std::pair<std::string, std::string>> all_defines{
        { "TARGET_WIDTH",   std::to_string(width)  },
        { "TARGET_HEIGHT",  std::to_string(height) },
        { "IMAGE_POSITION", row0_is_top ? std::string{"(gl_FragCoord.xy)"} : std::string{"vec2(gl_FragCoord.x, float(TARGET_HEIGHT) - gl_FragCoord.y)"} }
    };
    all_defines.insert(all_defines.end(), defines.begin(), defines.end());

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "fullscreen_pass",
        .defines          = all_defines,
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, fragment_source                 }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    if (!prototype.is_valid()) {
        ADD_FAILURE() << "render_fullscreen_pass: shader failed to compile/link";
        return color_target;
    }
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"fullscreen pass"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &layout;
    pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    if (!pipeline.is_valid()) {
        ADD_FAILURE() << "render_fullscreen_pass: pipeline is not valid";
        return color_target;
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&layout);
            encoder.set_render_pipeline(pipeline);
            for (const Sampled_image& image : images) {
                encoder.set_sampled_image(image.binding_point, *image.texture, *image.sampler);
            }
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );
    return color_target;
}

auto Gpu_test::memory_rows_to_image_rows(const std::span<const uint8_t> rows, const std::size_t bytes_per_row, const int height)
    -> std::vector<uint8_t>
{
    const bool row0_is_top =
        (device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);
    if (row0_is_top) {
        return std::vector<uint8_t>(rows.begin(), rows.end());
    }
    std::vector<uint8_t> out(rows.size());
    for (int y = 0; y < height; ++y) {
        const std::size_t source_row = static_cast<std::size_t>(height - 1 - y);
        std::memcpy(out.data() + (static_cast<std::size_t>(y) * bytes_per_row), rows.data() + (source_row * bytes_per_row), bytes_per_row);
    }
    return out;
}

void Gpu_test::expect_rgba8_near(
    const std::span<const uint8_t> actual,
    const std::span<const uint8_t> expected,
    const int                      width,
    const int                      height,
    const int                      tolerance,
    const std::string_view         label
)
{
    const std::size_t byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    ASSERT_EQ(actual.size(),   byte_count) << label << ": actual image size";
    ASSERT_EQ(expected.size(), byte_count) << label << ": expected image size";
    int         mismatches = 0;
    std::size_t first      = 0;
    for (std::size_t texel = 0; texel < (byte_count / 4u); ++texel) {
        bool ok = true;
        for (std::size_t c = 0; c < 4u; ++c) {
            const int a = actual  [(texel * 4u) + c];
            const int e = expected[(texel * 4u) + c];
            if (std::abs(a - e) > tolerance) {
                ok = false;
            }
        }
        if (!ok) {
            if (mismatches == 0) {
                first = texel;
            }
            ++mismatches;
        }
    }
    const std::size_t w = static_cast<std::size_t>(width);
    EXPECT_EQ(mismatches, 0)
        << label << ": " << mismatches << " of " << (byte_count / 4u) << " texels differ by more than " << tolerance
        << "; first at (" << (first % w) << ", " << (first / w) << ") got {"
        << static_cast<int>(actual[(first * 4u) + 0u]) << ", " << static_cast<int>(actual[(first * 4u) + 1u]) << ", "
        << static_cast<int>(actual[(first * 4u) + 2u]) << ", " << static_cast<int>(actual[(first * 4u) + 3u]) << "} expected {"
        << static_cast<int>(expected[(first * 4u) + 0u]) << ", " << static_cast<int>(expected[(first * 4u) + 1u]) << ", "
        << static_cast<int>(expected[(first * 4u) + 2u]) << ", " << static_cast<int>(expected[(first * 4u) + 3u]) << "}";
}

// --- Ray_query_test ---

namespace {

// Shared prelude of the ray query trace shaders (Ray_query_test::trace_image).
// The ray origin arithmetic is exact in float (power-of-two steps), so
// Ray_query_test::get_ray_origin reproduces it bit for bit.
constexpr const char* c_ray_query_prelude = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = RAY_QUERY_TLAS_BINDING) uniform accelerationStructureEXT s_tlas;

#ifndef RAY_CULL_MASK
#define RAY_CULL_MASK 0xFFu
#endif

uint encode_unorm8(float value)
{
    return uint((clamp(value, 0.0, 1.0) * 255.0) + 0.5);
}

uint pack_rgba8(uint r, uint g, uint b, uint a)
{
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

uint trace_texel(uvec2 texel, vec3 origin, vec3 direction);

void main()
{
    uvec2 texel  = gl_GlobalInvocationID.xy;
    float step   = 2.0 / float(IMAGE_SIZE);
    vec3  origin = vec3(
        -1.0 + ((float(texel.x) + 0.5) * step),
         1.0 - ((float(texel.y) + 0.5) * step),
        RAY_ORIGIN_Z
    );
    Texels.texels[(texel.y * uint(IMAGE_SIZE)) + texel.x] = trace_texel(texel, origin, vec3(0.0, 0.0, -1.0));
}
)glsl";

constexpr uint32_t c_ray_query_texels_binding = 0;
constexpr uint32_t c_ray_query_tlas_binding   = 1;

[[nodiscard]] auto cross_2d(const glm::dvec2 a, const glm::dvec2 b) -> double
{
    return (a.x * b.y) - (a.y * b.x);
}

} // namespace

void Ray_query_test::SetUp()
{
    Gpu_test::SetUp();
    if (!device().get_info().use_ray_query) {
        GTEST_SKIP() << "ray query not supported by this device";
    }
}

auto Ray_query_test::make_mesh(
    const std::span<const glm::vec3> positions,
    const std::span<const uint32_t>  indices,
    const char* const                debug_label
) -> Ray_query_mesh
{
    const erhe::graphics::Buffer_usage usage =
        erhe::graphics::Buffer_usage::acceleration_structure_build_input |
        erhe::graphics::Buffer_usage::shader_device_address;
    const std::size_t vertex_bytes = positions.size() * sizeof(glm::vec3);
    const std::size_t index_bytes  = indices.size() * sizeof(uint32_t);
    Ray_query_mesh mesh{
        .vertex_buffer = make_host_buffer(vertex_bytes, usage, debug_label),
        .index_buffer  = make_host_buffer(index_bytes,  usage, debug_label),
        .vertex_count  = positions.size(),
        .index_count   = indices.size()
    };
    {
        const std::span<std::byte> mapped = mesh.vertex_buffer->map_bytes(0, vertex_bytes);
        std::memcpy(mapped.data(), positions.data(), vertex_bytes);
        mesh.vertex_buffer->unmap();
    }
    {
        const std::span<std::byte> mapped = mesh.index_buffer->map_bytes(0, index_bytes);
        std::memcpy(mapped.data(), indices.data(), index_bytes);
        mesh.index_buffer->unmap();
    }
    return mesh;
}

auto Ray_query_test::get_triangles(const Ray_query_mesh& mesh, const Ray_query_opacity opacity)
    -> erhe::graphics::Acceleration_structure_triangles
{
    return erhe::graphics::Acceleration_structure_triangles{
        .vertex_buffer      = mesh.vertex_buffer.get(),
        .vertex_byte_offset = 0,
        .vertex_byte_stride = sizeof(glm::vec3),
        .vertex_count       = mesh.vertex_count,
        .vertex_format      = erhe::dataformat::Format::format_32_vec3_float,
        .index_buffer       = mesh.index_buffer.get(),
        .index_byte_offset  = 0,
        .index_count        = mesh.index_count,
        .opaque             = (opacity == Ray_query_opacity::opaque)
    };
}

auto Ray_query_test::trace_image(
    const char* const                                           name,
    const std::string_view                                      trace_source,
    const std::vector<std::pair<std::string, std::string>>&     defines,
    const std::function<void(erhe::graphics::Command_buffer&)>& record_builds,
    const erhe::graphics::Acceleration_structure&               tlas
) -> Ray_query_image
{
    Ray_query_image result{};
    if (device().get_info().coordinate_conventions.texture_origin != erhe::math::Texture_origin::top_left) {
        ADD_FAILURE() << name << ": ray query image routing expects texture_origin top_left";
        return result;
    }

    erhe::graphics::Shader_resource texels_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Texels",
            .binding_point = c_ray_query_texels_binding,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    };
    texels_block.add_uint("texels", erhe::graphics::Shader_resource::unsized_array);

    const erhe::graphics::Bind_group_layout layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = c_ray_query_texels_binding,
                    .type          = erhe::graphics::Binding_type::storage_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                },
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = c_ray_query_tlas_binding,
                    .type          = erhe::graphics::Binding_type::acceleration_structure,
                    .name          = "s_tlas",
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"ray query layout"},
            .uses_texture_heap = false
        }
    };

    std::vector<std::pair<std::string, std::string>> all_defines = defines;
    all_defines.emplace_back("IMAGE_SIZE",             std::to_string(c_image_size));
    all_defines.emplace_back("RAY_ORIGIN_Z",           "1.0");
    all_defines.emplace_back("RAY_T_MAX",              "2.0");
    all_defines.emplace_back("RAY_QUERY_TLAS_BINDING", std::to_string(c_ray_query_tlas_binding));
    const std::string source = std::string{c_ray_query_prelude} + std::string{trace_source};
    const Compute_program program = make_compute_program(name, source, all_defines, {}, { &texels_block }, layout, { "GL_EXT_ray_query" });
    if (!program.is_valid()) {
        return result;
    }

    const std::size_t texel_count = static_cast<std::size_t>(c_image_size) * static_cast<std::size_t>(c_image_size);
    const std::size_t data_bytes  = texel_count * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> texel_buffer = make_host_buffer(
        data_bytes,
        erhe::graphics::Buffer_usage::storage | erhe::graphics::Buffer_usage::transfer_src,
        "ray query texels"
    );
    {
        const std::span<std::byte> mapped = texel_buffer->map_bytes(0, data_bytes);
        std::memset(mapped.data(), 0, data_bytes);
        texel_buffer->unmap();
    }
    const std::shared_ptr<erhe::graphics::Texture> texture =
        make_sampled_texture(erhe::graphics::Texture_type::texture_2d, c_image_size, c_image_size, 1, 0, "ray query image");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            if (record_builds) {
                record_builds(command_buffer);
            }
            {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(&layout);
                encoder.set_compute_pipeline(*program.pipeline);
                encoder.set_buffer(erhe::graphics::Buffer_target::storage, texel_buffer.get(), 0, data_bytes, c_ray_query_texels_binding);
                encoder.set_acceleration_structure(c_ray_query_tlas_binding, tlas);
                encoder.dispatch_compute(c_image_size / 8, c_image_size / 8, 1);
            }
            command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::pixel_buffer_barrier_bit);
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                erhe::graphics::Buffer_texel_location{.buffer = texel_buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(c_image_size) * 4u, .bytes_per_image = static_cast<std::uintptr_t>(data_bytes)},
                glm::ivec3{c_image_size, c_image_size, 1},
                erhe::graphics::Texture_location{.texture = texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}}
            );
            command_buffer.transition_texture_layout(*texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*texel_buffer, data_bytes);
    result.texels.resize(texel_count);
    std::memcpy(result.texels.data(), raw.data(), data_bytes);
    result.pixels = read_texture_rgba8(*texture);
    return result;
}

void Ray_query_test::append_model_triangles(
    std::vector<Ray_query_model_triangle>& out,
    const std::span<const glm::vec3>       positions,
    const std::span<const uint32_t>        indices,
    const glm::mat4&                       transform,
    const uint32_t                         id
)
{
    for (std::size_t i = 0; (i + 2) < indices.size(); i += 3) {
        const glm::vec3 p0 = glm::vec3{transform * glm::vec4{positions[indices[i + 0]], 1.0f}};
        const glm::vec3 p1 = glm::vec3{transform * glm::vec4{positions[indices[i + 1]], 1.0f}};
        const glm::vec3 p2 = glm::vec3{transform * glm::vec4{positions[indices[i + 2]], 1.0f}};
        out.push_back(
            Ray_query_model_triangle{
                .p0              = p0,
                .p1              = p1,
                .p2              = p2,
                .primitive_index = static_cast<uint32_t>(i / 3),
                .id              = id
            }
        );
    }
}

auto Ray_query_test::get_ray_origin(const int x, const int y) -> glm::vec3
{
    const float step = 2.0f / static_cast<float>(c_image_size);
    return glm::vec3{
        -1.0f + ((static_cast<float>(x) + 0.5f) * step),
         1.0f - ((static_cast<float>(y) + 0.5f) * step),
        c_ray_origin_z
    };
}

auto Ray_query_test::trace_model(const std::span<const Ray_query_model_triangle> triangles, const int x, const int y)
    -> Ray_query_model_hit
{
    constexpr double edge_epsilon = 1.0e-4;
    constexpr double t_epsilon    = 1.0e-4;
    const glm::vec3  origin       = get_ray_origin(x, y);
    const glm::dvec2 p{origin.x, origin.y};

    Ray_query_model_hit result{};
    double              best_t     = 0.0;
    double              second_t   = 0.0;
    bool                has_second = false;
    for (const Ray_query_model_triangle& triangle : triangles) {
        const glm::dvec2 a{triangle.p0.x, triangle.p0.y};
        const glm::dvec2 b{triangle.p1.x, triangle.p1.y};
        const glm::dvec2 c{triangle.p2.x, triangle.p2.y};
        const double     area2 = cross_2d(b - a, c - a);
        if (std::abs(area2) < 1.0e-12) {
            continue; // edge-on to the ray
        }
        // p = a + u (b - a) + v (c - a)
        const double u  = cross_2d(p - a, c - a) / area2;
        const double v  = cross_2d(b - a, p - a) / area2;
        const double w  = 1.0 - u - v;
        const double z  = (w * triangle.p0.z) + (u * triangle.p1.z) + (v * triangle.p2.z);
        const double t  = static_cast<double>(c_ray_origin_z) - z;
        if ((t < 0.0) || (t > static_cast<double>(c_ray_t_max))) {
            continue;
        }
        // Signed distance of p to the edge opposite each vertex: the vertex's
        // weight times the triangle's height over that edge.
        const double d0 = w * std::abs(area2) / glm::length(c - b);
        const double d1 = u * std::abs(area2) / glm::length(a - c);
        const double d2 = v * std::abs(area2) / glm::length(b - a);
        const double min_distance = std::min(d0, std::min(d1, d2));
        if (min_distance < -edge_epsilon) {
            continue;
        }
        if (min_distance < edge_epsilon) {
            result.ambiguous = true;
            continue;
        }
        if (!result.hit || (t < best_t)) {
            if (result.hit) {
                second_t   = best_t;
                has_second = true;
            }
            best_t              = t;
            result.hit          = true;
            result.t            = static_cast<float>(t);
            result.barycentrics = glm::vec2{static_cast<float>(u), static_cast<float>(v)};
            result.triangle     = &triangle;
        } else if (!has_second || (t < second_t)) {
            second_t   = t;
            has_second = true;
        }
    }
    if (result.hit && has_second && ((second_t - best_t) < t_epsilon)) {
        result.ambiguous = true;
    }
    return result;
}

auto Ray_query_test::encode_unorm8(const float value) -> uint8_t
{
    return static_cast<uint8_t>((std::clamp(value, 0.0f, 1.0f) * 255.0f) + 0.5f);
}

void Ray_query_test::expect_image(
    const Ray_query_image&         image,
    const std::span<const uint8_t> expected,
    const std::span<const uint8_t> ambiguous,
    const int                      tolerance,
    const std::string_view         golden_name
)
{
    const std::size_t texel_count = static_cast<std::size_t>(c_image_size) * static_cast<std::size_t>(c_image_size);
    ASSERT_EQ(image.texels.size(), texel_count) << golden_name << ": SSBO texel count";
    ASSERT_EQ(image.pixels.size(), texel_count * 4u) << golden_name << ": texture readback size";
    ASSERT_EQ(expected.size(), texel_count * 4u) << golden_name << ": expected image size";
    ASSERT_EQ(ambiguous.size(), texel_count) << golden_name << ": ambiguity mask size";

    // The texture is a byte copy of the SSBO.
    std::vector<uint8_t> ssbo_bytes(texel_count * 4u);
    std::memcpy(ssbo_bytes.data(), image.texels.data(), ssbo_bytes.size());
    EXPECT_EQ(std::memcmp(ssbo_bytes.data(), image.pixels.data(), ssbo_bytes.size()), 0)
        << golden_name << ": texture readback differs from the SSBO it was copied from";

    // Ambiguous texels take the GPU's answer.
    std::vector<uint8_t> model(expected.begin(), expected.end());
    std::size_t ambiguous_count = 0;
    for (std::size_t texel = 0; texel < texel_count; ++texel) {
        if (ambiguous[texel] != 0) {
            ++ambiguous_count;
            std::memcpy(model.data() + (texel * 4u), ssbo_bytes.data() + (texel * 4u), 4u);
        }
    }
    EXPECT_LE(ambiguous_count, (texel_count * 3u) / 100u) << golden_name << ": too many texels on triangle edges for the model to decide";
    expect_rgba8_near(ssbo_bytes, model, c_image_size, c_image_size, tolerance, golden_name);

    expect_image_matches_golden(
        golden_name, c_image_size, c_image_size, erhe::dataformat::Format::format_8_vec4_unorm,
        std::as_bytes(std::span<const uint8_t>{image.pixels})
    );
}

} // namespace erhe::graphics::test
