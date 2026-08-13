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

// Decodes KTX2 containers holding Basis Universal supercompressed data
// (ETC1S or UASTC), as referenced by glTF KHR_texture_basisu - e.g. the
// textures inside OpenXR XR_FB_render_model controller GLBs. The image is
// transcoded to 8-bit RGBA (mip level 0 only; the GPU mipmap generation
// path rebuilds the chain). Used by Image_loader, which routes to this
// class on the KTX2 magic; same open/load contract: the buffer passed to
// open() must stay alive until load() has been called.
class Image_loader_ktx2
{
public:
    Image_loader_ktx2 ();
    ~Image_loader_ktx2() noexcept;
    Image_loader_ktx2 (const Image_loader_ktx2&) = delete;
    auto operator=    (const Image_loader_ktx2&) = delete;
    Image_loader_ktx2 (Image_loader_ktx2&&)      = delete;
    auto operator=    (Image_loader_ktx2&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info, bool linear) -> bool;
    [[nodiscard]] auto open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, bool linear) -> bool;
    [[nodiscard]] auto load(std::span<std::uint8_t> transfer_buffer) -> bool;
    void close();

    // True when the buffer starts with the KTX2 container identifier.
    [[nodiscard]] static auto is_ktx2(const std::span<const std::uint8_t>& buffer_view) -> bool;

private:
    std::unique_ptr<Image_loader_ktx2_impl_state> m_state;
};

} // namespace erhe::graphics
