#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace mango {
    namespace filesystem {
        class File;
        class FileStream;
    }
}

namespace erhe::graphics {

// How a loader produced the pixels of an image (Image_info::decode_path),
// for load statistics.
enum class Image_decode_path : unsigned int
{
    decoded            = 0, // PNG / JPEG / ... decode, or DDS blocks as stored
    basis_transcoded   = 1, // KTX2 Basis Universal payload transcoded
    raw_blocks         = 2, // KTX2 raw block image passed through as stored
    raw_blocks_decoded = 3  // KTX2 raw block image decoded to RGBA8 (format not supported by the device)
};

class Image_info
{
public:
    int                      width      {0};
    int                      height     {0};
    int                      depth      {0};
    int                      level_count{0};
    int                      row_stride {0};
    erhe::dataformat::Format format     {erhe::dataformat::Format::format_8_vec4_srgb};
    // True for a normal map stored as a two component X+Y map (X in RGB,
    // Y in A; Z reconstructed in shader). Set by Image_loader_ktx2 when the
    // KTX2 metadata says the file was encoded with `ktx encode --normal-mode`.
    bool                     two_component_normal{false};
    Image_decode_path        decode_path{Image_decode_path::decoded};
};

// Preferred GPU target for sources that are transcoded at load time (KTX2 /
// Basis Universal). The caller picks the preference from device format
// support; loaders that decode to a fixed format (PNG/JPEG, DDS) ignore it.
enum class Transcode_format_preference : unsigned int {
    rgba8 = 0, // uncompressed 8-bit RGBA (always supported)
    bc7,       // BC7 4x4 blocks (desktop GPUs)
    astc_4x4   // ASTC 4x4 blocks (mobile GPUs)
};

// Whether the device samples ASTC 4x4 images. A KTX2 file holding raw
// ASTC 4x4 blocks (vkFormat ASTC, e.g. a "Save for Quest" glTF texture) is
// passed through as stored with native, and decoded to RGBA8 (mip level 0;
// the GPU mipmap generation path rebuilds the chain) with decode_to_rgba8.
enum class Astc_support : unsigned int
{
    decode_to_rgba8 = 0,
    native          = 1
};

// How the wuffs path (PNG / JPEG) delivers the color channels of an image
// with an alpha channel. premultiplied (the default) multiplies RGB by alpha
// while decoding, which is what textures sampled with filtering and
// premultiplied blending want. straight returns the stored bytes unchanged,
// for byte-exact round trips (for example comparing a written PNG against
// the texels it was written from). KTX2 and DDS containers ignore it.
enum class Alpha_mode : unsigned int
{
    premultiplied = 0,
    straight
};

class Image_loader_impl;
class Image_loader_ktx2;
class Image_loader_dds;

class Image_loader
{
public:
    Image_loader  ();
    ~Image_loader () noexcept;
    Image_loader  (const Image_loader&) = delete;
    auto operator=(const Image_loader&) = delete;
    Image_loader  (Image_loader&&)      = delete;
    auto operator=(Image_loader&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info, bool linear, Transcode_format_preference transcode_format_preference = Transcode_format_preference::rgba8, Alpha_mode alpha_mode = Alpha_mode::premultiplied, Astc_support astc_support = Astc_support::decode_to_rgba8) -> bool;
    [[nodiscard]] auto open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, bool linear, Transcode_format_preference transcode_format_preference = Transcode_format_preference::rgba8, Alpha_mode alpha_mode = Alpha_mode::premultiplied, Astc_support astc_support = Astc_support::decode_to_rgba8) -> bool;
    [[nodiscard]] auto load(std::span<std::uint8_t> transfer_buffer) -> bool;
    void close();

private:
    // Routing: KTX2 containers (identified by magic) decode through the
    // Basis Universal transcoder, DDS containers through dds_image;
    // everything else through wuffs. m_use_ktx2 / m_use_dds record which
    // backend the last open() selected.
    std::unique_ptr<Image_loader_impl> m_impl;
    std::unique_ptr<Image_loader_ktx2> m_ktx2;
    std::unique_ptr<Image_loader_dds>  m_dds;
    bool                               m_use_ktx2{false};
    bool                               m_use_dds {false};
};

} // namespace erhe::graphics
