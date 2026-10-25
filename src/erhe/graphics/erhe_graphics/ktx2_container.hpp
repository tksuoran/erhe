#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics {

// Minimal KTX2 container support for raw block-compressed images - the
// "Save for Quest" glTF textures (doc/gltf_extensions/ERHE_texture_ktx2.md):
// one 2D image, one layer, one face, a mip chain, a non-zero vkFormat, and
// no supercompression or Zstandard supercompression (each level compressed
// on its own). Basis Universal KTX2 files (vkFormat 0) are parsed by the
// basist transcoder in Image_loader_ktx2 instead.

// supercompressionScheme values.
enum class Ktx2_supercompression : unsigned int
{
    none = 0,
    zstd = 2
};

// vkFormat values (VkFormat enum) for the formats the raw path handles.
constexpr std::uint32_t ktx2_vk_format_undefined        = 0;   // Basis Universal payload
constexpr std::uint32_t ktx2_vk_format_astc_4x4_unorm   = 157; // VK_FORMAT_ASTC_4x4_UNORM_BLOCK
constexpr std::uint32_t ktx2_vk_format_astc_4x4_srgb    = 158; // VK_FORMAT_ASTC_4x4_SRGB_BLOCK

// KTXwriterScParams value marking a two component X+Y normal map (X in RGB,
// Y in A), the same marker `ktx encode --normal-mode` writes.
constexpr std::string_view ktx2_normal_mode_sc_params = "--normal-mode";

class Ktx2_level
{
public:
    std::uint64_t byte_offset            {0};
    std::uint64_t byte_length            {0}; // stored (supercompressed) bytes
    std::uint64_t uncompressed_byte_length{0};
};

// Header, level index and key/value data of a KTX2 file. The spans point
// into the buffer passed to parse_ktx2(), which must outlive this object.
class Ktx2_container
{
public:
    std::uint32_t                       vk_format               {0};
    std::uint32_t                       type_size               {0};
    std::uint32_t                       width                   {0};
    std::uint32_t                       height                  {0};
    std::uint32_t                       depth                   {0};
    std::uint32_t                       layer_count             {0};
    std::uint32_t                       face_count              {0};
    std::uint32_t                       level_count             {0}; // as stored; 0 means "generate", reported as 1
    std::uint32_t                       supercompression_scheme {0};
    std::span<const std::uint8_t>       dfd;                          // including the leading dfdTotalSize word
    std::span<const std::uint8_t>       kvd;
    std::vector<Ktx2_level>             levels;                       // level 0 (largest) first

    // Value of a key in the key/value data (without the terminating NUL
    // when the value carries one); empty when the key is absent.
    [[nodiscard]] auto find_value(std::string_view key) const -> std::string_view;
    // Stored level bytes (supercompressed when the file is); empty when the
    // level is out of range.
    [[nodiscard]] auto get_level_data(std::span<const std::uint8_t> file, std::uint32_t level) const -> std::span<const std::uint8_t>;
    // Writes the level's uncompressed bytes (exactly uncompressed_byte_length)
    // to 'destination', inflating a Zstandard level. False on a corrupt level
    // or an unsupported scheme.
    [[nodiscard]] auto read_level(std::span<const std::uint8_t> file, std::uint32_t level, std::span<std::uint8_t> destination) const -> bool;
};

// Parses the header and indices and checks that every region lies inside
// the buffer. Returns false (and logs) on a malformed file.
[[nodiscard]] auto parse_ktx2(std::span<const std::uint8_t> file, Ktx2_container& container) -> bool;

// The erhe format for a raw-block vkFormat, or format_undefined when the
// vkFormat is not one the raw path handles.
[[nodiscard]] auto get_ktx2_raw_block_format(std::uint32_t vk_format) -> erhe::dataformat::Format;

// Whether a two component normal map marker (ktx2_normal_mode_sc_params in
// KTXwriterScParams) is present.
[[nodiscard]] auto is_ktx2_two_component_normal(const Ktx2_container& container) -> bool;

// Input of write_ktx2_raw_blocks(): one 2D image with its mip chain.
class Ktx2_raw_blocks_image
{
public:
    erhe::dataformat::Format                   format{erhe::dataformat::Format::format_astc_4x4_srgb}; // astc_4x4 unorm / srgb
    std::uint32_t                              width {0};
    std::uint32_t                              height{0};
    std::vector<std::span<const std::uint8_t>> levels; // level 0 (largest) first, tightly packed blocks
    bool                                       two_component_normal{false}; // writes ktx2_normal_mode_sc_params
    Ktx2_supercompression                      supercompression{Ktx2_supercompression::none};
};

// Writes a KTX2 file: header, level index, a Khronos Data Format
// descriptor for the block format, KTXwriter (+ KTXwriterScParams for a two
// component normal map) key/value data, and the levels (stored smallest
// first; 16-byte aligned without supercompression, each level compressed on
// its own and unaligned with zstd). Returns an empty vector when the format
// is not supported or a level has the wrong size.
[[nodiscard]] auto write_ktx2_raw_blocks(const Ktx2_raw_blocks_image& image) -> std::vector<std::uint8_t>;

// Transcodes a Basis Universal KTX2 file (vkFormat 0) to a raw ASTC 4x4 KTX2
// file with all of its mip levels, in the given color space, optionally
// Zstandard supercompressed. A two
// component normal map (detected the way Image_loader_ktx2 does) keeps its
// X+Y layout (X in RGB, Y in A) and its marker. Returns an empty vector on
// failure.
enum class Ktx2_color_space : unsigned int
{
    srgb   = 0,
    linear = 1
};
[[nodiscard]] auto transcode_ktx2_basis_to_astc_4x4(
    std::span<const std::uint8_t> basis_ktx2,
    Ktx2_color_space              color_space,
    Ktx2_supercompression         supercompression
) -> std::vector<std::uint8_t>;

// Whether a KTX2 file holds Basis Universal data (vkFormat 0) or raw
// blocks (any other vkFormat). Both false for a buffer that is not KTX2.
[[nodiscard]] auto is_ktx2_basis       (std::span<const std::uint8_t> file) -> bool;
[[nodiscard]] auto is_ktx2_raw_blocks  (std::span<const std::uint8_t> file) -> bool;

} // namespace erhe::graphics
