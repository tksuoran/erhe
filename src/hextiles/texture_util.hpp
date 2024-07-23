#pragma once

#include "erhe_graphics/image_loader.hpp"
#include "erhe_gl/wrapper_enums.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::graphics {
    class Instance;
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

auto to_gl(erhe::graphics::Image_format format) -> gl::Internal_format;

auto load_png    (const std::filesystem::path& path) -> Image;
auto load_texture(
    erhe::graphics::Instance&    graphics_instance,
    const std::filesystem::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

}
