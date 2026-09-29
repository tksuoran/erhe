#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Build a deterministic per-texel RGBA8 pattern for a width x height image,
// tightly packed at width*4 bytes per row.
auto make_rgba8_pattern(const int width, const int height) -> std::vector<uint8_t>
{
    std::vector<uint8_t> pattern(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            pattern[i + 0] = static_cast<uint8_t>((x * 17 + 3) & 0xff);
            pattern[i + 1] = static_cast<uint8_t>((y * 31 + 7) & 0xff);
            pattern[i + 2] = static_cast<uint8_t>(((x + y) * 13 + 1) & 0xff);
            pattern[i + 3] = 255u;
        }
    }
    return pattern;
}

} // namespace

// Blit_command_encoder::copy_from_buffer (buffer -> texture). Host-write a known
// RGBA8 pattern into a transfer_src buffer, copy it into a fresh texture's level
// 0, transition the texture to transfer_src_optimal, and read it back. Asserts
// the bytes match exactly.
//
// The destination texture is created with sampled usage (in addition to
// transfer_src | transfer_dst): the Vulkan blit encoder's buffer->image copy
// hardcodes a post-copy transition to SHADER_READ_ONLY_OPTIMAL and tracks that
// layout, which requires the image to have been created with SAMPLED usage. The
// texture is built directly via Texture_create_info (not via make_color_target)
// so it carries no color_attachment usage and is not a render target.
TEST_F(Gpu_test, copy_from_buffer_to_texture)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    const std::vector<uint8_t> source = make_rgba8_pattern(width, height);
    const std::size_t          bytes  = source.size();

    erhe::graphics::Device& graphics_device = device();

    // Host-visible source buffer with the pattern.
    const std::shared_ptr<erhe::graphics::Buffer> source_buffer =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_from_buffer source");
    {
        const std::span<std::byte> mapped = source_buffer->map_bytes(0, bytes);
        std::memcpy(mapped.data(), source.data(), bytes);
        source_buffer->unmap();
    }

    // Destination texture: sampled (required by the encoder's post-copy
    // SHADER_READ_ONLY transition) + transfer_dst (copy target) + transfer_src
    // (so read_texture_rgba8 can blit it out).
    const std::shared_ptr<erhe::graphics::Texture> destination = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"copy_from_buffer destination"}
        }
    );

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            // source_bytes_per_image = width*height*4 == whole image; copy the full extent.
            blit.copy_from_buffer(
                source_buffer.get(),
                0,                                                      // source_offset
                static_cast<std::uintptr_t>(width) * 4u,               // source_bytes_per_row
                static_cast<std::uintptr_t>(bytes),                    // source_bytes_per_image
                glm::ivec3{width, height, 1},                          // source_size
                destination.get(),
                0,                                                     // destination_slice
                0,                                                     // destination_level
                glm::ivec3{0, 0, 0}                                    // destination_origin
            );
            // copy_from_buffer leaves the image tracked + physically in
            // SHADER_READ_ONLY_OPTIMAL; read_texture_rgba8 needs transfer_src_optimal.
            command_buffer.transition_texture_layout(*destination, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*destination);
    ASSERT_EQ(pixels.size(), source.size());

    int mismatches = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (pixels[i] != source[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << source.size() << " bytes differed after buffer->texture copy + readback";
}

// Buffer <-> texture copies, ported from the agfx tests CopyBufferToTexture,
// CopyBufferToTextureMip, CopyBufferToTextureSlice, CopyTextureToBuffer,
// CopyTextureToBufferMip and CopyTextureToBufferSlice.
//
// Three texture shapes, format_8_vec4_unorm (make_copy_setup): a 64x64 2D
// texture, a two-level 128x128 2D texture copied at level 1 (64x64) and a
// four-layer 64x64 2D array copied at layer 2. Every subresource is first
// seeded with a whole-surface copy_from_buffer and read back byte-exact. Every
// texel is a function of (x, y, variant), so each copy has an exact CPU
// expectation; after the copy every subresource is read back and compared byte
// for byte (a copy into or out of the wrong level or layer shows up there).
//
// Patterns and regions are defined in image space (row 0 = image top) and
// converted to memory rows (row 0 at the device's texture origin) through
// texture_origin before they reach the device, so a region lands at the same
// place of the picture on every backend and one golden serves them all: image
// goldens go through expect_image_matches_golden, which orders rows top-down;
// the texture-to-buffer payload rows are ordered top-down by the test before
// expect_buffer_matches_golden.

namespace {

constexpr uint8_t c_junk_byte = 0xEEu; // leading / padding bytes of a copy source buffer
constexpr uint8_t c_fill_byte = 0xCDu; // prefill of a copy destination buffer

// A rectangle of texels.
class Region final
{
public:
    int x;
    int y;
    int width;
    int height;
};

class Copy_subresource final
{
public:
    unsigned int layer{0};
    unsigned int level{0};
};

enum class Copy_kind : unsigned int {
    plain,  // texture_2d 64x64, one level
    mip,    // texture_2d 128x128, two levels; level 1 (64x64) is copied
    slice   // texture_2d_array 64x64, four layers; layer 2 is copied
};

class Copy_setup final
{
public:
    erhe::graphics::Texture_type  type;
    int                           size;
    int                           array_layer_count;
    int                           level_count;
    std::vector<Copy_subresource> subresources;
    std::size_t                   target_index; // the subresource the copy writes (or reads)
    std::size_t                   source_index; // texture->texture: the source subresource
    const char*                   label;
};

[[nodiscard]] auto make_copy_setup(const Copy_kind kind) -> Copy_setup
{
    switch (kind) {
        case Copy_kind::mip: {
            return Copy_setup{
                .type              = erhe::graphics::Texture_type::texture_2d,
                .size              = 128,
                .array_layer_count = 0,
                .level_count       = 2,
                .subresources      = { Copy_subresource{0, 0}, Copy_subresource{0, 1} },
                .target_index      = 1,
                .source_index      = 1,
                .label             = "copy mip"
            };
        }
        case Copy_kind::slice: {
            return Copy_setup{
                .type              = erhe::graphics::Texture_type::texture_2d_array,
                .size              = 64,
                .array_layer_count = 4,
                .level_count       = 1,
                .subresources      = { Copy_subresource{0, 0}, Copy_subresource{1, 0}, Copy_subresource{2, 0}, Copy_subresource{3, 0} },
                .target_index      = 2,
                .source_index      = 1,
                .label             = "copy slice"
            };
        }
        case Copy_kind::plain:
        default: {
            return Copy_setup{
                .type              = erhe::graphics::Texture_type::texture_2d,
                .size              = 64,
                .array_layer_count = 0,
                .level_count       = 1,
                .subresources      = { Copy_subresource{0, 0} },
                .target_index      = 0,
                .source_index      = 0,
                .label             = "copy plain"
            };
        }
    }
}

[[nodiscard]] auto level_size(const int size, const unsigned int level) -> int
{
    return std::max(1, size >> level);
}

[[nodiscard]] auto texel_offset(const int surface_width, const int x, const int y) -> std::size_t
{
    return ((static_cast<std::size_t>(y) * static_cast<std::size_t>(surface_width)) + static_cast<std::size_t>(x)) * 4u;
}

// Image-space patterns (row 0 = image top), tightly packed RGBA8. Every texel
// value is a function of (x, y, variant) only, so the expected bytes of any
// copy are computable on the CPU.
//
// Gradient: red grows to the right, green grows downward, blue codes the
// variant. Every texel of a row / column is distinct.
[[nodiscard]] auto make_gradient_image(const int width, const int height, const unsigned int variant) -> std::vector<uint8_t>
{
    std::vector<uint8_t> image(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    const int x_span = std::max(1, width - 1);
    const int y_span = std::max(1, height - 1);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = texel_offset(width, x, y);
            image[index + 0u] = static_cast<uint8_t>((x * 255) / x_span);
            image[index + 1u] = static_cast<uint8_t>((y * 255) / y_span);
            image[index + 2u] = static_cast<uint8_t>(32u + (48u * variant));
            image[index + 3u] = 255u;
        }
    }
    return image;
}

// Checkerboard of 8x8 cells in two dark tones, tinted per variant.
[[nodiscard]] auto make_checker_image(const int width, const int height, const unsigned int variant) -> std::vector<uint8_t>
{
    std::vector<uint8_t> image(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = texel_offset(width, x, y);
            const unsigned int tone = ((((x / 8) + (y / 8)) & 1) != 0) ? 40u : 90u;
            image[index + 0u] = static_cast<uint8_t>(tone + (30u * variant));
            image[index + 1u] = static_cast<uint8_t>(tone);
            image[index + 2u] = static_cast<uint8_t>(150u - tone);
            image[index + 3u] = 255u;
        }
    }
    return image;
}

[[nodiscard]] auto flip_rows(const std::span<const uint8_t> texels, const std::size_t bytes_per_row, const int row_count) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out(texels.size());
    for (int row = 0; row < row_count; ++row) {
        const std::size_t source_row = static_cast<std::size_t>(row_count - 1 - row);
        std::memcpy(out.data() + (static_cast<std::size_t>(row) * bytes_per_row), texels.data() + (source_row * bytes_per_row), bytes_per_row);
    }
    return out;
}

[[nodiscard]] auto extract_region(const std::span<const uint8_t> surface, const int surface_width, const Region& region) -> std::vector<uint8_t>
{
    const std::size_t    row_bytes = static_cast<std::size_t>(region.width) * 4u;
    std::vector<uint8_t> out(row_bytes * static_cast<std::size_t>(region.height));
    for (int row = 0; row < region.height; ++row) {
        std::memcpy(
            out.data() + (static_cast<std::size_t>(row) * row_bytes),
            surface.data() + texel_offset(surface_width, region.x, region.y + row),
            row_bytes
        );
    }
    return out;
}

void paste_region(std::vector<uint8_t>& surface, const int surface_width, const Region& region, const std::span<const uint8_t> texels)
{
    const std::size_t row_bytes = static_cast<std::size_t>(region.width) * 4u;
    for (int row = 0; row < region.height; ++row) {
        std::memcpy(
            surface.data() + texel_offset(surface_width, region.x, region.y + row),
            texels.data() + (static_cast<std::size_t>(row) * row_bytes),
            row_bytes
        );
    }
}

// Byte-exact comparison reporting the count of differing bytes and the first
// differing offset.
void expect_bytes_equal(const std::span<const uint8_t> actual, const std::span<const uint8_t> expected, const std::string_view what)
{
    ASSERT_EQ(actual.size(), expected.size()) << what << ": size differs";
    std::size_t differing = 0;
    std::size_t first     = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            if (differing == 0) {
                first = i;
            }
            ++differing;
        }
    }
    EXPECT_EQ(differing, 0u)
        << what << ": " << differing << " of " << actual.size() << " bytes differ, first at offset " << first
        << " (got " << static_cast<int>(actual[first]) << ", expected " << static_cast<int>(expected[first]) << ")";
}

class Copy_test : public Gpu_test
{
protected:
    [[nodiscard]] auto row0_is_top() -> bool
    {
        return device().get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left;
    }

    // Image rows (row 0 = image top) to memory rows (row 0 at the device's
    // texture origin). The conversion is its own inverse.
    [[nodiscard]] auto to_memory_rows(const std::span<const uint8_t> image, const int width, const int height) -> std::vector<uint8_t>
    {
        if (row0_is_top()) {
            return std::vector<uint8_t>(image.begin(), image.end());
        }
        return flip_rows(image, static_cast<std::size_t>(width) * 4u, height);
    }

    // An image-space region of a surface surface_height rows tall, in memory
    // rows: the copy origins passed to the blit encoder.
    [[nodiscard]] auto to_memory_region(const Region& region, const int surface_height) -> Region
    {
        if (row0_is_top()) {
            return region;
        }
        return Region{region.x, surface_height - region.y - region.height, region.width, region.height};
    }

    [[nodiscard]] auto make_copy_texture(const Copy_setup& setup, const char* label) -> std::shared_ptr<erhe::graphics::Texture>
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
                .type              = setup.type,
                .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
                .width             = setup.size,
                .height            = setup.size,
                .array_layer_count = setup.array_layer_count,
                .level_count       = setup.level_count,
                .debug_label       = erhe::utility::Debug_label{label}
            }
        );
    }

    // Seeds every subresource with memory-row texels (a whole-surface
    // copy_from_buffer each) and asserts each reads back unchanged.
    void seed_all(const erhe::graphics::Texture& texture, const Copy_setup& setup, const std::vector<std::vector<uint8_t>>& seeds)
    {
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            seed_subresource_rgba8(texture, setup.subresources[i].layer, setup.subresources[i].level, seeds[i]);
        }
        static_cast<void>(read_all(texture, setup, seeds, "after the whole-surface seed copy"));
    }

    // Reads every subresource back, asserts each equals expected[i] byte for
    // byte and returns the readbacks.
    [[nodiscard]] auto read_all(
        const erhe::graphics::Texture&           texture,
        const Copy_setup&                        setup,
        const std::vector<std::vector<uint8_t>>& expected,
        const std::string_view                   when
    ) -> std::vector<std::vector<uint8_t>>
    {
        std::vector<std::vector<uint8_t>> readbacks;
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            const Copy_subresource& subresource = setup.subresources[i];
            readbacks.push_back(read_subresource_rgba8(texture, subresource.layer, subresource.level));
            const std::string what =
                std::string{texture.get_debug_label().string_view()} + " layer " + std::to_string(subresource.layer) +
                " level " + std::to_string(subresource.level) + " " + std::string{when};
            expect_bytes_equal(readbacks.back(), expected[i], what);
        }
        return readbacks;
    }
    // agfx CopyBufferToTexture / CopyBufferToTextureMip / CopyBufferToTextureSlice.
    void run_buffer_to_texture(const Copy_kind kind, const char* golden_name)
    {
        const Copy_setup                               setup   = make_copy_setup(kind);
        const std::shared_ptr<erhe::graphics::Texture> texture = make_copy_texture(setup, setup.label);
        ASSERT_EQ(texture->get_level_count(), setup.level_count);
        ASSERT_EQ(texture->get_array_layer_count(), setup.array_layer_count);

        // Whole-surface copies: every subresource gets its own checkerboard.
        std::vector<std::vector<uint8_t>> seeds;
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            const int size = level_size(setup.size, setup.subresources[i].level);
            seeds.push_back(to_memory_rows(make_checker_image(size, size, static_cast<unsigned int>(i)), size, size));
        }
        seed_all(*texture, setup, seeds);
        if (HasFatalFailure()) {
            return;
        }

        // Region copy: a gradient patch from a padded buffer at a non-zero
        // offset, placed at a non-zero origin of the target subresource.
        const Copy_subresource& target        = setup.subresources[setup.target_index];
        const int               target_size   = level_size(setup.size, target.level);
        const Region            image_region  {target_size / 8, target_size / 4, target_size / 2, (target_size * 3) / 8};
        const Region            memory_region = to_memory_region(image_region, target_size);
        const std::vector<uint8_t> patch = to_memory_rows(
            make_gradient_image(image_region.width, image_region.height, 4u), image_region.width, image_region.height
        );

        constexpr std::size_t source_offset   = 256;
        const std::size_t     row_bytes       = static_cast<std::size_t>(image_region.width) * 4u;
        const std::size_t     bytes_per_row   = row_bytes + 32u;
        const std::size_t     bytes_per_image = bytes_per_row * static_cast<std::size_t>(image_region.height);
        const std::size_t     buffer_size     = source_offset + bytes_per_image;
        std::vector<uint8_t> staging(buffer_size, c_junk_byte);
        for (int row = 0; row < image_region.height; ++row) {
            std::memcpy(
                staging.data() + source_offset + (static_cast<std::size_t>(row) * bytes_per_row),
                patch.data() + (static_cast<std::size_t>(row) * row_bytes),
                row_bytes
            );
        }
        const std::shared_ptr<erhe::graphics::Buffer> source =
            make_host_buffer(buffer_size, erhe::graphics::Buffer_usage::transfer_src, "copy_buffer_to_texture source");
        {
            const std::span<std::byte> mapped = source->map_bytes(0, buffer_size);
            std::memcpy(mapped.data(), staging.data(), buffer_size);
            source->unmap();
        }
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    source.get(),
                    static_cast<std::uintptr_t>(source_offset),
                    static_cast<std::uintptr_t>(bytes_per_row),
                    static_cast<std::uintptr_t>(bytes_per_image),
                    glm::ivec3{image_region.width, image_region.height, 1},
                    texture.get(),
                    static_cast<std::uintptr_t>(target.layer),          // destination_slice
                    static_cast<std::uintptr_t>(target.level),          // destination_level
                    glm::ivec3{memory_region.x, memory_region.y, 0}     // destination_origin
                );
            }
        );

        std::vector<std::vector<uint8_t>> expected = seeds;
        paste_region(expected[setup.target_index], target_size, memory_region, patch);
        const std::vector<std::vector<uint8_t>> readbacks = read_all(*texture, setup, expected, "after the region copy");
        expect_image_matches_golden(
            golden_name, target_size, target_size, erhe::dataformat::Format::format_8_vec4_unorm,
            std::as_bytes(std::span<const uint8_t>{readbacks[setup.target_index]})
        );
    }

    // agfx CopyTextureToBuffer / CopyTextureToBufferMip / CopyTextureToBufferSlice.
    void run_texture_to_buffer(const Copy_kind kind, const char* golden_name)
    {
        const Copy_setup                               setup   = make_copy_setup(kind);
        const std::shared_ptr<erhe::graphics::Texture> texture = make_copy_texture(setup, setup.label);
        ASSERT_EQ(texture->get_level_count(), setup.level_count);
        ASSERT_EQ(texture->get_array_layer_count(), setup.array_layer_count);

        // The source subresource holds a gradient, every other one a checkerboard.
        std::vector<std::vector<uint8_t>> image_seeds;
        std::vector<std::vector<uint8_t>> seeds;
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            const int          size    = level_size(setup.size, setup.subresources[i].level);
            const unsigned int variant = static_cast<unsigned int>(i);
            image_seeds.push_back((i == setup.target_index) ? make_gradient_image(size, size, variant) : make_checker_image(size, size, variant));
            seeds.push_back(to_memory_rows(image_seeds.back(), size, size));
        }
        seed_all(*texture, setup, seeds);
        if (HasFatalFailure()) {
            return;
        }

        const Copy_subresource& target        = setup.subresources[setup.target_index];
        const int               target_size   = level_size(setup.size, target.level);
        const Region            image_region  {(target_size / 4) - 4, target_size / 8, target_size / 2, (target_size * 3) / 8};
        const Region            memory_region = to_memory_region(image_region, target_size);

        // Destination layout: 256 leading bytes, rows padded by 64 bytes, 64
        // trailing bytes; everything outside the payload keeps the prefill.
        constexpr std::size_t destination_offset = 256;
        const std::size_t     row_bytes          = static_cast<std::size_t>(image_region.width) * 4u;
        const std::size_t     bytes_per_row      = row_bytes + 64u;
        const std::size_t     payload_bytes      = bytes_per_row * static_cast<std::size_t>(image_region.height);
        const std::size_t     buffer_size        = destination_offset + payload_bytes + 64u;
        const std::shared_ptr<erhe::graphics::Buffer> destination = make_readback_buffer(buffer_size, "copy_texture_to_buffer destination");
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.fill_buffer(destination.get(), 0, static_cast<std::uintptr_t>(buffer_size), c_fill_byte);
            }
        );
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_texture(
                    texture.get(),
                    static_cast<std::uintptr_t>(target.layer),          // source_slice
                    static_cast<std::uintptr_t>(target.level),          // source_level
                    glm::ivec3{memory_region.x, memory_region.y, 0},    // source_origin
                    glm::ivec3{image_region.width, image_region.height, 1},
                    destination.get(),
                    static_cast<std::uintptr_t>(destination_offset),
                    static_cast<std::uintptr_t>(bytes_per_row),
                    static_cast<std::uintptr_t>(payload_bytes)
                );
            }
        );

        const std::vector<std::byte> raw = read_buffer(*destination, buffer_size);
        std::vector<uint8_t> output(buffer_size);
        std::memcpy(output.data(), raw.data(), buffer_size);

        // The payload rows are memory rows; order them top-down (as the image
        // golden helper does, from texture_origin) so one golden serves every
        // backend.
        if (!row0_is_top()) {
            const std::vector<uint8_t> top_down = flip_rows(
                std::span<const uint8_t>{output}.subspan(destination_offset, payload_bytes), bytes_per_row, image_region.height
            );
            std::memcpy(output.data() + destination_offset, top_down.data(), payload_bytes);
        }

        std::vector<uint8_t> expected(buffer_size, c_fill_byte);
        const std::vector<uint8_t>& image = image_seeds[setup.target_index];
        for (int row = 0; row < image_region.height; ++row) {
            std::memcpy(
                expected.data() + destination_offset + (static_cast<std::size_t>(row) * bytes_per_row),
                image.data() + texel_offset(target_size, image_region.x, image_region.y + row),
                row_bytes
            );
        }
        expect_bytes_equal(output, expected, "destination buffer");
        static_cast<void>(read_all(*texture, setup, seeds, "after the copy to the buffer (the source is unchanged)"));
        expect_buffer_matches_golden(golden_name, std::as_bytes(std::span<const uint8_t>{output}));
    }
};

} // namespace

// agfx CopyBufferToTexture: whole-surface copy (the seed), then a padded,
// offset buffer region into a non-zero origin of a 64x64 2D texture.
TEST_F(Copy_test, copy_buffer_to_texture)
{
    run_buffer_to_texture(Copy_kind::plain, "copy_buffer_to_texture");
}

// agfx CopyBufferToTextureMip: region copy into level 1 of a two-level
// 128x128 texture; level 0 keeps its seed.
TEST_F(Copy_test, copy_buffer_to_texture_mip)
{
    run_buffer_to_texture(Copy_kind::mip, "copy_buffer_to_texture_mip");
}

// agfx CopyBufferToTextureSlice: region copy into layer 2 of a four-layer
// 64x64 array; the other layers keep their seeds.
TEST_F(Copy_test, copy_buffer_to_texture_slice)
{
    run_buffer_to_texture(Copy_kind::slice, "copy_buffer_to_texture_slice");
}

// agfx CopyTextureToBuffer: a region of a 64x64 2D texture into a buffer at a
// non-zero offset with padded rows.
TEST_F(Copy_test, copy_texture_to_buffer)
{
    run_texture_to_buffer(Copy_kind::plain, "copy_texture_to_buffer");
}

// agfx CopyTextureToBufferMip: the same from level 1 of a two-level 128x128
// texture.
TEST_F(Copy_test, copy_texture_to_buffer_mip)
{
    run_texture_to_buffer(Copy_kind::mip, "copy_texture_to_buffer_mip");
}

// agfx CopyTextureToBufferSlice: the same from layer 2 of a four-layer 64x64
// array.
TEST_F(Copy_test, copy_texture_to_buffer_slice)
{
    run_texture_to_buffer(Copy_kind::slice, "copy_texture_to_buffer_slice");
}

} // namespace erhe::graphics::test
