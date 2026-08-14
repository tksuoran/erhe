#pragma once

// Image_info lives in image_loader_wuffs.hpp; including it directly (not
// through image_loader.hpp) keeps this header out of a cycle - the wuffs
// header forward-declares Image_loader_dds for its routing member.
#include "erhe_graphics/image_loader_wuffs.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace erhe::graphics {

class Image_loader_dds_impl_state;

// Decodes DDS containers holding block-compressed (BC1..BC7) or plain RGBA
// data, as referenced by glTF MSFT_texture_dds - e.g. the niagara_bistro
// BC7 textures. The full mip chain is exposed: Image_info::level_count is
// the file's mip count and load() writes all levels contiguously,
// largest-first, tightly packed. The color space comes from the DXGI format
// in the file; the linear hint is ignored. Used by Image_loader, which
// routes to this class on the 'DDS ' magic; same open/load contract: the
// buffer passed to open() must stay alive until load() has been called.
class Image_loader_dds
{
public:
    Image_loader_dds ();
    ~Image_loader_dds() noexcept;
    Image_loader_dds (const Image_loader_dds&) = delete;
    auto operator=   (const Image_loader_dds&) = delete;
    Image_loader_dds (Image_loader_dds&&)      = delete;
    auto operator=   (Image_loader_dds&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info, bool linear) -> bool;
    [[nodiscard]] auto open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, bool linear) -> bool;
    [[nodiscard]] auto load(std::span<std::uint8_t> transfer_buffer) -> bool;
    void close();

    // True when the buffer starts with the DDS magic 'DDS '.
    [[nodiscard]] static auto is_dds(const std::span<const std::uint8_t>& buffer_view) -> bool;

private:
    std::unique_ptr<Image_loader_dds_impl_state> m_state;
};

} // namespace erhe::graphics
