#pragma once

// Image_info lives in image_loader_wuffs.hpp; including it directly (not
// through image_loader.hpp) keeps this header out of a cycle - the wuffs
// header forward-declares Image_loader_ktx2 for its routing member.
#include "erhe_graphics/image_loader_wuffs.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {

class Image_loader_ktx2_impl_state;

// Raw block KTX2 files (non-zero vkFormat, no supercompression; ASTC 4x4
// today - the "Save for Quest" glTF textures,
// doc/gltf_extensions/ERHE_texture_ktx2.md) are not transcoded: with
// Astc_support::native the stored levels are passed through as-is, otherwise
// mip level 0 is decoded to RGBA8 with the ARM astc-encoder decoder and the
// GPU mipmap generation path rebuilds the chain. The file's vkFormat decides
// SRGB / UNORM (the 'linear' argument is ignored), and a KTXwriterScParams
// "--normal-mode" key marks a two component X+Y normal map.
//
// Decodes KTX2 containers holding Basis Universal supercompressed data
// (ETC1S or UASTC), as referenced by glTF KHR_texture_basisu - e.g. the
// textures inside OpenXR XR_FB_render_model controller GLBs. With a
// block-compressed transcode preference (BC7 / ASTC 4x4; BC5 when the image
// is detected as a two component X+Y normal map) the image stays
// GPU-compressed and the file's full mip chain is exposed (level_count is
// the file's mip count, load() writes all levels contiguously,
// largest-first, tightly packed - same contract as Image_loader_dds).
// With the rgba8 preference the image is transcoded to 8-bit RGBA, mip
// level 0 only; the GPU mipmap generation path rebuilds the chain. Used by
// Image_loader, which routes to this class on the KTX2 magic; same
// open/load contract: the buffer passed to open() must stay alive until
// load() has been called.
class Image_loader_ktx2
{
public:
    Image_loader_ktx2 ();
    ~Image_loader_ktx2() noexcept;
    Image_loader_ktx2 (const Image_loader_ktx2&) = delete;
    auto operator=    (const Image_loader_ktx2&) = delete;
    Image_loader_ktx2 (Image_loader_ktx2&&)      = delete;
    auto operator=    (Image_loader_ktx2&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info, bool linear, Transcode_format_preference transcode_format_preference = Transcode_format_preference::rgba8, Astc_support astc_support = Astc_support::decode_to_rgba8) -> bool;
    [[nodiscard]] auto open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, bool linear, Transcode_format_preference transcode_format_preference = Transcode_format_preference::rgba8, Astc_support astc_support = Astc_support::decode_to_rgba8) -> bool;
    [[nodiscard]] auto load(std::span<std::uint8_t> transfer_buffer) -> bool;
    void close();

    // True when the buffer starts with the KTX2 container identifier.
    [[nodiscard]] static auto is_ktx2(const std::span<const std::uint8_t>& buffer_view) -> bool;

private:
    std::unique_ptr<Image_loader_ktx2_impl_state> m_state;
};

} // namespace erhe::graphics
