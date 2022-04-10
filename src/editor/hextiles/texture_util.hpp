#include "erhe/graphics/png_loader.hpp"

namespace erhe::graphics
{
    class Texture;
}

namespace hextiles
{

class Image
{
public:
    erhe::graphics::Image_info info;
    std::vector<std::byte>     data;
};

auto load_png    (const fs::path& path) -> Image;
auto load_texture(const fs::path& path) -> std::shared_ptr<erhe::graphics::Texture>;


#if 0
struct SDL_Surface;

void mask_tile(
    const int          tile_x,
    const int          tile_y,
    SDL_Surface* const mask,
    SDL_Surface* const surface
);

void color_mask_tile(
    unsigned char r0,
    unsigned char g0,
    unsigned char b0,
    int           tile_x,
    int           tile_y,
    SDL_Surface* surface
);

void apply_mask();
#endif

}
