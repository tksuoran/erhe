#include "icon_set.hpp"
#include "editor_log.hpp"

#include "renderers/programs.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_SVG_LIBRARY_LUNASVG)
#   include <lunasvg.h>
#endif

namespace editor {

Icon_set* g_icon_set{nullptr};

Icon_set::Icon_set()
    : erhe::components::Component{c_type_name}
{
}

Icon_set::~Icon_set()
{
    ERHE_VERIFY(g_icon_set == nullptr);
}

void Icon_set::deinitialize_component()
{
    ERHE_VERIFY(g_icon_set == this);
    const erhe::application::Scoped_gl_context gl_context;
    m_small.reset();
    m_large.reset();
    m_hotbar.reset();
    g_icon_set = nullptr;
}

void Icon_set::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Gl_context_provider>();
    require<Programs>();
}

void Icon_set::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_icon_set == nullptr);

    auto ini = erhe::application::get_ini("erhe.ini", "icons");
    ini->get("small_icon_size",  config.small_icon_size);
    ini->get("large_icon_size",  config.large_icon_size);
    ini->get("hotbar_icon_size", config.hotbar_icon_size);

    m_row_count    = 16;
    m_column_count = 16;
    m_row          = 0;
    m_column       = 0;

    const erhe::application::Scoped_gl_context gl_context;

    m_small  = Icon_rasterization(config.small_icon_size,  m_column_count, m_row_count);
    m_large  = Icon_rasterization(config.large_icon_size,  m_column_count, m_row_count);
    m_hotbar = Icon_rasterization(config.hotbar_icon_size, m_column_count, m_row_count);

    const auto icon_directory = std::filesystem::path("res") / "icons";

    icons.brush_big         = load(icon_directory / "brush_big.svg");
    icons.brush_small       = load(icon_directory / "brush_small.svg");
    icons.camera            = load(icon_directory / "camera.svg");
    icons.directional_light = load(icon_directory / "directional_light.svg");
    icons.drag              = load(icon_directory / "drag.svg");
    icons.grid              = load(icon_directory / "grid.svg");
    icons.hud               = load(icon_directory / "hud.svg");
    icons.material          = load(icon_directory / "material.svg");
    icons.mesh              = load(icon_directory / "mesh.svg");
    icons.mouse_lmb         = load(icon_directory / "mouse_lmb.svg");
    icons.mouse_lmb_drag    = load(icon_directory / "mouse_lmb_drag.svg");
    icons.mouse_mmb         = load(icon_directory / "mouse_mmb.svg");
    icons.mouse_mmb_drag    = load(icon_directory / "mouse_mmb_drag.svg");
    icons.mouse_move        = load(icon_directory / "mouse_move.svg");
    icons.mouse_rmb         = load(icon_directory / "mouse_rmb.svg");
    icons.mouse_rmb_drag    = load(icon_directory / "mouse_rmb_drag.svg");
    icons.move              = load(icon_directory / "move.svg");
    icons.node              = load(icon_directory / "node.svg");
    icons.point_light       = load(icon_directory / "point_light.svg");
    icons.pull              = load(icon_directory / "pull.svg");
    icons.push              = load(icon_directory / "push.svg");
    icons.rotate            = load(icon_directory / "rotate.svg");
    icons.scene             = load(icon_directory / "scene.svg");
    icons.select            = load(icon_directory / "select.svg");
    icons.space_mouse       = load(icon_directory / "space_mouse.svg");
    icons.space_mouse_lmb   = load(icon_directory / "space_mouse_lmb.svg");
    icons.space_mouse_rmb   = load(icon_directory / "space_mouse_rmb.svg");
    icons.spot_light        = load(icon_directory / "spot_light.svg");
    icons.three_dots        = load(icon_directory / "three_dots.svg");
    icons.vive              = load(icon_directory / "vive.svg");
    icons.vive_menu         = load(icon_directory / "vive_menu.svg");
    icons.vive_trackpad     = load(icon_directory / "vive_trackpad.svg");
    icons.vive_trigger      = load(icon_directory / "vive_trigger.svg");

    g_icon_set = this;
}

auto Icon_set::load(const std::filesystem::path& path) -> glm::vec2
{
#if defined(ERHE_SVG_LIBRARY_LUNASVG)
    Expects(m_row < m_row_count);

    //const auto  current_path = std::filesystem::current_path();
    const auto document = lunasvg::Document::loadFromFile(path.string());
    if (!document) {
        log_svg->error("Unable to load {}", path.string());
        return glm::vec2{0.0f, 0.0f};
    }

    const float u = static_cast<float>(m_column) / static_cast<float>(m_column_count);
    const float v = static_cast<float>(m_row   ) / static_cast<float>(m_row_count);

    m_small ->rasterize(*document.get(), m_column, m_row);
    m_large ->rasterize(*document.get(), m_column, m_row);
    m_hotbar->rasterize(*document.get(), m_column, m_row);

    ++m_column;
    if (m_column >= m_column_count) {
        m_column = 0;
        ++m_row;
    }

    return glm::vec2{u, v};
#else
    static_cast<void>(path);
    return glm::vec2{};
#endif
}

auto Icon_set::get_icon(const erhe::scene::Light_type type) const -> const glm::vec2
{
    switch (type) {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::spot:        return icons.spot_light;
        case erhe::scene::Light_type::directional: return icons.directional_light;
        case erhe::scene::Light_type::point:       return icons.point_light;
        default: return {};
    }
}

[[nodiscard]] auto Icon_set::get_small_rasterization() const -> const Icon_rasterization&
{
    return m_small.value();
}

[[nodiscard]] auto Icon_set::get_large_rasterization() const -> const Icon_rasterization&
{
    return m_large.value();
}

[[nodiscard]] auto Icon_set::get_hotbar_rasterization() const -> const Icon_rasterization&
{
    return m_hotbar.value();
}

}
