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
            pattern[i + 0] = static_cast<uint8_t>((x * 23 + 5)       & 0xff);
            pattern[i + 1] = static_cast<uint8_t>((y * 41 + 11)      & 0xff);
            pattern[i + 2] = static_cast<uint8_t>(((x * y) * 7 + 2)  & 0xff);
            pattern[i + 3] = 255u;
        }
    }
    return pattern;
}

// A sampled | transfer_src | transfer_dst RGBA8 texture, built directly via
// Texture_create_info (no color_attachment usage, not a render target). sampled
// is required by the Vulkan blit encoder's post-copy SHADER_READ_ONLY transition
// (which both copy_from_buffer and the texture->texture copy_from_texture leave
// the image in and track); transfer_dst to receive a copy; transfer_src so the
// readback blit can copy it out.
auto make_sampled_copy_texture(
    erhe::graphics::Device& graphics_device,
    const int               width,
    const int               height,
    const char*             debug_label
) -> std::shared_ptr<erhe::graphics::Texture>
{
    return std::make_shared<erhe::graphics::Texture>(
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
            .debug_label = erhe::utility::Debug_label{debug_label}
        }
    );
}

} // namespace

// Blit_command_encoder::copy_from_texture (texture -> texture, whole-texture
// overload). Upload a known RGBA8 pattern into a source texture (via the
// buffer->texture copy_from_buffer, which leaves the source tracked in
// SHADER_READ_ONLY_OPTIMAL), copy the whole source image into a destination
// texture, transition the destination to transfer_src_optimal, and read it
// back. Asserts the destination bytes match the source pattern exactly.
//
// This exercises the engine fix that makes the texture->texture copy read the
// source's tracked layout for the pre-barrier (and restore it afterwards)
// instead of hardcoding SHADER_READ_ONLY_OPTIMAL, and that updates the
// destination's tracked layout to SHADER_READ_ONLY_OPTIMAL after the copy.
TEST_F(Gpu_test, copy_from_texture_whole_image)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    const std::vector<uint8_t> source = make_rgba8_pattern(width, height);
    const std::size_t          bytes  = source.size();

    erhe::graphics::Device& graphics_device = device();

    // Host-visible staging buffer holding the source pattern.
    const std::shared_ptr<erhe::graphics::Buffer> staging =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_from_texture staging");
    {
        const std::span<std::byte> mapped = staging->map_bytes(0, bytes);
        std::memcpy(mapped.data(), source.data(), bytes);
        staging->unmap();
    }

    const std::shared_ptr<erhe::graphics::Texture> source_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture source");
    const std::shared_ptr<erhe::graphics::Texture> destination_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture destination");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);

            // Fill the source texture from the staging buffer. This leaves the
            // source texture tracked + physically in SHADER_READ_ONLY_OPTIMAL.
            blit.copy_from_buffer(
                staging.get(),
                0,                                        // source_offset
                static_cast<std::uintptr_t>(width) * 4u, // source_bytes_per_row
                static_cast<std::uintptr_t>(bytes),       // source_bytes_per_image
                glm::ivec3{width, height, 1},             // source_size
                source_texture.get(),
                0,                                        // destination_slice
                0,                                        // destination_level
                glm::ivec3{0, 0, 0}                       // destination_origin
            );

            // Texture -> texture copy of the whole image. The source enters in
            // SHADER_READ_ONLY_OPTIMAL (its tracked layout) and is restored to
            // it; the destination is left tracked in SHADER_READ_ONLY_OPTIMAL.
            blit.copy_from_texture(source_texture.get(), destination_texture.get());

            // read_texture_rgba8 needs the destination in transfer_src_optimal.
            command_buffer.transition_texture_layout(*destination_texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*destination_texture);
    ASSERT_EQ(pixels.size(), source.size());

    int mismatches = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (pixels[i] != source[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << source.size() << " bytes differed after texture->texture copy + readback";
}

// Blit_command_encoder::copy_from_texture (texture -> texture, explicit-origins
// overload), sub-rect copy. Copy a w x h sub-rectangle from a non-zero source
// origin into a non-zero destination origin, then read the destination back and
// assert the copied region landed at the destination origin with the source
// sub-rect's texels. Only the copied region is asserted: the pre-barrier
// transitions the destination from UNDEFINED, so texels outside the copied
// rectangle are not guaranteed to retain any prior contents.
TEST_F(Gpu_test, copy_from_texture_sub_rect)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    // Sub-rect: a 3x2 block taken from source (2,1), placed at destination (4,5).
    constexpr int rect_w = 3;
    constexpr int rect_h = 2;
    constexpr int src_x  = 2;
    constexpr int src_y  = 1;
    constexpr int dst_x  = 4;
    constexpr int dst_y  = 5;

    const std::vector<uint8_t> source = make_rgba8_pattern(width, height);
    const std::size_t          bytes  = source.size();

    erhe::graphics::Device& graphics_device = device();

    const std::shared_ptr<erhe::graphics::Buffer> staging =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_from_texture sub staging");
    {
        const std::span<std::byte> mapped = staging->map_bytes(0, bytes);
        std::memcpy(mapped.data(), source.data(), bytes);
        staging->unmap();
    }

    const std::shared_ptr<erhe::graphics::Texture> source_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture sub source");
    const std::shared_ptr<erhe::graphics::Texture> destination_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture sub destination");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);

            blit.copy_from_buffer(
                staging.get(),
                0,
                static_cast<std::uintptr_t>(width) * 4u,
                static_cast<std::uintptr_t>(bytes),
                glm::ivec3{width, height, 1},
                source_texture.get(),
                0,
                0,
                glm::ivec3{0, 0, 0}
            );

            // Sub-rect texture -> texture copy with non-zero origins.
            blit.copy_from_texture(
                source_texture.get(),
                0,                                  // source_slice
                0,                                  // source_level
                glm::ivec3{src_x, src_y, 0},        // source_origin
                glm::ivec3{rect_w, rect_h, 1},      // source_size
                destination_texture.get(),
                0,                                  // destination_slice
                0,                                  // destination_level
                glm::ivec3{dst_x, dst_y, 0}         // destination_origin
            );

            command_buffer.transition_texture_layout(*destination_texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*destination_texture);
    ASSERT_EQ(pixels.size(), source.size());

    // Assert the copied rectangle landed at (dst_x, dst_y) carrying the source
    // texels from (src_x, src_y).
    int mismatches = 0;
    for (int ry = 0; ry < rect_h; ++ry) {
        for (int rx = 0; rx < rect_w; ++rx) {
            const std::size_t src_index =
                ((static_cast<std::size_t>(src_y + ry) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(src_x + rx)) * 4u;
            const std::size_t dst_index =
                ((static_cast<std::size_t>(dst_y + ry) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(dst_x + rx)) * 4u;
            for (std::size_t c = 0; c < 4u; ++c) {
                if (pixels[dst_index + c] != source[src_index + c]) {
                    ++mismatches;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << (rect_w * rect_h * 4) << " sub-rect bytes differed after texture->texture sub-rect copy";
}

// Texture -> texture region copies, ported from the agfx tests
// CopyTextureToTexture, CopyTextureToTextureMip and CopyTextureToTextureSlice.
//
// Source and destination share one of three shapes, format_8_vec4_unorm
// (make_copy_setup): a 64x64 2D texture, a two-level 128x128 2D texture copied
// level 1 to level 1, and a four-layer 64x64 2D array copied layer 1 to layer
// 2. Every source subresource is seeded with a gradient and every destination
// subresource with a checkerboard (whole-surface copy_from_buffer, read back
// byte-exact). A 32x24 region at a non-zero source origin is copied to a
// non-zero destination origin; afterwards every destination subresource must
// equal its seed with exactly that region replaced, and every source
// subresource its seed, byte for byte.
//
// Patterns and regions are defined in image space (row 0 = image top) and
// converted to memory rows through texture_origin, so the destination golden
// shows the same picture on every backend.

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

class Texture_copy_test : public Gpu_test
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
    // agfx CopyTextureToTexture / CopyTextureToTextureMip / CopyTextureToTextureSlice.
    void run_texture_to_texture(const Copy_kind kind, const char* golden_name)
    {
        const Copy_setup                               setup       = make_copy_setup(kind);
        const std::shared_ptr<erhe::graphics::Texture> source      = make_copy_texture(setup, "copy source");
        const std::shared_ptr<erhe::graphics::Texture> destination = make_copy_texture(setup, "copy destination");
        ASSERT_EQ(destination->get_level_count(), setup.level_count);
        ASSERT_EQ(destination->get_array_layer_count(), setup.array_layer_count);

        // Source subresources hold gradients, destination subresources
        // checkerboards; the blue channel tells the subresources apart.
        std::vector<std::vector<uint8_t>> source_seeds;
        std::vector<std::vector<uint8_t>> destination_seeds;
        for (std::size_t i = 0; i < setup.subresources.size(); ++i) {
            const int          size    = level_size(setup.size, setup.subresources[i].level);
            const unsigned int variant = static_cast<unsigned int>(i);
            source_seeds     .push_back(to_memory_rows(make_gradient_image(size, size, variant), size, size));
            destination_seeds.push_back(to_memory_rows(make_checker_image (size, size, variant), size, size));
        }
        seed_all(*source,      setup, source_seeds);
        seed_all(*destination, setup, destination_seeds);
        if (HasFatalFailure()) {
            return;
        }

        const Copy_subresource& source_subresource      = setup.subresources[setup.source_index];
        const Copy_subresource& destination_subresource = setup.subresources[setup.target_index];
        const int               size                    = level_size(setup.size, destination_subresource.level);
        ASSERT_EQ(size, level_size(setup.size, source_subresource.level));

        const Region source_image       {size / 16, size / 8, size / 2, (size * 3) / 8};
        const Region destination_image  {(size * 3) / 8, size / 2, source_image.width, source_image.height};
        const Region source_memory      = to_memory_region(source_image, size);
        const Region destination_memory = to_memory_region(destination_image, size);

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_texture(
                    source.get(),
                    static_cast<std::uintptr_t>(source_subresource.layer),          // source_slice
                    static_cast<std::uintptr_t>(source_subresource.level),          // source_level
                    glm::ivec3{source_memory.x, source_memory.y, 0},                // source_origin
                    glm::ivec3{source_image.width, source_image.height, 1},         // source_size
                    destination.get(),
                    static_cast<std::uintptr_t>(destination_subresource.layer),     // destination_slice
                    static_cast<std::uintptr_t>(destination_subresource.level),     // destination_level
                    glm::ivec3{destination_memory.x, destination_memory.y, 0}       // destination_origin
                );
            }
        );

        std::vector<std::vector<uint8_t>> expected = destination_seeds;
        paste_region(
            expected[setup.target_index], size, destination_memory,
            extract_region(source_seeds[setup.source_index], size, source_memory)
        );
        const std::vector<std::vector<uint8_t>> readbacks = read_all(*destination, setup, expected, "after the region copy");
        static_cast<void>(read_all(*source, setup, source_seeds, "after the region copy (the source is unchanged)"));
        expect_image_matches_golden(
            golden_name, size, size, erhe::dataformat::Format::format_8_vec4_unorm,
            std::as_bytes(std::span<const uint8_t>{readbacks[setup.target_index]})
        );
    }
};

} // namespace

// agfx CopyTextureToTexture: a 32x24 region of a gradient 64x64 texture into a
// non-zero origin of a checkerboard-seeded 64x64 texture; the rest of the
// destination keeps its seed.
TEST_F(Texture_copy_test, copy_texture_to_texture)
{
    run_texture_to_texture(Copy_kind::plain, "copy_texture_to_texture");
}

// agfx CopyTextureToTextureMip: the same between level 1 of two two-level
// 128x128 textures; level 0 of both keeps its seed.
TEST_F(Texture_copy_test, copy_texture_to_texture_mip)
{
    run_texture_to_texture(Copy_kind::mip, "copy_texture_to_texture_mip");
}

// agfx CopyTextureToTextureSlice: the same from layer 1 of a four-layer 64x64
// array into layer 2 of another; the other layers keep their seeds.
TEST_F(Texture_copy_test, copy_texture_to_texture_slice)
{
    run_texture_to_texture(Copy_kind::slice, "copy_texture_to_texture_slice");
}

} // namespace erhe::graphics::test
