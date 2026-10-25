#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/image_loader_ktx2.hpp"
#include "erhe_profile/profile.hpp"

#include <zstd/zstd.h>

#include <algorithm>
#include <cstring>

namespace erhe::graphics {

namespace {

constexpr std::uint8_t c_ktx2_identifier[12] = {
    0xABu, 0x4Bu, 0x54u, 0x58u, 0x20u, 0x32u, 0x30u, 0xBBu, 0x0Du, 0x0Au, 0x1Au, 0x0Au
};
constexpr std::size_t c_header_byte_count      = 80;
constexpr std::size_t c_level_index_byte_count = 24;

// Khronos Data Format Specification 1.3 constants used by the ASTC
// descriptor.
constexpr std::uint32_t c_khr_df_version_number    = 2;
constexpr std::uint32_t c_khr_df_model_astc        = 162;
constexpr std::uint32_t c_khr_df_primaries_bt709   = 1;
constexpr std::uint32_t c_khr_df_transfer_linear   = 1;
constexpr std::uint32_t c_khr_df_transfer_srgb     = 2;
constexpr std::uint32_t c_khr_df_channel_astc_data = 0;

// Zstandard level for supercompressed levels: ASTC blocks reach about half
// their size; higher levels gain little and cost save time.
constexpr int c_zstd_level = 6;

[[nodiscard]] auto read_u32(const std::span<const std::uint8_t> bytes, const std::size_t offset) -> std::uint32_t
{
    return
        (static_cast<std::uint32_t>(bytes[offset + 0])      ) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) <<  8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

[[nodiscard]] auto read_u64(const std::span<const std::uint8_t> bytes, const std::size_t offset) -> std::uint64_t
{
    return
        (static_cast<std::uint64_t>(read_u32(bytes, offset))) |
        (static_cast<std::uint64_t>(read_u32(bytes, offset + 4)) << 32);
}

void write_u32(std::vector<std::uint8_t>& out, const std::size_t offset, const std::uint32_t value)
{
    out[offset + 0] = static_cast<std::uint8_t>( value        & 0xFFu);
    out[offset + 1] = static_cast<std::uint8_t>((value >>  8) & 0xFFu);
    out[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    out[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void write_u64(std::vector<std::uint8_t>& out, const std::size_t offset, const std::uint64_t value)
{
    write_u32(out, offset,     static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    write_u32(out, offset + 4, static_cast<std::uint32_t>(value >> 32));
}

[[nodiscard]] auto align_up(const std::size_t value, const std::size_t alignment) -> std::size_t
{
    return ((value + alignment - 1) / alignment) * alignment;
}

[[nodiscard]] auto region_fits(const std::size_t file_size, const std::uint64_t offset, const std::uint64_t length) -> bool
{
    return (offset <= file_size) && (length <= (file_size - offset));
}

[[nodiscard]] auto get_vk_format(const erhe::dataformat::Format format) -> std::uint32_t
{
    switch (format) {
        case erhe::dataformat::Format::format_astc_4x4_unorm: return ktx2_vk_format_astc_4x4_unorm;
        case erhe::dataformat::Format::format_astc_4x4_srgb:  return ktx2_vk_format_astc_4x4_srgb;
        default:                                              return ktx2_vk_format_undefined;
    }
}

// One key/value entry: length word, key, NUL, value, padding to 4.
void append_key_value(std::vector<std::uint8_t>& out, const std::string_view key, const std::string_view value)
{
    const std::size_t entry_byte_count = key.size() + 1 + value.size() + 1; // key NUL value NUL
    const std::size_t start = out.size();
    out.resize(start + 4 + align_up(entry_byte_count, 4), std::uint8_t{0});
    write_u32(out, start, static_cast<std::uint32_t>(entry_byte_count));
    std::memcpy(out.data() + start + 4, key.data(), key.size());
    std::memcpy(out.data() + start + 4 + key.size() + 1, value.data(), value.size());
}

} // anonymous namespace

auto Ktx2_container::find_value(const std::string_view key) const -> std::string_view
{
    std::size_t offset = 0;
    while ((offset + 4) <= kvd.size()) {
        const std::uint32_t entry_byte_count = read_u32(kvd, offset);
        const std::size_t   entry_start      = offset + 4;
        if (entry_byte_count > (kvd.size() - entry_start)) {
            break;
        }
        const std::string_view entry{reinterpret_cast<const char*>(kvd.data() + entry_start), entry_byte_count};
        const std::size_t nul = entry.find('\0');
        if ((nul != std::string_view::npos) && (entry.substr(0, nul) == key)) {
            std::string_view value = entry.substr(nul + 1);
            if (!value.empty() && (value.back() == '\0')) {
                value.remove_suffix(1);
            }
            return value;
        }
        offset = entry_start + align_up(entry_byte_count, 4);
    }
    return {};
}

auto Ktx2_container::get_level_data(const std::span<const std::uint8_t> file, const std::uint32_t level) const -> std::span<const std::uint8_t>
{
    if (level >= levels.size()) {
        return {};
    }
    const Ktx2_level& entry = levels[level];
    return file.subspan(static_cast<std::size_t>(entry.byte_offset), static_cast<std::size_t>(entry.byte_length));
}

auto Ktx2_container::read_level(const std::span<const std::uint8_t> file, const std::uint32_t level, const std::span<std::uint8_t> destination) const -> bool
{
    if ((level >= levels.size()) || (destination.size() != levels[level].uncompressed_byte_length)) {
        return false;
    }
    const std::span<const std::uint8_t> stored = get_level_data(file, level);
    switch (static_cast<Ktx2_supercompression>(supercompression_scheme)) {
        case Ktx2_supercompression::none: {
            if (stored.size() != destination.size()) {
                return false;
            }
            std::memcpy(destination.data(), stored.data(), stored.size());
            return true;
        }
        case Ktx2_supercompression::zstd: {
            const std::size_t result = ZSTD_decompress(destination.data(), destination.size(), stored.data(), stored.size());
            if (ZSTD_isError(result) || (result != destination.size())) {
                log_texture->warn("KTX2: zstd level {} inflate failed: {}", level, ZSTD_isError(result) ? ZSTD_getErrorName(result) : "size mismatch");
                return false;
            }
            return true;
        }
        default: {
            log_texture->warn("KTX2: unsupported supercompression scheme {}", supercompression_scheme);
            return false;
        }
    }
}

auto parse_ktx2(const std::span<const std::uint8_t> file, Ktx2_container& container) -> bool
{
    if ((file.size() < c_header_byte_count) || (std::memcmp(file.data(), c_ktx2_identifier, sizeof(c_ktx2_identifier)) != 0)) {
        log_texture->warn("KTX2: not a KTX2 file");
        return false;
    }
    container.vk_format               = read_u32(file, 12);
    container.type_size               = read_u32(file, 16);
    container.width                   = read_u32(file, 20);
    container.height                  = read_u32(file, 24);
    container.depth                   = read_u32(file, 28);
    container.layer_count             = read_u32(file, 32);
    container.face_count              = read_u32(file, 36);
    container.level_count             = read_u32(file, 40);
    container.supercompression_scheme = read_u32(file, 44);
    const std::uint32_t dfd_offset    = read_u32(file, 48);
    const std::uint32_t dfd_length    = read_u32(file, 52);
    const std::uint32_t kvd_offset    = read_u32(file, 56);
    const std::uint32_t kvd_length    = read_u32(file, 60);

    const std::uint32_t stored_level_count = std::max(container.level_count, 1u);
    const std::uint64_t level_index_end    = c_header_byte_count + (static_cast<std::uint64_t>(stored_level_count) * c_level_index_byte_count);
    if (level_index_end > file.size()) {
        log_texture->warn("KTX2: level index outside the file");
        return false;
    }
    if (!region_fits(file.size(), dfd_offset, dfd_length) || !region_fits(file.size(), kvd_offset, kvd_length)) {
        log_texture->warn("KTX2: data format descriptor or key/value data outside the file");
        return false;
    }
    container.dfd = file.subspan(dfd_offset, dfd_length);
    container.kvd = file.subspan(kvd_offset, kvd_length);
    container.levels.clear();
    for (std::uint32_t level = 0; level < stored_level_count; ++level) {
        const std::size_t entry_offset = c_header_byte_count + (static_cast<std::size_t>(level) * c_level_index_byte_count);
        const Ktx2_level entry{
            .byte_offset              = read_u64(file, entry_offset),
            .byte_length              = read_u64(file, entry_offset + 8),
            .uncompressed_byte_length = read_u64(file, entry_offset + 16)
        };
        if (!region_fits(file.size(), entry.byte_offset, entry.byte_length)) {
            log_texture->warn("KTX2: level {} outside the file", level);
            return false;
        }
        container.levels.push_back(entry);
    }
    return true;
}

auto get_ktx2_raw_block_format(const std::uint32_t vk_format) -> erhe::dataformat::Format
{
    switch (vk_format) {
        case ktx2_vk_format_astc_4x4_unorm: return erhe::dataformat::Format::format_astc_4x4_unorm;
        case ktx2_vk_format_astc_4x4_srgb:  return erhe::dataformat::Format::format_astc_4x4_srgb;
        default:                            return erhe::dataformat::Format::format_undefined;
    }
}

auto is_ktx2_two_component_normal(const Ktx2_container& container) -> bool
{
    const std::string_view params = container.find_value("KTXwriterScParams");
    return
        (params.find("--normal-mode") != std::string_view::npos) ||
        (params.find("--normal_mode") != std::string_view::npos) ||
        (params.find("--normal_map")  != std::string_view::npos);
}

auto write_ktx2_raw_blocks(const Ktx2_raw_blocks_image& image) -> std::vector<std::uint8_t>
{
    ERHE_PROFILE_FUNCTION();

    const std::uint32_t vk_format = get_vk_format(image.format);
    if ((vk_format == ktx2_vk_format_undefined) || image.levels.empty() || (image.width == 0) || (image.height == 0)) {
        log_texture->warn("KTX2 write: unsupported format or empty image");
        return {};
    }
    const std::size_t level_count = image.levels.size();
    for (std::size_t level = 0; level < level_count; ++level) {
        const std::size_t level_width  = std::max<std::size_t>(1, image.width  >> level);
        const std::size_t level_height = std::max<std::size_t>(1, image.height >> level);
        const std::size_t expected     = erhe::dataformat::get_image_level_size_bytes(image.format, level_width, level_height);
        if (image.levels[level].size() != expected) {
            log_texture->warn("KTX2 write: level {} has {} bytes, expected {}", level, image.levels[level].size(), expected);
            return {};
        }
    }

    // Stored level bytes: the levels themselves, or each level compressed
    // on its own.
    const bool zstd = (image.supercompression == Ktx2_supercompression::zstd);
    std::vector<std::vector<std::uint8_t>> compressed_levels;
    if (zstd) {
        compressed_levels.resize(level_count);
        for (std::size_t level = 0; level < level_count; ++level) {
            const std::span<const std::uint8_t> raw = image.levels[level];
            std::vector<std::uint8_t>& out_level = compressed_levels[level];
            out_level.resize(ZSTD_compressBound(raw.size()));
            const std::size_t result = ZSTD_compress(out_level.data(), out_level.size(), raw.data(), raw.size(), c_zstd_level);
            if (ZSTD_isError(result)) {
                log_texture->warn("KTX2 write: zstd level {} failed: {}", level, ZSTD_getErrorName(result));
                return {};
            }
            out_level.resize(result);
        }
    }
    const auto stored_level = [&](const std::size_t level) -> std::span<const std::uint8_t> {
        return zstd
            ? std::span<const std::uint8_t>{compressed_levels[level].data(), compressed_levels[level].size()}
            : image.levels[level];
    };

    // Data format descriptor: dfdTotalSize + one basic block with one sample.
    const bool          srgb             = (image.format == erhe::dataformat::Format::format_astc_4x4_srgb);
    const std::size_t   block_extent     = erhe::dataformat::get_block_extent(image.format);
    const std::uint32_t block_byte_count = static_cast<std::uint32_t>(erhe::dataformat::get_block_size_bytes(image.format));
    constexpr std::uint32_t basic_block_byte_count = 24 + 16; // header + one sample
    std::vector<std::uint8_t> dfd(4 + basic_block_byte_count, std::uint8_t{0});
    write_u32(dfd,  0, static_cast<std::uint32_t>(dfd.size()));
    write_u32(dfd,  4, 0); // vendorId 0 (Khronos), descriptorType 0 (basic)
    write_u32(dfd,  8, c_khr_df_version_number | (basic_block_byte_count << 16));
    write_u32(dfd, 12,
        c_khr_df_model_astc |
        (c_khr_df_primaries_bt709 << 8) |
        ((srgb ? c_khr_df_transfer_srgb : c_khr_df_transfer_linear) << 16)
        // flags 0: straight alpha
    );
    write_u32(dfd, 16,
        (static_cast<std::uint32_t>(block_extent - 1)) |
        (static_cast<std::uint32_t>(block_extent - 1) << 8)
        // texelBlockDimension2 / 3 = 0 (depth 1, 1 plane)
    );
    write_u32(dfd, 20, zstd ? 0u : block_byte_count); // bytesPlane0: 0 for supercompressed data
    write_u32(dfd, 24, 0);                // bytesPlane4..7
    // Sample 0: the whole block, ASTC data channel.
    write_u32(dfd, 28, 0u | ((block_byte_count * 8 - 1) << 16) | (c_khr_df_channel_astc_data << 24));
    write_u32(dfd, 32, 0);          // sample position
    write_u32(dfd, 36, 0);          // sampleLower
    write_u32(dfd, 40, 0xFFFFFFFFu); // sampleUpper

    // Key/value data, keys sorted by byte value.
    std::vector<std::uint8_t> kvd;
    append_key_value(kvd, "KTXwriter", "erhe");
    if (image.two_component_normal) {
        append_key_value(kvd, "KTXwriterScParams", ktx2_normal_mode_sc_params);
    }

    const std::size_t level_index_offset = c_header_byte_count;
    const std::size_t dfd_offset         = level_index_offset + (level_count * c_level_index_byte_count);
    const std::size_t kvd_offset         = dfd_offset + dfd.size();
    const std::size_t kvd_end            = kvd_offset + kvd.size();
    // Levels are stored smallest first, aligned to lcm(texel block size, 4)
    // = 16; supercompressed levels are not aligned.
    const std::size_t level_alignment = zstd ? 1 : 16;
    std::vector<std::size_t> level_offsets(level_count, 0);
    std::size_t file_size = kvd_end;
    for (std::size_t i = 0; i < level_count; ++i) {
        const std::size_t level = level_count - 1 - i;
        file_size = align_up(file_size, level_alignment);
        level_offsets[level] = file_size;
        file_size += stored_level(level).size();
    }

    std::vector<std::uint8_t> out(file_size, std::uint8_t{0});
    std::memcpy(out.data(), c_ktx2_identifier, sizeof(c_ktx2_identifier));
    write_u32(out, 12, vk_format);
    write_u32(out, 16, 1);            // typeSize: 1 for block-compressed formats
    write_u32(out, 20, image.width);
    write_u32(out, 24, image.height);
    write_u32(out, 28, 0);            // pixelDepth: 2D
    write_u32(out, 32, 0);            // layerCount: not an array
    write_u32(out, 36, 1);            // faceCount
    write_u32(out, 40, static_cast<std::uint32_t>(level_count));
    write_u32(out, 44, static_cast<std::uint32_t>(image.supercompression));
    write_u32(out, 48, static_cast<std::uint32_t>(dfd_offset));
    write_u32(out, 52, static_cast<std::uint32_t>(dfd.size()));
    write_u32(out, 56, static_cast<std::uint32_t>(kvd_offset));
    write_u32(out, 60, static_cast<std::uint32_t>(kvd.size()));
    write_u64(out, 64, 0);            // sgdByteOffset
    write_u64(out, 72, 0);            // sgdByteLength
    for (std::size_t level = 0; level < level_count; ++level) {
        const std::size_t entry_offset = level_index_offset + (level * c_level_index_byte_count);
        const std::span<const std::uint8_t> stored = stored_level(level);
        write_u64(out, entry_offset,      level_offsets[level]);
        write_u64(out, entry_offset +  8, stored.size());
        write_u64(out, entry_offset + 16, image.levels[level].size()); // uncompressedByteLength
        std::memcpy(out.data() + level_offsets[level], stored.data(), stored.size());
    }
    std::memcpy(out.data() + dfd_offset, dfd.data(), dfd.size());
    if (!kvd.empty()) {
        std::memcpy(out.data() + kvd_offset, kvd.data(), kvd.size());
    }
    return out;
}

auto transcode_ktx2_basis_to_astc_4x4(
    const std::span<const std::uint8_t> basis_ktx2,
    const Ktx2_color_space              color_space,
    const Ktx2_supercompression         supercompression
) -> std::vector<std::uint8_t>
{
    ERHE_PROFILE_FUNCTION();

    Image_loader_ktx2 loader;
    Image_info        info{};
    const bool linear = (color_space == Ktx2_color_space::linear);
    if (!loader.open(basis_ktx2, info, linear, Transcode_format_preference::astc_4x4)) {
        return {};
    }
    const std::size_t chain_byte_count = erhe::dataformat::get_mip_chain_byte_count(
        info.format,
        static_cast<std::size_t>(info.width),
        static_cast<std::size_t>(info.height),
        static_cast<std::size_t>(info.level_count)
    );
    std::vector<std::uint8_t> blocks(chain_byte_count);
    if (!loader.load(std::span<std::uint8_t>{blocks.data(), blocks.size()})) {
        return {};
    }
    loader.close();

    Ktx2_raw_blocks_image image{
        .format               = info.format,
        .width                = static_cast<std::uint32_t>(info.width),
        .height               = static_cast<std::uint32_t>(info.height),
        .levels               = {},
        .two_component_normal = info.two_component_normal,
        .supercompression     = supercompression
    };
    std::size_t offset = 0;
    for (int level = 0; level < info.level_count; ++level) {
        const std::size_t level_width  = std::max<std::size_t>(1, static_cast<std::size_t>(info.width)  >> level);
        const std::size_t level_height = std::max<std::size_t>(1, static_cast<std::size_t>(info.height) >> level);
        const std::size_t byte_count   = erhe::dataformat::get_image_level_size_bytes(info.format, level_width, level_height);
        image.levels.push_back(std::span<const std::uint8_t>{blocks.data() + offset, byte_count});
        offset += byte_count;
    }
    return write_ktx2_raw_blocks(image);
}

auto is_ktx2_basis(const std::span<const std::uint8_t> file) -> bool
{
    return Image_loader_ktx2::is_ktx2(file) && (file.size() >= c_header_byte_count) && (read_u32(file, 12) == ktx2_vk_format_undefined);
}

auto is_ktx2_raw_blocks(const std::span<const std::uint8_t> file) -> bool
{
    return Image_loader_ktx2::is_ktx2(file) && (file.size() >= c_header_byte_count) && (read_u32(file, 12) != ktx2_vk_format_undefined);
}

} // namespace erhe::graphics
