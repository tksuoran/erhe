#pragma once

#include "erhe_graphics/image_loader.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::graphics {
    class Device;
    class Texture;
}

namespace hextiles {

class Image
{
public:
    [[nodiscard]] auto get_pixel(size_t x, size_t y) const -> glm::vec4;
    void put_pixel(size_t x, size_t y, glm::vec4 color);

    erhe::graphics::Image_info info;
    std::vector<std::uint8_t>  data;
};

auto load_png    (const std::filesystem::path& path) -> Image;
auto load_texture(erhe::graphics::Device& graphics_device, const std::filesystem::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

}
