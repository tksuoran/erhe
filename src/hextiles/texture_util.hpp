#pragma once

#include "erhe_graphics/image_loader.hpp"

#include <glm/glm.hpp>

#include <vector>

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

}
