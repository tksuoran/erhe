#pragma once

#include "erhe/toolkit/verify.hpp"

#include <gsl/span>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace erhe::ui
{

class Bitmap final
{
public:
    using value_t     = uint8_t;
    using component_t = int;

    Bitmap(int width, int height, component_t components)
        : m_width     {width}
        , m_height    {height}
        , m_components{components}
    {
        if ((width < 0) || (height < 0) || (components < 1) || (components > 4))
        {
            FATAL("bad dimension");
        }

        m_stride = m_width * m_components;

        const size_t byte_count = static_cast<size_t>(m_stride) * static_cast<size_t>(m_height);
        m_data.resize(byte_count);
        fill(0);
    }

    ~Bitmap() = default;

    auto width() const noexcept
    -> int
    {
        return m_width;
    }

    auto height() const noexcept
    -> int
    {
        return m_height;
    }

    auto components() const noexcept
    -> component_t
    {
        return m_components;
    }

    void fill(value_t value)
    {
        for (int y = 0; y < height(); ++y)
        {
            for (int x = 0; x < width(); ++x)
            {
                for (component_t c = 0; c < m_components; ++c)
                {
                    put(x, y, c, value);
                }
            }
        }
    }

    // Applies premultiplication
    // TODO(tksuoran@gmail.com): Gamma
    void post_process(Bitmap& destination, float gamma)
    {
        static_cast<void>(gamma);
        for (int y = 0; y < height(); ++y)
        {
            for (int x = 0; x < width(); ++x)
            {
                const auto ic      = get(x, y, 0);
                const auto oc      = get(x, y, 1);
                const auto inside  = static_cast<float>(ic) / 255.0f;
                const auto outside = static_cast<float>(oc) / 255.0f;
                const auto max     = std::max(inside, outside);
                const auto alpha   = max;
                const auto color   = inside * alpha; // premultiplied
                const auto cc      = static_cast<uint8_t>(color * 255.0f);
                const auto ac      = static_cast<uint8_t>(alpha * 255.0f);
                destination.put(x, y, 0, cc);
                destination.put(x, y, 1, ac);
            }
        }
    }

    void put(int x, int y, component_t c, value_t value)
    {
        if ((x < 0) ||
            (y < 0) ||
            (c < 0) ||
            (x >= m_width) ||
            (y >= m_height) ||
            (c >= m_components))
        {
            FATAL("invalid index");
        }

        const size_t offset = static_cast<size_t>(x) * static_cast<size_t>(m_components) +
                              static_cast<size_t>(y) * static_cast<size_t>(m_stride) +
                              static_cast<size_t>(c);
        m_data[offset] = value;
    }

    auto get(int x, int y, component_t c) const
    -> value_t
    {
        if ((x < 0) ||
            (y < 0) ||
            (c < 0) ||
            (x >= m_width) ||
            (y >= m_height) ||
            (c >= m_components))
        {
            FATAL("invalid index");
        }

        const size_t offset = static_cast<size_t>((x * m_components) + (y * m_stride) + c);
        return m_data[offset];
    }

    template <bool Max>
    void blit(Bitmap*     src,
              int         src_x,
              int         src_y,
              int         width,
              int         height,
              int         dst_x,
              int         dst_y,
              component_t src_components,
              component_t dst_component_offset)
    {
        if ((src == nullptr)                 ||
            (width  < 0)                     ||
            (height < 0)                     ||
            (src_x >= src->width())          ||
            (src_y >= src->height())         ||
            (src_x + width  > src->width())  ||
            (src_y + height > src->height()) ||
            (dst_x >= this->width())         ||
            (dst_y >= this->height())        ||
            (dst_x + width  > this->width()) ||
            (dst_y + height > this->height()))
        {
            FATAL("invalid input");
        }

        for (int iy = 0; iy < height; ++iy)
        {
            const int ly = dst_y + iy;
            if (ly < 0)
            {
                continue;
            }
            if (ly >= this->height())
            {
                continue;
            }
            const int dst_y_ = ly;
            for (int ix = 0; ix < width; ++ix)
            {
                const int lx = dst_x + ix;
                if (lx < 0)
                {
                    continue;
                }
                if (lx >= this->width())
                {
                    continue;
                }
                const int dst_x_ = lx;

                for (component_t c = 0; c < src_components; ++c)
                {
                    auto value = src->get(src_x + ix, src_y + iy, c);
                    if (Max)
                    {
                        value = std::max(value, get(dst_x_, dst_y_, c + dst_component_offset));
                    }
                    put(dst_x_, dst_y_, c + dst_component_offset, value);
                }
            }
        }
    }

    template <bool Max>
    void blit(int                         width,
              int                         height,
              int                         dst_x,
              int                         dst_y,
              const std::vector<value_t>& src_buffer,
              int                         src_pitch,
              int                         src_byte_width,
              int                         src_components,
              int                         dst_component_offset,
              bool                        rotated)
    {
        if (rotated)
        {
            blit_rotated<Max>(width, height, dst_x, dst_y, src_buffer, src_pitch, src_byte_width, src_components, dst_component_offset);
        }
        else
        {
            blit<Max>(width, height, dst_x, dst_y, src_buffer, src_pitch, src_byte_width, src_components, dst_component_offset);
        }
    }

    void dump_data()
    {
        size_t counter{0};
        for (component_t c = 0; c < m_components; ++c)
        {
            for (int iy = 0; iy < m_height; ++iy)
            {
                for (int ix = 0; ix < m_width; ++ix)
                {
                    const auto value = get(ix, iy, c);
                    fmt::print("{}, ", value);
                    ++counter;
                    if (counter > 15u)
                    {
                        fmt::print("\n");
                        counter = 0;
                    }
                }
            }
        }
    }

    void load_data(gsl::span<value_t> source)
    {
        size_t read_offset{0};
        for (component_t c = 0; c < m_components; ++c)
        {
            for (int iy = 0; iy < m_height; ++iy)
            {
                for (int ix = 0; ix < m_width; ++ix)
                {
                    put(ix, iy, c, source[read_offset]);
                    ++read_offset;
                }
            }
        }
    }

    void dump()
    {
#if 0
        const char* shades = " -+#";
        for (component_t c = 0; c < m_components; ++c)
        {
            fmt::print("\nbitmap dump w = {} h = {} c = {}\n", m_width, m_height, c);
            for (int iy = 0; iy < m_height; ++iy)
            {
                for (int ix = 0; ix < m_width; ++ix)
                {
                    auto value = get(ix, iy, c);
                    fmt::print("{}", shades[value / 64]);
                }
                fmt::print("\n");
            }
        }
#endif
        constexpr const char* shades = " /\\X???????";
        fmt::print("\nbitmap dump w = {} h = {}\n", m_width, m_height);
        fmt::print("|");
        for (int ix = 0; ix < m_width; ++ix)
        {
            fmt::print("-");
        }
        fmt::print("|\n");
        for (int iy = m_height - 1; iy >= 0; --iy)
        {
            fmt::print("|");
            for (int ix = 0; ix < m_width; ++ix)
            {
                unsigned int bits{0};
                for (component_t c = 0; c < m_components; ++c)
                {
                    auto value = get(ix, iy, c);
                    if (value > 127) {
                        bits |= 1 << (c);
                    }
                }
                fmt::print("{}", shades[bits]);
            }
            fmt::print("|\n");
        }
        fmt::print("|");
        for (int ix = 0; ix < m_width; ++ix)
        {
            fmt::print("-");
        }
        fmt::print("|\n");
    }

    template <bool Max>
    void blit(int                         width,
              int                         height,
              int                         dst_x,
              int                         dst_y,
              const std::vector<value_t>& src_buffer,
              int                         src_pitch,
              int                         src_byte_width,
              component_t                 src_components,
              component_t                 dst_component_offset)
    {
        if ((width < 0) ||
            (height < 0) ||
            (src_components < 0))
        {
            FATAL("invalid index");
        }

        // fmt::print("blit: w:{} h:{} dx:{} dy:{} sp: {} sbw: {} sc:{} dco: {}\n",
        //            width, height, dst_x, dst_y, src_pitch, src_byte_width,
        //            src_components, dst_component_offset);

        const int dst_height{height};
        const int dst_width{width};

        // char* shades = " -+#";

        for (int iy = 0; iy < dst_height; ++iy)
        {
            const int ly = dst_y + iy;
            if (ly < 0)
            {
                continue;
            }
            if (ly >= this->height())
            {
                continue;
            }
            const int dst_y_ = ly;
            for (int ix = 0; ix < dst_width; ++ix)
            {
                const int lx = dst_x + ix;
                if (lx < 0)
                {
                    continue;
                }
                if (lx >= this->width())
                {
                    continue;
                }
                const int dst_x_ = lx;

                //unsigned int src_x = ix;
                const int src_y_ = height - 1 - iy;

                for (component_t c = 0; c < src_components; ++c)
                {
                    const int     src_byte_x = (ix * src_components) + c;
                    uint8_t value{0};
                    size_t  offset{0};
                    if (src_byte_x < src_byte_width)
                    {
                        offset = static_cast<size_t>(src_byte_x + (src_y_ * src_pitch));
                        value  = src_buffer[offset];
                    }
                    else
                    {
                        continue; // TODO(tksuoran@gmail.com) Or value = 0; ?
                    }
                    //if (value > 128)
                    //put(dst_x_, dst_y_, c + dst_component_offset, value);
                    if (Max)
                    {
                        value = std::max(value, get(dst_x_, dst_y_, c + dst_component_offset));
                    }
                    put(dst_x_, dst_y_, c + dst_component_offset, value);
                    //fmt::print("%c", shades[value / 64]);
                }
            }
            //fmt::print("\n");
        }
        //fmt::print("\nend of blit\n");
    }

    template <bool Max>
    void blit_rotated(int                         width,
                      int                         height,
                      int                         dst_x,
                      int                         dst_y,
                      const std::vector<value_t>& src_buffer,
                      int                         src_pitch,
                      int                         src_byte_width,
                      component_t                 src_components,
                      component_t                 dst_component_offset)
    {
        if ((width < 0) ||
            (height < 0) ||
            (src_components < 0))
        {
            FATAL("invalid index");
        }

        const int dst_width {height};
        const int dst_height{width};

        // fmt::print("blit_rotated: w:{} h:{} dx:{} dy:{} sp: {} sbw: {} sc:{} dco: {}\n",
        //            width, height, dst_x, dst_y, src_pitch, src_byte_width,
        //            src_components, dst_component_offset );

        // char* shades = " -+#";

        for (int iy = 0; iy < dst_height; ++iy)
        {
            const int ly = dst_y + iy;
            if (ly < 0)
            {
                continue;
            }
            if (ly >= this->height())
            {
                continue;
            }
            const int dst_y_ = ly;
            for (int ix = 0; ix < dst_width; ++ix)
            {
                const int lx = dst_x + ix;
                if (lx < 0)
                {
                    continue;
                }
                if (lx >= this->width())
                {
                    continue;
                }
                const int dst_x_ = lx;

                const int src_x = iy;
                const int src_y = /* height - 1 - */ ix;

                for (component_t c = 0; c < src_components; ++c)
                {
                    const int src_byte_x = (src_x * src_components) + c;
                    uint8_t   value {0};
                    size_t    offset{0};
                    if (src_byte_x < src_byte_width)
                    {
                        offset = src_byte_x + (src_y * src_pitch);
                        value  = src_buffer[offset];
                    }
                    else
                    {
                        continue; // TODO(tksuoran@gmail.com) Or value = 0; ?
                    }
                    // fmt::print("%c", shades[value / 64]);
                    if (Max)
                    {
                        value = std::max(value, get(dst_x_, dst_y_, c + dst_component_offset));
                    }
                    put(dst_x_, dst_y_, c + dst_component_offset, value);
                }
            }
            // fmt::print("\n");
        }
        // fmt::print("\nend of blit\n");
    }

    auto as_span()
    -> gsl::span<std::byte>
    {
        return gsl::span<std::byte>(reinterpret_cast<std::byte*>(&m_data[0]),
                                    m_data.size() * sizeof(value_t));
    }

private:
    int                  m_width     {0};
    int                  m_height    {0};
    component_t          m_components{0};
    int                  m_stride    {0};
    std::vector<value_t> m_data;
};

} // namespace erhe::ui
