#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

[[nodiscard]] auto to_mtl_pixel_format(erhe::dataformat::Format format) -> MTL::PixelFormat;
[[nodiscard]] auto to_mtl_texture_type(Texture_type type, int sample_count, int array_layer_count) -> MTL::TextureType;

} // namespace erhe::graphics
