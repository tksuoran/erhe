#pragma once

#include "erhe/graphics/png_loader.hpp"
#include "erhe/gl/wrapper_enums.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics
{
    class Texture;
}

namespace hextiles
{

class Image
{
public:
    [[nodiscard]] auto get_pixel(size_t x, size_t y) const -> glm::vec4;
    void put_pixel(size_t x, size_t y, glm::vec4 color);

    erhe::graphics::Image_info info;
    std::vector<std::byte>     data;
};

auto to_gl(erhe::graphics::Image_format format) -> gl::Internal_format;

auto load_png    (const fs::path& path) -> Image;
auto load_texture(const fs::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

}
