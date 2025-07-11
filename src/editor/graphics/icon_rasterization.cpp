#include "graphics/icon_rasterization.hpp"
#include "graphics/icon_set.hpp"
#include "app_context.hpp"
#include "app_settings.hpp"
#include "editor_log.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_SVG_LIBRARY_PLUTOSVG)
#   include <plutosvg.h>
#endif

namespace editor {

Icon_load_data::Icon_load_data(Icon_settings& icon_settings, const char* icon_name, int column, int row, glm::vec2 uv0)
    : m_column{column}
    , m_row{row}
    , m_uv0{uv0}
{
    const auto path = std::filesystem::path{"res"} / "icons" / icon_name;
    m_document = plutosvg_document_load_from_file(path.string().c_str(), -1, -1);
    if (m_document == nullptr) {
        log_svg->warn("Unable to load {}", path.string());
        return;
    }

    int small  = icon_settings.small_icon_size;
    int large  = icon_settings.large_icon_size;
    int hotbar = icon_settings.hotbar_icon_size;
    m_bitmaps.emplace_back(plutovg_surface_create(small,  small));
    m_bitmaps.emplace_back(plutovg_surface_create(large,  large));
    m_bitmaps.emplace_back(plutovg_surface_create(hotbar, hotbar));
}

Icon_load_data::~Icon_load_data()
{
    for (plutovg_surface_t* bitmap : m_bitmaps) {
        plutovg_surface_destroy(bitmap);
    }
}

void Icon_load_data::rasterize(int size)
{
    if (!m_document) {
        return;
    }

    // Render a super sampled icon
    const char*              id            = nullptr;
    const plutovg_color_t*   current_color = nullptr;
    plutosvg_palette_func_t  palette_func  = nullptr;
    void*                    closure       = nullptr;
    plutovg_surface_t* const bitmap_ss = plutosvg_document_render_to_surface(
        m_document,
        id,
        size * 4,
        size * 4,
        current_color,
        palette_func,
        closure
    );
    if (bitmap_ss == nullptr) {
        log_render->error("Unable to rasterize");
        return;
    }

    // Downsample
    for (plutovg_surface_t* bitmap : m_bitmaps) {
        if (plutovg_surface_get_width(bitmap) != size) {
            continue;
        }
        const int read_stride  = plutovg_surface_get_stride(bitmap_ss);
        const int write_stride = plutovg_surface_get_stride(bitmap);
        const unsigned char* const src_data = plutovg_surface_get_data(bitmap_ss);
        unsigned char* const dst_data = plutovg_surface_get_data(bitmap);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float data[4] = { 0, 0, 0, 0};
                for (int ys = 0; ys < 4; ++ys) {
                    for (int xs = 0; xs < 4; ++xs) {
                        const int offset =
                            (
                                (y * 4 + ys) * read_stride +
                                (x * 4 + xs) * 4
                            );
                        for (int c = 0; c < 4; ++c) {
                            unsigned char v = src_data[offset + c];
                            data[c] += static_cast<float>(v);
                        }
                    }
                }
                // TODO Figure out what is the correct way to render SVG with alpha and ImGui
                dst_data[y * write_stride + 4 * x + 0] = data[0] > 0.0f ? 0xffu : 0x00u;
                dst_data[y * write_stride + 4 * x + 1] = data[1] > 0.0f ? 0xffu : 0x00u;
                dst_data[y * write_stride + 4 * x + 2] = data[2] > 0.0f ? 0xffu : 0x00u;
                dst_data[y * write_stride + 4 * x + 3] = static_cast<uint8_t>(data[3] / 16.0f);
            }
        }
    }

    plutovg_surface_destroy(bitmap_ss);
}

void Icon_load_data::upload(int size, erhe::graphics::Texture& texture)
{
    for (size_t i = 0, end = m_bitmaps.size(); i < end; ++i) {
        plutovg_surface_t* bitmap = m_bitmaps[i];
        if (bitmap == nullptr) {
            continue;
        }
        if (plutovg_surface_get_width(bitmap) != size) {
            continue;
        }
        const int stride   = plutovg_surface_get_stride(bitmap);
        const int x_offset = m_column * size;
        const int y_offset = m_row    * size;

        const auto span = std::span<unsigned char>{
            plutovg_surface_get_data(bitmap),
            static_cast<size_t>(stride) * static_cast<size_t>(size)
        };

        texture.upload(
            erhe::dataformat::Format::format_8_vec4_unorm,
            span,
            size,
            size,
            1, // depth
            0, // array layer
            0, // level
            x_offset,
            y_offset,
            0
        );
        plutovg_surface_destroy(bitmap);
        m_bitmaps[i] = nullptr;
    }
}

Icon_loader::Icon_loader(Icon_settings& icon_settings)
    : m_icon_settings{icon_settings}
{
}

void Icon_loader::queue_icon_load(glm::vec2& uv, const char* icon_name)
{
    ERHE_VERIFY(m_row < Icon_set::s_row_count);

    const float u = static_cast<float>(m_column) / static_cast<float>(Icon_set::s_column_count);
    const float v = static_cast<float>(m_row   ) / static_cast<float>(Icon_set::s_row_count);

    uv = glm::vec2{u, v};
    m_icons_to_load.emplace_back(
        new Icon_load_data(m_icon_settings, icon_name, m_column, m_row, uv)
    );

    ++m_column;
    if (m_column >= Icon_set::s_column_count) {
        m_column = 0;
        ++m_row;
    }
}

void Icon_loader::execute_rasterization_queue()
{
    if (m_rasterization_queue_executed) {
        return;
    }

    for (std::unique_ptr<Icon_load_data>& icon_load_data : m_icons_to_load) {
        icon_load_data->rasterize(m_icon_settings.small_icon_size);
        icon_load_data->rasterize(m_icon_settings.large_icon_size);
        icon_load_data->rasterize(m_icon_settings.hotbar_icon_size);
    }

    m_rasterization_queue_executed = true;
}

void Icon_loader::upload_to_texture(Icon_rasterization& icon_rasterization)
{
    ERHE_VERIFY(m_rasterization_queue_executed);
    int size = icon_rasterization.get_size();
    std::shared_ptr<erhe::graphics::Texture> texture_shared = icon_rasterization.get_texture();
    ERHE_VERIFY(texture_shared);
    erhe::graphics::Texture& texture = *texture_shared.get();
    for (std::unique_ptr<Icon_load_data>& icon_load_data : m_icons_to_load) {
        icon_load_data->upload(size, texture);
    }
}

void Icon_loader::clear_load_queue()
{
    m_icons_to_load.clear();
}

Icon_rasterization::Icon_rasterization(App_context& context, erhe::graphics::Device& graphics_device, const int size)
    : m_context{context}
    , m_texture{
        std::make_shared<erhe::graphics::Texture>(
            graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = graphics_device,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm, // TODO sRGB?
                .use_mipmaps = true,
                .width       = Icon_set::s_column_count * size,
                .height      = Icon_set::s_row_count * size,
                .debug_label = "Icons"
            }
        )
    }
    , m_linear_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Icon_rasterization linear"
        }
    }
    , m_texture_handle{
        graphics_device.get_handle(*m_texture.get(), m_linear_sampler)
    }
    , m_icon_width    {size}
    , m_icon_height   {size}
    , m_icon_uv_width {static_cast<float>(1.0) / static_cast<float>(Icon_set::s_column_count)}
    , m_icon_uv_height{static_cast<float>(1.0) / static_cast<float>(Icon_set::s_row_count)}
{
}

auto Icon_rasterization::get_size() const -> int
{
    return m_icon_width;
}

auto Icon_rasterization::uv1(const glm::vec2& uv0) const -> glm::vec2
{
    return glm::vec2{uv0.x + m_icon_uv_width, uv0.y + m_icon_uv_height};
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

void Icon_rasterization::icon(const glm::vec2 uv0, const glm::vec4 background_color, const glm::vec4 tint_color) const
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
#else
    ERHE_PROFILE_FUNCTION();

    m_context.imgui_renderer->image(
        m_texture,
        m_icon_width,
        m_icon_height,
        uv0,
        uv1(uv0),
        background_color,
        imvec_from_glm(tint_color),
        erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    );
    ImGui::SameLine();
#endif
}

auto Icon_rasterization::icon_button(
    const uint32_t                            id,
    const glm::vec2                           uv0,
    const glm::vec4                           background_color,
    const glm::vec4                           tint_color,
    const erhe::graphics::Filter              filter,
    const erhe::graphics::Sampler_mipmap_mode mipmap_mode
) const -> bool
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
    return false;
#else
    ERHE_PROFILE_FUNCTION();

    const bool result = m_context.imgui_renderer->image_button(
        id, m_texture, m_icon_width, m_icon_height, uv0, uv1(uv0), background_color, tint_color,
        filter, mipmap_mode
    );
    ImGui::SameLine();
    return result;
#endif
}

}
