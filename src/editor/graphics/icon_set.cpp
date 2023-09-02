#include "graphics/icon_set.hpp"
#include "editor_log.hpp"
#include "editor_settings.hpp"
#include "renderers/programs.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_bit/bit_helpers.hpp"

#if defined(ERHE_SVG_LIBRARY_LUNASVG)
#   include <lunasvg.h>
#endif

namespace editor {

Icon_set::Icon_set(
    erhe::graphics::Instance&    graphics_instance,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    Icon_settings&               icon_settings,
    Programs&                    programs
)
{
    load_icons(graphics_instance, imgui_renderer, icon_settings, programs);
}

void Icon_set::load_icons(
    erhe::graphics::Instance&    graphics_instance,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    Icon_settings&               icon_settings,
    Programs&                    programs
)
{
    const auto icon_directory = std::filesystem::path("res") / "icons";

    m_row_count    = 16;
    m_column_count = 16;
    m_row          = 0;
    m_column       = 0;
    m_small  = std::make_unique<Icon_rasterization>(graphics_instance, imgui_renderer, programs, icon_settings.small_icon_size,  m_column_count, m_row_count);
    m_large  = std::make_unique<Icon_rasterization>(graphics_instance, imgui_renderer, programs, icon_settings.large_icon_size,  m_column_count, m_row_count);
    m_hotbar = std::make_unique<Icon_rasterization>(graphics_instance, imgui_renderer, programs, icon_settings.hotbar_icon_size, m_column_count, m_row_count);

    icons.anim              = load(icon_directory / "anim.svg");
    icons.bone              = load(icon_directory / "bone_data.svg");
    icons.brush_big         = load(icon_directory / "brush_big.svg");
    icons.brush_small       = load(icon_directory / "brush_small.svg");
    icons.camera            = load(icon_directory / "camera.svg");
    icons.directional_light = load(icon_directory / "directional_light.svg");
    icons.drag              = load(icon_directory / "drag.svg");
    icons.file              = load(icon_directory / "file.svg");
    icons.folder            = load(icon_directory / "filebrowser.svg");
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
    icons.physics           = load(icon_directory / "physics.svg");
    icons.point_light       = load(icon_directory / "point_light.svg");
    icons.pull              = load(icon_directory / "pull.svg");
    icons.push              = load(icon_directory / "push.svg");
    icons.raytrace          = load(icon_directory / "curve_path.svg");
    icons.rotate            = load(icon_directory / "rotate.svg");
    icons.scale             = load(icon_directory / "scale.svg");
    icons.scene             = load(icon_directory / "scene.svg");
    icons.select            = load(icon_directory / "select.svg");
    icons.skin              = load(icon_directory / "armature_data.svg");
    icons.space_mouse       = load(icon_directory / "space_mouse.svg");
    icons.space_mouse_lmb   = load(icon_directory / "space_mouse_lmb.svg");
    icons.space_mouse_rmb   = load(icon_directory / "space_mouse_rmb.svg");
    icons.spot_light        = load(icon_directory / "spot_light.svg");
    icons.texture           = load(icon_directory / "texture.svg");
    icons.three_dots        = load(icon_directory / "three_dots.svg");
    icons.vive              = load(icon_directory / "vive.svg");
    icons.vive_menu         = load(icon_directory / "vive_menu.svg");
    icons.vive_trackpad     = load(icon_directory / "vive_trackpad.svg");
    icons.vive_trigger      = load(icon_directory / "vive_trigger.svg");

    type_icons.resize(erhe::Item_type::count);
    type_icons[erhe::Item_type::index_scene               ] = { .icon = icons.scene,       .color = glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_content_library_node] = { .icon = icons.folder,      .color = glm::vec4{0.7f, 0.7f, 0.7f, 1.0f}};
    type_icons[erhe::Item_type::index_brush               ] = { .icon = icons.brush_small, .color = glm::vec4{0.7f, 0.8f, 0.9f, 1.0f}};
    type_icons[erhe::Item_type::index_material            ] = { .icon = icons.material};   // .color = glm::vec4{1.0f, 0.1f, 0.1f, 1.0f}};
    type_icons[erhe::Item_type::index_node                ] = { .icon = icons.node};       // .color = glm::vec4{0.7f, 0.8f, 0.9f, 1.0f}};
    type_icons[erhe::Item_type::index_mesh                ] = { .icon = icons.mesh,        .color = glm::vec4{0.6f, 1.0f, 0.6f, 1.0f}};
    type_icons[erhe::Item_type::index_skin                ] = { .icon = icons.skin,        .color = glm::vec4{1.0f, 0.5f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_bone                ] = { .icon = icons.bone,        .color = glm::vec4{0.5f, 1.0f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_animation           ] = { .icon = icons.anim,        .color = glm::vec4{1.0f, 0.5f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_camera              ] = { .icon = icons.camera};     // .color = glm::vec4{0.4f, 0.0f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_light               ] = { .icon = icons.point_light};// .color = glm::vec4{1.0f, 0.8f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_physics             ] = { .icon = icons.physics,     .color = glm::vec4{0.2f, 0.5f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_raytrace            ] = { .icon = icons.raytrace,    .color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_grid                ] = { .icon = icons.grid,        .color = glm::vec4{0.0f, 0.6f, 0.0f, 1.0f}};
    type_icons[erhe::Item_type::index_texture             ] = { .icon = icons.texture,     .color = glm::vec4{0.5f, 0.8f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_folder        ] = { .icon = icons.folder,      .color = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_gltf     ] = { .icon = icons.scene,       .color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_png      ] = { .icon = icons.texture,     .color = glm::vec4{0.8f, 0.8f, 0.8f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_other    ] = { .icon = icons.file,        .color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}};
}

void Icon_set::add_icons(
    const uint64_t item_type,
    const float    scale
)
{
    using namespace erhe::bit;
    for (uint64_t bit_position = 0; bit_position < erhe::Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        if (test_all_rhs_bits_set(item_type, bit_mask)) {
            const auto& icon_opt = type_icons.at(bit_position);
            if (icon_opt.has_value()) {
                const auto& icon = icon_opt.value();
                const auto& icon_rasterization = scale < 1.5f
                    ? get_small_rasterization()
                    : get_large_rasterization();
                glm::vec4 color = icon.color.has_value() ? icon.color.value() : glm::vec4{0.9f, 0.9f, 0.9f, 1.0f};
                icon_rasterization.icon(icon.icon, color);
            }
        }
    }
}

auto Icon_set::load(const std::filesystem::path& path) -> glm::vec2
{
#if defined(ERHE_SVG_LIBRARY_LUNASVG)
    Expects(m_row < m_row_count);

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
    return *m_small.get();
}

[[nodiscard]] auto Icon_set::get_large_rasterization() const -> const Icon_rasterization&
{
    return *m_large.get();
}

[[nodiscard]] auto Icon_set::get_hotbar_rasterization() const -> const Icon_rasterization&
{
    return *m_hotbar.get();
}

} // namespace editor
