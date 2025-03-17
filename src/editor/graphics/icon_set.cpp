#include "graphics/icon_set.hpp"
#include "graphics/icon_rasterization.hpp"
#include "editor_context.hpp"
#include "editor_settings.hpp"
#include "scene/content_library.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/skin.hpp"

namespace editor {

void Icons::queue_load_icons(Icon_loader& loader)
{
    struct Entry{
        glm::vec2&  uv;
        const char* name;
    };
    const Entry entries[] = {
        {anim             , "anim.svg"             },
        {bone             , "bone_data.svg"        },
        {brush_big        , "brush_big.svg"        },
        {brush_small      , "brush_small.svg"      },
        {camera           , "camera.svg"           },
        {directional_light, "directional_light.svg"},
        {drag             , "drag.svg"             },
        {file             , "file.svg"             },
        {folder           , "filebrowser.svg"      },
        {grid             , "grid.svg"             },
        {hud              , "hud.svg"              },
        {material         , "material.svg"         },
        {mesh             , "mesh.svg"             },
        {mouse_lmb        , "mouse_lmb.svg"        },
        {mouse_lmb_drag   , "mouse_lmb_drag.svg"   },
        {mouse_mmb        , "mouse_mmb.svg"        },
        {mouse_mmb_drag   , "mouse_mmb_drag.svg"   },
        {mouse_move       , "mouse_move.svg"       },
        {mouse_rmb        , "mouse_rmb.svg"        },
        {mouse_rmb_drag   , "mouse_rmb_drag.svg"   },
        {move             , "move.svg"             },
        {node             , "node.svg"             },
        {physics          , "physics.svg"          },
        {point_light      , "point_light.svg"      },
        {pull             , "pull.svg"             },
        {push             , "push.svg"             },
        {raytrace         , "curve_path.svg"       },
        {rotate           , "rotate.svg"           },
        {scale            , "scale.svg"            },
        {scene            , "scene.svg"            },
        {select           , "select.svg"           },
        {skin             , "armature_data.svg"    },
        {space_mouse      , "space_mouse.svg"      },
        {space_mouse_lmb  , "space_mouse_lmb.svg"  },
        {space_mouse_rmb  , "space_mouse_rmb.svg"  },
        {spot_light       , "spot_light.svg"       },
        {texture          , "texture.svg"          },
        {three_dots       , "three_dots.svg"       },
        {vive             , "vive.svg"             },
        {vive_menu        , "vive_menu.svg"        },
        {vive_trackpad    , "vive_trackpad.svg"    },
        {vive_trigger     , "vive_trigger.svg"     }
    };
    for (const Entry& entry : entries) {
        loader.queue_icon_load(entry.uv, entry.name);
    }
}

Icon_set::Icon_set(
    Editor_context&           editor_context,
    erhe::graphics::Instance& graphics_instance,
    Icon_settings&            icon_settings,
    Icons&                    icons_in,
    Icon_loader&              loader
)
    : m_context{editor_context}
{
    load_icons(graphics_instance, icon_settings, icons_in, loader);
}

void Icon_set::load_icons(
    erhe::graphics::Instance& graphics_instance,
    Icon_settings&            icon_settings,
    Icons&                    icons_in,
    Icon_loader&              loader
)
{
    ERHE_PROFILE_FUNCTION();

    icons = icons_in;

    m_small  = std::make_unique<Icon_rasterization>(m_context, graphics_instance, icon_settings.small_icon_size);
    m_large  = std::make_unique<Icon_rasterization>(m_context, graphics_instance, icon_settings.large_icon_size);
    m_hotbar = std::make_unique<Icon_rasterization>(m_context, graphics_instance, icon_settings.hotbar_icon_size);

    loader.upload_to_texture(*m_small.get());
    loader.upload_to_texture(*m_large.get());
    loader.upload_to_texture(*m_hotbar.get());
    loader.clear_load_queue();

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
    type_icons[erhe::Item_type::index_asset_file_geogram  ] = { .icon = icons.scene,       .color = glm::vec4{0.0f, 0.8f, 1.0f, 1.0f}};
    //type_icons[erhe::Item_type::index_asset_file_png      ] = { .icon = icons.texture,     .color = glm::vec4{0.8f, 0.8f, 0.8f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_other    ] = { .icon = icons.file,        .color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}};
}

void Icon_set::add_icons(const uint64_t item_type, const float scale)
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

auto Icon_set::get_small_rasterization() const -> const Icon_rasterization&
{
    return *m_small.get();
}

auto Icon_set::get_large_rasterization() const -> const Icon_rasterization&
{
    return *m_large.get();
}

auto Icon_set::get_hotbar_rasterization() const -> const Icon_rasterization&
{
    return *m_hotbar.get();
}

void Icon_set::item_icon(const std::shared_ptr<erhe::Item_base>& item, const float scale)
{
    const auto& icon_rasterization = (scale < 1.5f)
        ? m_context.icon_set->get_small_rasterization()
        : m_context.icon_set->get_large_rasterization();

    std::optional<glm::vec2> icon;
    glm::vec4 tint_color{0.8f, 0.8f, 0.8f, 1.0f}; // TODO  item->get_wireframe_color(); ?
    glm::vec4 background_color{0.0f, 0.0f, 0.0f, 0.0f};
    ////if (item->is_selected()) {
    ////    ImGuiStyle& style = ImGui::GetCurrentContext()->Style;
    ////    ImVec4 selected_color = style.Colors[ImGuiCol_Header];
    ////    background_color = selected_color;
    ////}

    const auto content_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (content_node) {
        if (content_node->item) {
            item_icon(content_node->item, scale);
        } else {
            // Content libray node without item is considered folder
            icon_rasterization.icon(icons.folder, background_color, tint_color);
        }
        return;
    }

    const uint64_t type_mask = item->get_type();
    using namespace erhe::bit;
    for (uint64_t bit_position = 0; bit_position < erhe::Item_type::count; ++bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        if (test_all_rhs_bits_set(type_mask, bit_mask)) {
            const auto& icon_opt = type_icons.at(bit_position);
            if (icon_opt.has_value()) {
                const auto& type_icon = icon_opt.value();
                icon  = type_icon.icon;
                if (type_icon.color.has_value()) {
                    tint_color = type_icon.color.value();
                }
                break;
            }
        }
    }

    if (erhe::scene::is_bone(item)) {
        icon = icons.bone;
    }
    const auto& material = std::dynamic_pointer_cast<erhe::primitive::Material>(item);
    if (material) {
        tint_color = material->base_color;
    }
    const auto& light = std::dynamic_pointer_cast<erhe::scene::Light>(item);
    if (light) {
        tint_color = glm::vec4{light->color, 1.0f};
        switch (light->type) {
            //using enum erhe::scene::Light_type;
            case erhe::scene::Light_type::spot:        icon = icons.spot_light; break;
            case erhe::scene::Light_type::directional: icon = icons.directional_light; break;
            case erhe::scene::Light_type::point:       icon = icons.point_light; break;
            default: break;
        }
    }

    if (icon.has_value()) {
        icon_rasterization.icon(icon.value(), background_color, tint_color);
    }
}

} // namespace editor
