#pragma once

#include "erhe_graphics/texture.hpp"
#include "erhe_ui/bitmap.hpp"
#include "erhe_ui/rectangle.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct FT_LibraryRec_;
struct FT_FaceRec_;
struct hb_font_t;
struct hb_buffer_t;

namespace erhe::ui {

class Font final
{
public:
    Font(
        erhe::graphics::Instance&    graphics_instance,
        const std::filesystem::path& path,
        unsigned int                 size,
        float                        outline_thickness = 0.0f
    );

    ~Font() noexcept;

    [[nodiscard]] auto line_height() const -> float
    {
        return m_line_height;
    }

    auto print(
        std::span<uint32_t> uint_data,
        std::string_view    text,
        glm::vec3           text_position,
        const uint32_t      text_color,
        Rectangle&          out_bounds
    ) const -> size_t;

    auto get_glyph_count(const std::string_view text) const -> size_t;

    auto measure(const std::string_view text) const -> Rectangle;

    [[nodiscard]] auto texture() const -> erhe::graphics::Texture*
    {
        ERHE_VERIFY(m_texture);

        return m_texture.get();
    }

    [[nodiscard]] auto hinting() const -> bool
    {
        return m_hinting;
    }

    [[nodiscard]] auto dpi() const -> unsigned int
    {
        return m_dpi;
    }

    [[nodiscard]] auto gamma() const -> float
    {
        return m_gamma;
    }

    [[nodiscard]] auto saturation() const -> float
    {
        return m_saturation;
    }

    [[nodiscard]] auto chars() const -> const std::string&
    {
        return m_chars;
    }

    [[nodiscard]] auto pixel_size() const -> unsigned int
    {
        return m_pixel_size;
    }

    [[nodiscard]] auto bolding() const -> float
    {
        return m_bolding;
    }

    [[nodiscard]] auto outline_thickness() const -> float
    {
        return m_outline_thickness;
    }

    void set_hinting(bool value)
    {
        m_hinting = value;
    }

    void set_dpi(unsigned int value)
    {
        m_dpi = value;
    }

    void set_gamma(float value)
    {
        m_gamma = value;
    }

    void set_saturation(float value)
    {
        m_saturation = value;
    }

    void set_chars(const std::string& value)
    {
        m_chars = value;
    }

    void set_pixel_size(unsigned int value)
    {
        m_pixel_size = value;
    }

    void set_bolding(float value)
    {
        m_bolding = value;
    }

    void set_outline_thickness(float value)
    {
        m_outline_thickness = value;
    }

    auto render() -> bool;

    void post_process();

    void trace_info() const;

private:

    struct ft_char
    {
        int width    {0};
        int height   {0};
        int g_left   {0};
        int g_bottom {0};
        int g_top    {0};
        int g_height {0};
        int b_left   {0};
        int b_bottom {0};
        int b_top    {0};
        bool rotated{false};
        std::array<float, 4> u{0.0f, 0.0f, 0.0f, 0.0f};
        std::array<float, 4> v{0.0f, 0.0f, 0.0f, 0.0f};
    };

    erhe::graphics::Instance&  m_graphics_instance;

    std::map<uint32_t, size_t> m_glyph_to_char;

    ft_char               m_chars_256[256];
    std::string           m_chars;
    std::filesystem::path m_path;

    bool         m_hinting          {true};
    unsigned int m_dpi              {96};
    float        m_gamma            {1.0f};
    float        m_saturation       {1.0f};
    unsigned int m_pixel_size       {0};
    float        m_bolding          {0.0f};
    float        m_outline_thickness{0.0f};
    int          m_spacing_delta    {0};
    int          m_hint_mode        {0};
    float        m_line_height      {0.0f};
    int          m_texture_width    {0};
    int          m_texture_height   {0};

    std::unique_ptr<erhe::graphics::Texture> m_texture;
    std::unique_ptr<Bitmap>                  m_bitmap;
#if defined(ERHE_FONT_RASTERIZATION_LIBRARY_FREETYPE)
    struct FT_LibraryRec_*                   m_freetype_library{nullptr};
    struct FT_FaceRec_*                      m_freetype_face{nullptr};
#endif
#if defined(ERHE_TEXT_LAYOUT_LIBRARY_HARFBUZZ)
    hb_font_t*                               m_harfbuzz_font{nullptr};
    hb_buffer_t*                             m_harfbuzz_buffer{nullptr};
#endif
};

} // namespace erhe::ui
