#include "pixel_lookup.hpp"
#include "texture_util.hpp"
#include "log.hpp"

#include <bitset>
#include <limits>
#include <map>

namespace hextiles
{

using tile_mask_t = std::bitset<Tile_shape::full_size>;

//tile_mask_t get_mask_bits();

auto get_mask_bits() -> tile_mask_t
{
    const auto mask = load_png("res/hextiles/mask.png");

    tile_mask_t res;
    const std::byte* const mask_pixels = mask.data.data();
    if (mask_pixels == nullptr)
    {
#if defined(ERHE_PNG_LIBRARY_NONE)
        log_pixel_lookup->error("Unable to load image due to ERHE_PNG_LIBRARY_NONE build configuration. Exiting program.");
#elif !defined(ERHE_PNG_LIBRARY_MANGO)
        log_pixel_lookup->error("Unable to load image, check ERHE_PNG_LIBRARY build configuration. Exiting program.");
#else
        log_pixel_lookup->error("Unable to load image, check program working directory. Exiting program.");
#endif
        std::abort();
    }
    for (size_t y = 0; y < Tile_shape::height; ++y)
    {
        for (size_t x = 0; x < Tile_shape::full_width; ++x)
        {
            const std::byte r         = mask_pixels[y * mask.info.row_stride + x * 4];
            const size_t    bit_index = x + y * Tile_shape::full_width;
            res.set(bit_index, r != std::byte{0u});
        }
    }

    return res;
}

Pixel_lookup::Pixel_lookup()
{
    constexpr static coordinate_t unset_coordinate = std::numeric_limits<coordinate_t>::max();

    for (pixel_t lx = 0; lx < lut_width; ++lx)
    {
        for (pixel_t ly = 0; ly < lut_height; ++ly)
        {
            const int lut_index = (int)lx + (int)ly * lut_width;
            m_lut[static_cast<int>(lut_index)] = Tile_coordinate{
                unset_coordinate,
                unset_coordinate
            };
        }
    }

    const tile_mask_t mask_bits = get_mask_bits();
    for (coordinate_t tx = -1; tx < 2; ++tx)
    {
        const pixel_t x0       = static_cast<pixel_t>(tx * Tile_shape::interleave_width);
        const pixel_t y_offset = is_odd(tx) ? Tile_shape::odd_y_offset : 0;
        for (coordinate_t ty = -1; ty < 2; ++ty)
        {
            const pixel_t y0 = static_cast<pixel_t>(ty * Tile_shape::height + y_offset);
            bool any_set{false};
            // (x0, y0) is tile center
            for (pixel_t mx = 0; mx < Tile_shape::full_width; ++mx)
            {
                for (pixel_t my = 0; my < Tile_shape::height; ++my)
                {
                    const pixel_t lx = x0 + mx; //+ Tile_shape::center_x + 1 - lut_width;
                    const pixel_t ly = y0 + my; //+ Tile_shape::center_y - lut_height;
                    if (
                        (lx >= 0) &&
                        (ly >= 0) &&
                        (lx < lut_width) &&
                        (ly < lut_height)
                    )
                    {
                        any_set = true;
                        const int  mask_bit_index = (int)mx + (int)my * (int)Tile_shape::full_width;
                        const int  lut_index      = (int)lx + (int)ly * (int)lut_width;
                        const bool mask           = mask_bits[mask_bit_index];
                        if (mask)
                        {
                            const Tile_coordinate old = m_lut[lut_index];
                            m_lut[lut_index] = Tile_coordinate{tx, ty};
                            if (
                                (old.x != unset_coordinate) ||
                                (old.y != unset_coordinate)
                            )
                            {
                                log_pixel_lookup->error("logic error");
                                std::abort();
                            }
                        }
                    }
                }
            }
            if (!any_set)
            {
                log_pixel_lookup->error("lut: nothing set for tile {}, {}", tx, ty);
            }
        }
    }

    log_pixel_lookup->info("--- lut:");
    std::stringstream ss;
    const char* glyphs = ":.*#+";
    size_t used_glyphs{0};
    std::map<Tile_coordinate, char> tiles;
    //for (pixel_t ly_ = 0; ly_ < lut_height; ++ly_)
    for (pixel_t ly = 0; ly < lut_height; ++ly)
    {
        //pixel_t ly = lut_height - 1 - ly_;
        ss << fmt::format("{:2}: ", ly);
        for (pixel_t lx = 0; lx < lut_width; ++lx)
        {
            const int             lut_index = (int)lx + (int)ly * lut_width;
            const Tile_coordinate value     = m_lut[lut_index];
            char glyph = '?';
            if (
                (value.x == unset_coordinate) ||
                (value.y == unset_coordinate)
            )
            {
                glyph = '!';
                //log_pixel_lookup.error("bad\n");
                //std::abort();
            }
            else
            {
                const auto i = tiles.find(value);
                if (i != tiles.end())
                {
                    glyph = i->second;
                }
                else
                {
                    glyph = glyphs[used_glyphs++];
                    tiles[value] = glyph;
                }
            }
            ss << glyph;
        }
        ss << "\n";
    }
    ss << "---\n";
    for (auto& i : tiles)
    {
        ss << fmt::format("{} = {}, {}\n", i.second, i.first.x, i.first.y);
    }
    ss << "---";
    log_pixel_lookup->info("{}", ss.str());
}

template <typename T> int mod(T a, T b)
{
    T r = a % b;
    return r < 0 ? r + b : r;
}

auto Pixel_lookup::lut_xy(Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate
{
    pixel_coordinate.x += Tile_shape::center_x;
    pixel_coordinate.y += Tile_shape::center_y;
    const int lx = (int)mod(pixel_coordinate.x, lut_width);
    const int ly = (int)mod(pixel_coordinate.y, lut_height);
    return Pixel_coordinate{static_cast<coordinate_t>(lx), static_cast<coordinate_t>(ly)};
}

auto Pixel_lookup::lut_value(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate
{
    pixel_coordinate.x += Tile_shape::center_x;
    pixel_coordinate.y += Tile_shape::center_y - 1;
    const int lx        = (int)mod(pixel_coordinate.x, lut_width);
    const int ly        = (int)mod(pixel_coordinate.y, lut_height);
    const int lut_index = lx + ly * lut_width;
    return m_lut[lut_index];
}

auto Pixel_lookup::pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate
{
    pixel_coordinate.x += Tile_shape::center_x;
    pixel_coordinate.y += Tile_shape::center_y - 1;
    const auto tx        = static_cast<coordinate_t>(std::floor(static_cast<float>(pixel_coordinate.x) / static_cast<float>(lut_width)));
    const auto ty        = static_cast<coordinate_t>(std::floor(static_cast<float>(pixel_coordinate.y) / static_cast<float>(lut_height)));
    const int  lx        = (int)mod(pixel_coordinate.x, lut_width);
    const int  ly        = (int)mod(pixel_coordinate.y, lut_height);
    const int  lut_index = lx + ly * lut_width;
    const Tile_coordinate tile_offset = m_lut[lut_index];
    return Tile_coordinate{
        tx * 2,
        ty
    } + tile_offset;
}

} // namespace hextiles
