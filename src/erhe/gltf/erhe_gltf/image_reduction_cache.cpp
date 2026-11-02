#include "erhe_gltf/image_reduction_cache.hpp"

#include "erhe_gltf/gltf_log.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_file/file.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <thread>

namespace erhe::gltf {

namespace {

constexpr std::uint64_t c_magic          = 0x3143585445485245ull; // "ERHETXC1"
// Bump with any encoder, preset or filter change.
constexpr std::uint32_t c_format_version = 1;
constexpr const char*   c_encoder_tag    = "bc7e-scalar-ultrafast;astcenc-5.3.0-fastest;box-srgb-linear";

class Header
{
public:
    std::uint64_t magic         {0};
    std::uint32_t format_version{0};
    std::uint32_t format        {0}; // erhe::dataformat::Format
    std::uint64_t key           {0};
    std::int32_t  width         {0};
    std::int32_t  height        {0};
    std::int32_t  level_count   {0};
    std::int32_t  row_stride    {0};
    std::uint64_t payload_bytes {0};
};

[[nodiscard]] auto get_entry_path(const std::filesystem::path& cache_directory, const Image_reduction_cache_key& key) -> std::filesystem::path
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.erhetex", static_cast<unsigned long long>(key.value));
    return cache_directory / name;
}

} // anonymous namespace

auto make_image_reduction_cache_key(
    const std::span<const std::byte>       encoded_stream,
    const erhe::graphics::Image_reduction& reduction,
    const bool                             linear
) -> Image_reduction_cache_key
{
    ERHE_PROFILE_FUNCTION();
    std::uint64_t h = erhe::hash::hash(&c_format_version, sizeof(c_format_version));
    h = erhe::hash::hash(c_encoder_tag, std::strlen(c_encoder_tag), h);
    const std::uint32_t encode_format    = static_cast<std::uint32_t>(reduction.encode_format);
    const std::int32_t  downscale_levels = reduction.downscale_levels;
    const std::uint8_t  linear_byte      = linear ? 1 : 0;
    h = erhe::hash::hash(&encode_format,    sizeof(encode_format),    h);
    h = erhe::hash::hash(&downscale_levels, sizeof(downscale_levels), h);
    h = erhe::hash::hash(&linear_byte,      sizeof(linear_byte),      h);
    h = erhe::hash::hash(encoded_stream.data(), encoded_stream.size(), h);
    return Image_reduction_cache_key{.value = h};
}

auto load_reduced_image(
    const std::filesystem::path&     cache_directory,
    const Image_reduction_cache_key& key,
    erhe::graphics::Image_info&      out_info,
    std::vector<std::uint8_t>&       out_pixels
) -> bool
{
    ERHE_PROFILE_FUNCTION();
    if (cache_directory.empty()) {
        return false;
    }
    std::ifstream file{get_entry_path(cache_directory, key), std::ios::binary};
    if (!file.is_open()) {
        return false;
    }
    Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || (header.magic != c_magic) || (header.format_version != c_format_version) || (header.key != key.value)) {
        return false;
    }
    const erhe::dataformat::Format format = static_cast<erhe::dataformat::Format>(header.format);
    if ((header.width <= 0) || (header.height <= 0) || (header.level_count <= 0)) {
        return false;
    }
    const std::size_t expected = erhe::dataformat::get_mip_chain_byte_count(
        format,
        static_cast<std::size_t>(header.width),
        static_cast<std::size_t>(header.height),
        static_cast<std::size_t>(header.level_count)
    );
    if ((expected == 0) || (header.payload_bytes != expected)) {
        return false;
    }
    out_pixels.resize(expected);
    file.read(reinterpret_cast<char*>(out_pixels.data()), static_cast<std::streamsize>(expected));
    if (!file) {
        out_pixels.clear();
        return false;
    }
    out_info.width       = header.width;
    out_info.height      = header.height;
    out_info.depth       = 1;
    out_info.level_count = header.level_count;
    out_info.row_stride  = header.row_stride;
    out_info.format      = format;
    return true;
}

void store_reduced_image(
    const std::filesystem::path&      cache_directory,
    const Image_reduction_cache_key&  key,
    const erhe::graphics::Image_info& info,
    const std::span<const std::uint8_t> pixels
)
{
    ERHE_PROFILE_FUNCTION();
    if (cache_directory.empty() || !erhe::file::ensure_directory_exists(cache_directory)) {
        return;
    }
    const Header header{
        .magic          = c_magic,
        .format_version = c_format_version,
        .format         = static_cast<std::uint32_t>(info.format),
        .key            = key.value,
        .width          = info.width,
        .height         = info.height,
        .level_count    = info.level_count,
        .row_stride     = info.row_stride,
        .payload_bytes  = pixels.size()
    };
    const std::filesystem::path path = get_entry_path(cache_directory, key);
    // Unique per writing thread: two decode tasks may store the same entry.
    char temp_suffix[48];
    std::snprintf(temp_suffix, sizeof(temp_suffix), ".%016llx.tmp", static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::path temp_path = path;
    temp_path += std::filesystem::path{temp_suffix};
    {
        std::ofstream file{temp_path, std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
            log_gltf->warn("image reduction cache: cannot create '{}'", erhe::file::to_string(temp_path));
            return;
        }
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!file) {
            file.close();
            std::error_code discard;
            std::filesystem::remove(temp_path, discard);
            return;
        }
    }
    std::error_code error_code;
    std::filesystem::rename(temp_path, path, error_code);
    if (error_code) {
        std::error_code discard;
        std::filesystem::remove(temp_path, discard);
    }
}

} // namespace erhe::gltf
