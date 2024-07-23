#include "texture_util.hpp"
#include "hextiles_log.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_file/file.hpp"
#include "erhe_verify/verify.hpp"

namespace hextiles
{

auto to_gl(const erhe::graphics::Image_format format) -> gl::Internal_format
{
    switch (format) {
        //using enum erhe::graphics::Image_format;
        case erhe::graphics::Image_format::srgb8:        return gl::Internal_format::srgb;
        case erhe::graphics::Image_format::srgb8_alpha8: return gl::Internal_format::srgb8_alpha8;
        default: {
            ERHE_FATAL("Bad image format %04x", static_cast<unsigned int>(format));
        }
    }
    // std::unreachable() return gl::Internal_format::rgba8;
}

namespace
{

auto pixel_size(const erhe::graphics::Image_format format) -> size_t
{
    switch (format) {
        case erhe::graphics::Image_format::srgb8:        return 3;
        case erhe::graphics::Image_format::srgb8_alpha8: return 4;
        default: return 0;
    }
}

auto get_buffer_size(const erhe::graphics::Image_info& info) -> size_t
{
    ERHE_VERIFY(info.width >= 1);
    ERHE_VERIFY(info.height >= 1);

    return static_cast<size_t>(info.width) * static_cast<size_t>(info.height) * pixel_size(info.format);
}

} // anonymous namespce

auto Image::get_pixel(size_t x, size_t y) const -> glm::vec4
{
    ERHE_VERIFY(x < info.width);
    ERHE_VERIFY(y < info.height);
    const size_t       index = y * info.row_stride + x * 4;
    const std::uint8_t r_i   = static_cast<uint8_t>(data[index]);
    const std::uint8_t g_i   = static_cast<uint8_t>(data[index + 1]);
    const std::uint8_t b_i   = static_cast<uint8_t>(data[index + 2]);
    const std::uint8_t a_i   = static_cast<uint8_t>(data[index + 3]);
    const float        r     = static_cast<float>(r_i) / 255.0f;
    const float        g     = static_cast<float>(g_i) / 255.0f;
    const float        b     = static_cast<float>(b_i) / 255.0f;
    const float        a     = static_cast<float>(a_i) / 255.0f;
    return glm::vec4{r, g, b, a};
}

void Image::put_pixel(size_t x, size_t y, glm::vec4 color)
{
    ERHE_VERIFY(x < info.width);
    ERHE_VERIFY(y < info.height);
    const size_t index = y * info.row_stride + x * 4;
    data[index    ] = static_cast<uint8_t>(color.r * 255.0f);
    data[index + 1] = static_cast<uint8_t>(color.g * 255.0f);
    data[index + 2] = static_cast<uint8_t>(color.b * 255.0f);
    data[index + 3] = static_cast<uint8_t>(color.a * 255.0f);
}

auto load_png(const std::filesystem::path& path) -> Image
{
    const bool is_ok = erhe::file::check_is_existing_non_empty_regular_file("load_png", path);
    if (!is_ok) {
        return {};
    }

    Image image;
    erhe::graphics::Image_loader loader;

    if (!loader.open(path, image.info)) {
        log_image->error("Unable to load image '{}'. Exiting program.", path.string());
        std::abort();
    }

    const auto byte_count = get_buffer_size(image.info);
    image.data.resize(byte_count);

    const bool ok = loader.load(image.data);
    loader.close();
    if (!ok) {
        log_image->error("File PNG load error {}", path.string());
        return {};
    }
    return image;
}

auto load_texture(erhe::graphics::Instance& graphics_instance, const std::filesystem::path& path) -> std::shared_ptr<erhe::graphics::Texture>
{
    const Image image = load_png(path);
    if (image.data.size() == 0) {
        log_image->error("Image empty {}", path.string());
        return {};
    }
    erhe::graphics::Texture_create_info texture_create_info{
        .instance        = graphics_instance,
        .internal_format = to_gl(image.info.format),
        .use_mipmaps     = (image.info.level_count > 1),
        .width           = image.info.width,
        .height          = image.info.height,
        .depth           = image.info.depth,
        .level_count     = image.info.level_count,
        .row_stride      = image.info.row_stride,
        .debug_label     = path.string()
    };

    auto texture = std::make_shared<erhe::graphics::Texture>(texture_create_info);
    texture->set_debug_label(path.string());
    texture->upload(
        texture_create_info.internal_format,
        image.data,
        texture_create_info.width,
        texture_create_info.height
    );

    return texture;
}

#if 0
void mask_tile(
    const int          tile_x,
    const int          tile_y,
    SDL_Surface* const mask,
    SDL_Surface* const surface
)
{
    auto mask_pixels = reinterpret_cast<uint8_t*>(mask->pixels);
    auto pixels      = reinterpret_cast<uint8_t*>(surface->pixels);
    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_full_width; ++x) {
            auto r = mask_pixels[y * mask->pitch + x * 4 + 3];
            if (r == 0U) {
                int px = (tile_x * tile_full_width) + x;
                int py = (tile_y * tile_height) + y;
                pixels[py * surface->pitch + px * 4 + 0] = 0U;
            }
        }
    }
}

void color_mask_tile(
    uint8_t      r0,
    uint8_t      g0,
    uint8_t      b0,
    int          tile_x,
    int          tile_y,
    SDL_Surface* surface
)
{
    auto pixels = reinterpret_cast<uint8_t*>(surface->pixels);
    for (int y = 0; y < tile_height; ++y) {
        int py = (tile_y * tile_height) + y;
        for (int x = 0; x < tile_full_width; ++x) {
            int px = (tile_x * tile_full_width) + x;
            uint8_t b = pixels[py * surface->pitch + px * 4 + 1];
            uint8_t g = pixels[py * surface->pitch + px * 4 + 2];
            uint8_t r = pixels[py * surface->pitch + px * 4 + 3];
            if ((r == r0) && (g == g0) && (b == b0)) {
                pixels[py * surface->pitch + px * 4 + 0] = 0U;
            }
        }
    }
}

void apply_mask()
{
    SDL_Surface *const mask_in = IMG_Load("data/graphics/hexmask.png");
    if (mask_in == nullptr) {
        const char *const error_message = SDL_GetError();
        fmt::print(stderr, "IMG_Load() failed: {}", (error_message != nullptr) ? error_message : "");
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }
    SDL_Surface *const surface_in = IMG_Load("data/graphics/hex.png");
    if (surface_in == nullptr) {
        const char *const error_message = SDL_GetError();
        fmt::print(stderr, "IMG_Load() failed: {}", (error_message != nullptr) ? error_message : "");
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }

    SDL_Surface *const mask = SDL_ConvertSurfaceFormat(mask_in, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_Surface *const surface_out = SDL_ConvertSurfaceFormat(surface_in, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_LockSurface(mask);
    SDL_LockSurface(surface_out);

    for (int ty = 0; ty < group_count * group_height; ++ty) {
        for (int tx = 0; tx < group_width; ++tx) {
            mask_tile(tx, ty, mask, surface_out);
        }
    }

    for (int ty = 0; ty < basetiles_height + 1; ++ty) {
        for (int tx = 0; tx < basetiles_width; ++tx) {
            mask_tile(tx, 1 + ty + group_count * group_height, mask, surface_out);
        }
    }
    //mask_tile(0, 1 + basetiles_height + 1 + group_count * group_height, mask, surface_out);
    // 1 / 2 extra row above - using color mask
    for (int tx = 0; tx < 6; ++tx) {
        color_mask_tile(64U, 64U, 80U, tx, group_count * group_height, surface_out);
    }
    // One extra row below - using color mask
    for (int tx = 0; tx < basetiles_width - 1; ++tx) {
        color_mask_tile(64U, 64U, 80U, tx, group_count * group_height + basetiles_height + 2, surface_out);
    }
    mask_tile(basetiles_width - 1, group_count * group_height + basetiles_height + 2, mask, surface_out);

    SDL_UnlockSurface(mask);
    SDL_UnlockSurface(surface_out);

    if (IMG_SavePNG(surface_out, "data/graphics/hex_.png") < 0) {
        const char *const error_message = SDL_GetError();
        fmt::print(stderr, "IMG_SavePNG() failed: {}", (error_message != nullptr) ? error_message : "");
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }
    SDL_FreeSurface(mask_in);
    SDL_FreeSurface(mask);
    SDL_FreeSurface(surface_in);
    SDL_FreeSurface(surface_out);

    SDL_Surface *const unit_in = IMG_Load("data/graphics/unit.png");
    if (mask_in == nullptr) {
        const char *const error_message = SDL_GetError();
        fmt::print(stderr, "IMG_Load() failed: {}", (error_message != nullptr) ? error_message : "");
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }

    SDL_Surface *const unit = SDL_ConvertSurfaceFormat(unit_in, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_LockSurface(unit);

    for (int ty = 0; ty < unit_group_height; ++ty) {
        for (int tx = 0; tx < unit_group_width; ++tx) {
            color_mask_tile(0x30U, 0x30U, 0x60U, tx, ty, unit);
        }
    }
    if (IMG_SavePNG(unit, "data/graphics/unit_.png") < 0) {
        const char *error_message = SDL_GetError();
        fmt::print(stderr, "IMG_SavePNG() failed: {}", (error_message != nullptr) ? error_message : "");
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }

    SDL_UnlockSurface(unit);
    SDL_FreeSurface(unit_in);
    SDL_FreeSurface(unit);
}

auto get_mask_bits() -> tile_mask_t
{
    SDL_Surface *const mask_in = IMG_Load("data/graphics/hexmask.png");
    if (mask_in == nullptr) {
        const char *const error_message = SDL_GetError();
        fmt::print(stderr, "IMG_Load() failed: {}", (error_message != nullptr) ? error_message : "");
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }
    SDL_Surface *const mask = SDL_ConvertSurfaceFormat(mask_in, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(mask_in);
    SDL_LockSurface(mask);

    tile_mask_t res;
    unsigned char *const mask_pixels = (unsigned char*)(mask->pixels);
    for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_full_width; ++x) {
            unsigned char r = mask_pixels[y * mask->pitch + x * 4 + 3];
            size_t bit_index = x + y * tile_full_width;
            res.set(bit_index, r != 0U);
        }
    }

    SDL_UnlockSurface(mask);
    SDL_FreeSurface(mask);

    return res;
}
#endif

} // namespace hextiles
