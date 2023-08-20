#include "graphics/icon_rasterization.hpp"
#include "renderers/programs.hpp"

#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_SVG_LIBRARY_LUNASVG)
#   include <lunasvg.h>
#endif

namespace editor {

Icon_rasterization::Icon_rasterization(
    erhe::graphics::Instance&    graphics_instance,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    Programs&                    programs,
    const int                    size,
    const int                    column_count,
    const int                    row_count
)
    : m_imgui_renderer{imgui_renderer}
    , m_icon_width    {size}
    , m_icon_height   {size}
    , m_icon_uv_width {static_cast<float>(1.0) / static_cast<float>(column_count)}
    , m_icon_uv_height{static_cast<float>(1.0) / static_cast<float>(row_count)}
{
    m_texture = std::make_shared<erhe::graphics::Texture>(
        erhe::graphics::Texture_create_info{
            .instance        = graphics_instance,
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba8,
            .use_mipmaps     = true,
            .width           = column_count * m_icon_width,
            .height          = row_count * m_icon_height
        }
    );
    m_texture->set_debug_label("Icon_set");

    m_texture_handle = graphics_instance.get_handle(
        *m_texture.get(),
        programs.linear_sampler
    );
}

void Icon_rasterization::rasterize(
    lunasvg::Document& document,
    const int          column,
    const int          row
)
{
#if defined(ERHE_SVG_LIBRARY_LUNASVG)
    // Render a super sampled icon
    const auto bitmap_ss = document.renderToBitmap(m_icon_width * 4, m_icon_height * 4);

    // Downsample
    const lunasvg::Bitmap bitmap{
        static_cast<uint32_t>(m_icon_width),
        static_cast<uint32_t>(m_icon_height)
    };
    const auto read_stride  = bitmap_ss.stride();
    const auto write_stride = bitmap.stride();
    for (int y = 0; y < m_icon_height; ++y) {
        for (int x = 0; x < m_icon_width; ++x) {
            float data[4] = { 0, 0, 0, 0};
            for (int ys = 0; ys < 4; ++ys) {
                for (int xs = 0; xs < 4; ++xs) {
                    const int offset =
                        (
                            (y * 4 + ys) * read_stride +
                            (x * 4 + xs) * 4
                        );
                    for (int c = 0; c < 4; ++c) {
                        uint8_t v = bitmap_ss.data()[offset + c];
                        data[c] += static_cast<float>(v);
                    }
                }
            }
            bitmap.data()[y * write_stride + 4 * x + 0] = static_cast<uint8_t>(data[0] / 16.0f);
            bitmap.data()[y * write_stride + 4 * x + 1] = static_cast<uint8_t>(data[1] / 16.0f);
            bitmap.data()[y * write_stride + 4 * x + 2] = static_cast<uint8_t>(data[2] / 16.0f);
            bitmap.data()[y * write_stride + 4 * x + 3] = static_cast<uint8_t>(data[3] / 16.0f);
        }
    }

    const int x_offset = column * m_icon_width;
    const int y_offset = row    * m_icon_height;

    const auto span = gsl::span<std::byte>{
        reinterpret_cast<std::byte*>(bitmap.data()),
        static_cast<size_t>(bitmap.stride()) * static_cast<size_t>(bitmap.height())
    };

    m_texture->upload(
        gl::Internal_format::rgba8,
        span,
        bitmap.width(),
        bitmap.height(),
        1,
        0,
        x_offset,
        y_offset,
        0
    );
#else
    static_cast<void>(document);
    static_cast<void>(column);
    static_cast<void>(row);
#endif
}

[[nodiscard]] auto Icon_rasterization::get_size() const -> int
{
    return m_icon_width;
}

auto Icon_rasterization::uv1(const glm::vec2& uv0) const -> glm::vec2
{
    return glm::vec2{
        uv0.x + m_icon_uv_width,
        uv0.y + m_icon_uv_height
    };
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto imvec_from_glm(glm::vec2 v) -> ImVec2
{
    return ImVec2{v.x, v.y};
}
auto imvec_from_glm(glm::vec4 v) -> ImVec4
{
    return ImVec4{v.x, v.y, v.z, v.w};
}
#endif

void Icon_rasterization::icon(
    const glm::vec2 uv0,
    const glm::vec4 tint_color
) const
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
#else
    ERHE_PROFILE_FUNCTION();

    m_imgui_renderer.image(
        m_texture,
        m_icon_width,
        m_icon_height,
        uv0,
        uv1(uv0),
        imvec_from_glm(tint_color),
        false
    );
    ImGui::SameLine();
#endif
}

auto Icon_rasterization::icon_button(
    const uint32_t  id,
    const glm::vec2 uv0,
    const glm::vec4 background_color,
    const glm::vec4 tint_color,
    const bool      linear
) const -> bool
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
    return false;
#else
    ERHE_PROFILE_FUNCTION();

    const bool result = m_imgui_renderer.image_button(
        id,
        m_texture,
        m_icon_width,
        m_icon_height,
        uv0,
        uv1(uv0),
        background_color,
        tint_color,
        linear
    );
    ImGui::SameLine();
    return result;
#endif
}

} // namespace editor
