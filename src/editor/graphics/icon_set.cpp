#include "icon_set.hpp"
#include "log.hpp"

#include "renderers/programs.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_SVG_LIBRARY_LUNASVG)
#   include <lunasvg.h>
#endif

namespace editor {

Icon_set::Icon_set()
    : erhe::components::Component{c_label}
{
}

void Icon_set::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Gl_context_provider>();
    require<Programs>();
}

void Icon_set::initialize_component()
{
    const auto& config = get<erhe::application::Configuration>();
    if (!config->imgui.enabled)
    {
        return;
    }

    m_icon_width  = config->imgui.icon_size;
    m_icon_height = config->imgui.icon_size;

    m_row_count    = 16;
    m_column_count = 16;
    m_row          = 0;
    m_column       = 0;

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    m_texture = std::make_shared<erhe::graphics::Texture>(
        erhe::graphics::Texture_create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba8,
            .use_mipmaps     = true,
            .width           = m_column_count * m_icon_width,
            .height          = m_row_count * m_icon_height
        }
    );
    m_texture->set_debug_label("Icon_set");

    m_texture_handle = erhe::graphics::get_handle(
        *m_texture.get(),
        *get<Programs>()->linear_sampler.get()
    );

    m_icon_uv_width  = static_cast<float>(m_icon_width ) / static_cast<float>(m_texture->width());
    m_icon_uv_height = static_cast<float>(m_icon_height) / static_cast<float>(m_texture->height());

    const auto icon_directory = fs::path("res") / "icons";

    icons.camera            = load(icon_directory / "camera.svg");
    icons.directional_light = load(icon_directory / "directional_light.svg");
    icons.spot_light        = load(icon_directory / "spot_light.svg");
    icons.point_light       = load(icon_directory / "point_light.svg");
    icons.mesh              = load(icon_directory / "mesh.svg");
    icons.node              = load(icon_directory / "node.svg");
    icons.three_dots        = load(icon_directory / "three_dots.svg");
}

auto Icon_set::load(const fs::path& path) -> glm::vec2
{
#if defined(ERHE_SVG_LIBRARY_LUNASVG)
    Expects(m_row < m_row_count);

    //const auto  current_path = fs::current_path();
    const auto document = lunasvg::Document::loadFromFile(path.string());
    if (!document)
    {
        log_svg->error("Unable to load {}", path.string());
        return glm::vec2{0.0f, 0.0f};
    }

    // Render a super sampled icon
    const auto bitmap_ss = document->renderToBitmap(m_icon_width * 4, m_icon_height * 4);

    // Downsample
    const lunasvg::Bitmap bitmap{
        static_cast<uint32_t>(m_icon_width),
        static_cast<uint32_t>(m_icon_height)
    };
    const auto read_stride  = bitmap_ss.stride();
    const auto write_stride = bitmap.stride();
    for (int y = 0; y < m_icon_height; ++y)
    {
        for (int x = 0; x < m_icon_width; ++x)
        {
            float data[4] = { 0, 0, 0, 0};
            for (int ys = 0; ys < 4; ++ys)
            {
                for (int xs = 0; xs < 4; ++xs)
                {
                    const int offset =
                        (
                            (y * 4 + ys) * read_stride +
                            (x * 4 + xs) * 4
                        );
                    for (int c = 0; c < 4; ++c)
                    {
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

    const int   x_offset  = m_column * m_icon_width;
    const int   y_offset  = m_row    * m_icon_height;
    const float u         = static_cast<float>(x_offset) / static_cast<float>(m_texture->width());
    const float v         = static_cast<float>(y_offset) / static_cast<float>(m_texture->height());

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

    ++m_column;
    if (m_column >= m_column_count)
    {
        m_column = 0;
        ++m_row;
    }

    return glm::vec2{u, v};
#else
    static_cast<void>(path);
    return glm::vec2{};
#endif
}

auto Icon_set::uv1(const glm::vec2& uv0) const -> glm::vec2
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

void Icon_set::icon(
    const glm::vec2 uv0,
    const glm::vec4 tint_color
) const
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
#else
    ERHE_PROFILE_FUNCTION

    get<erhe::application::Imgui_renderer>()->image(
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

auto Icon_set::get_icon(const erhe::scene::Light_type type) const -> const glm::vec2
{
    switch (type)
    {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::spot:        return icons.spot_light;
        case erhe::scene::Light_type::directional: return icons.directional_light;
        case erhe::scene::Light_type::point:       return icons.point_light;
        default: return {};
    }
}

void Icon_set::icon(const erhe::scene::Camera&) const
{
    icon(icons.camera);
}

void Icon_set::icon(const erhe::scene::Light& light) const
{
    icon(get_icon(light.type), glm::vec4{light.color, 1.0f});
}

void Icon_set::icon(const erhe::scene::Mesh&) const
{
    icon(icons.mesh);
}

void Icon_set::icon(const erhe::scene::Node&) const
{
    icon(icons.node);
}

}
