#include "icon_set.hpp"
#include "gl_context_provider.hpp"
#include "erhe/graphics/texture.hpp"
#include "lunasvg.h"

namespace editor {

Icon_set::Icon_set(int icon_width, int icon_height, int row_count, int column_count)
    : erhe::components::Component{c_name}
    , m_icon_width               {icon_width}
    , m_icon_height              {icon_height}
    , m_row_count                {row_count}
    , m_column_count             {column_count}
    , m_row                      {0}
    , m_column                   {0}
{
}

Icon_set::~Icon_set() = default;

void Icon_set::connect()
{
    require<Gl_context_provider>();
}

void Icon_set::initialize_component()
{
    Scoped_gl_context gl_context{Component::get<Gl_context_provider>().get()};

    const erhe::graphics::Texture_create_info create_info{
        gl::Texture_target::texture_2d,
        gl::Internal_format::rgba8,
        true,
        m_column_count * m_icon_width,
        m_row_count * m_icon_height
    };
    texture = std::make_shared<erhe::graphics::Texture>(create_info);
    texture->set_debug_label("Icon_set");

    m_icon_uv_width  = static_cast<float>(m_icon_width ) / static_cast<float>(texture->width());
    m_icon_uv_height = static_cast<float>(m_icon_height) / static_cast<float>(texture->height());

    const auto icon_directory = std::filesystem::path("res") / "icons";

    icons.camera            = load(icon_directory / "camera.svg");
    icons.directional_light = load(icon_directory / "directional_light.svg");
    icons.spot_light        = load(icon_directory / "spot_light.svg");
    icons.point_light       = load(icon_directory / "point_light.svg");
    icons.mesh              = load(icon_directory / "mesh.svg");
    icons.node              = load(icon_directory / "node.svg");
}

auto Icon_set::load(const std::filesystem::path& path)
-> ImVec2
{
    Expects(m_row < m_row_count);

    //const auto  current_path = std::filesystem::current_path();
    const auto  document     = lunasvg::Document::loadFromFile(path.string());
    const auto  bitmap       = document->renderToBitmap(m_icon_width, m_icon_height);
    const int   x_offset     = m_column * m_icon_width;
    const int   y_offset     = m_row    * m_icon_height;
    const float u            = static_cast<float>(x_offset) / static_cast<float>(texture->width());
    const float v            = static_cast<float>(y_offset) / static_cast<float>(texture->height());

    const auto  span     = gsl::span<std::byte>{
        reinterpret_cast<std::byte*>(bitmap.data()),
        bitmap.stride() * bitmap.height()
    };

    texture->upload(gl::Internal_format::rgba8, span, bitmap.width(), bitmap.height(), 1, 0, x_offset, y_offset, 0);

    m_column++;
    if (m_column >= m_column_count)
    {
        m_column = 0;
        m_row++;
    }
    
    return ImVec2(u, v);
}

auto Icon_set::uv1(const ImVec2& uv0) -> ImVec2
{
    return ImVec2{uv0.x + m_icon_uv_width, uv0.y + m_icon_uv_height};
}

}
