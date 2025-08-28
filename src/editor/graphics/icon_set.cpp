#include "graphics/icon_set.hpp"
#include "app_context.hpp"
#include "content_library/content_library.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/skin.hpp"

#define ICON_MDI_BRUSH                                    "\xf3\xb0\x83\xa3" // U+F00E3

namespace editor {

Icon_set::Icon_set(
    App_context&                 context,
    erhe::imgui::Imgui_renderer& imgui_renderer
)
    : m_context{context}
{
    material_design = imgui_renderer.material_design_font();
    custom_icons    = imgui_renderer.icon_font();

    icons.anim              = "\xee\xa8\x81";
    icons.skin              = "\xee\xa8\x82";
    icons.bone              = "\xee\xa8\x83";
    icons.brush_big         = "\xee\xa8\x84";
    icons.brush_small       = "\xee\xa8\x85";
    icons.brush_tool        = "\xee\xa8\x86";
    icons.camera            = "\xee\xa8\x87";
    icons.raytrace          = "\xee\xa8\x88";
    icons.directional_light = "\xee\xa8\x89";
    icons.drag              = "\xee\xa8\x8a";
    icons.file              = "\xee\xa8\x8b";
    icons.folder            = "\xee\xa8\x8c";
    icons.grid              = "\xee\xa8\x8d";
    icons.hud               = "\xee\xa8\x8f";
    icons.material          = "\xee\xa8\x90";
    icons.mesh              = "\xee\xa8\x91";
    icons.mouse_lmb         = "\xee\xa8\x98";
    icons.mouse_lmb_drag    = "\xee\xa8\x99";
    icons.mouse_mmb         = "\xee\xa8\x9a";
    icons.mouse_mmb_drag    = "\xee\xa8\x9b";
    icons.mouse_move        = "\xee\xa8\x9c";
    icons.mouse_rmb         = "\xee\xa8\x9d";
    icons.mouse_rmb_drag    = "\xee\xa8\x9e";
    icons.move              = "\xee\xa8\x9f";
    icons.node              = "\xee\xa8\xa0";
    icons.physics           = "\xee\xa8\xa1";
    icons.point_light       = "\xee\xa8\xa2";
    icons.pull              = "\xee\xa8\xa3";
    icons.push              = "\xee\xa8\xa4";
    icons.rotate            = "\xee\xa8\xa5";
    icons.scale             = "\xee\xa8\xa6";
    icons.scene             = "\xee\xa8\xa7";
    icons.select            = "\xee\xa8\xa8";
    icons.space_mouse       = "\xee\xa8\xa9";
    icons.space_mouse_lmb   = "\xee\xa8\xaa";
    icons.space_mouse_rmb   = "\xee\xa8\xab";
    icons.spot_light        = "\xee\xa8\xac";
    icons.texture           = "\xee\xa8\xad";
    icons.three_dots        = "\xee\xa8\xae";
    icons.vive              = "\xee\xa8\xaf";
    icons.vive_menu         = "\xee\xa8\xb0";
    icons.vive_trackpad     = "\xee\xa8\xb1";
    icons.vive_trigger      = "\xee\xa8\xb2";

    type_icons.resize(erhe::Item_type::count);
    type_icons[erhe::Item_type::index_scene               ] = { .code = icons.scene,       .color = glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_content_library_node] = { .code = icons.folder,      .color = glm::vec4{0.7f, 0.7f, 0.7f, 1.0f}};
    type_icons[erhe::Item_type::index_brush               ] = {
        .code = icons.brush_small,
        .color = glm::vec4{0.7f, 0.8f, 0.9f, 1.0f}
    };
    type_icons[erhe::Item_type::index_material            ] = { .code = icons.material};   // .color = glm::vec4{1.0f, 0.1f, 0.1f, 1.0f}};
    type_icons[erhe::Item_type::index_node                ] = { .code = icons.node};       // .color = glm::vec4{0.7f, 0.8f, 0.9f, 1.0f}};
    type_icons[erhe::Item_type::index_mesh                ] = { .code = icons.mesh,        .color = glm::vec4{0.6f, 1.0f, 0.6f, 1.0f}};
    type_icons[erhe::Item_type::index_skin                ] = { .code = icons.skin,        .color = glm::vec4{1.0f, 0.5f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_bone                ] = { .code = icons.bone,        .color = glm::vec4{0.5f, 1.0f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_animation           ] = { .code = icons.anim,        .color = glm::vec4{1.0f, 0.5f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_camera              ] = { .code = icons.camera};     // .color = glm::vec4{0.4f, 0.0f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_light               ] = { .code = icons.point_light};// .color = glm::vec4{1.0f, 0.8f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_physics             ] = { .code = icons.physics,     .color = glm::vec4{0.2f, 0.5f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_raytrace            ] = { .code = icons.raytrace,    .color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_grid                ] = { .code = icons.grid,        .color = glm::vec4{0.0f, 0.6f, 0.0f, 1.0f}};
    type_icons[erhe::Item_type::index_texture             ] = { .code = icons.texture,     .color = glm::vec4{0.5f, 0.8f, 1.0f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_folder        ] = { .code = icons.folder,      .color = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_gltf     ] = { .code = icons.scene,       .color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_geogram  ] = { .code = icons.scene,       .color = glm::vec4{0.0f, 0.8f, 1.0f, 1.0f}};
    //type_icons[erhe::Item_type::index_asset_file_png      ] = { .code = icons.texture,     .color = glm::vec4{0.8f, 0.8f, 0.8f, 1.0f}};
    type_icons[erhe::Item_type::index_asset_file_other    ] = { .code = icons.file,        .color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f}};
    type_icons[erhe::Item_type::index_brush_placement     ] = {
        .font  = material_design,
        .code  = ICON_MDI_BRUSH,
        .color = glm::vec4{0.4f, 0.1f, 1.0f, 1.0f}
    };

}

void Icon_set::add_icons(const uint64_t item_type, const float size)
{
    using namespace erhe::utility;
    for (uint64_t bit_position = 0; bit_position < erhe::Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        if (test_bit_set(item_type, bit_mask)) {
            const auto& icon_opt = type_icons.at(bit_position);
            if (icon_opt.has_value()) {
                const auto& icon = icon_opt.value();
                glm::vec4 color = icon.color.has_value() ? icon.color.value() : glm::vec4{0.9f, 0.9f, 0.9f, 1.0f};
                if (icon.code != nullptr) {
                    draw_icon(icon.code, color, icon.font, size);
                }
            }
        }
    }
}

auto Icon_set::get_icon(const erhe::scene::Light_type type) const -> const char*
{
    switch (type) {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::spot:        return icons.spot_light;
        case erhe::scene::Light_type::directional: return icons.directional_light;
        case erhe::scene::Light_type::point:       return icons.point_light;
        default: return {};
    }
}

void Icon_set::draw_icon(const char* code, glm::vec4 color, ImFont* font, float size)
{
    if (code == nullptr) {
        return;
    }

    ImFont*     icon_font = (font != nullptr) ? font : m_context.imgui_renderer->icon_font();
    const float font_size = m_context.imgui_renderer->get_imgui_settings().icon_font_size;

    ImGui::PushFont(icon_font, size == 0.0f ? font_size : size);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(code);
    ImGui::SameLine();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

auto Icon_set::icon_button(
    const uint32_t                 id,
    const char*                    code,
    ImFont* const                  font,
    const float                    size,
    const glm::vec4                color,
    const std::optional<glm::vec4> background_color
) const -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (code == nullptr) {
        return false;
    }

    ImFont*     icon_font = (font != nullptr) ? font : m_context.imgui_renderer->icon_font();
    const float font_size = m_context.imgui_renderer->get_imgui_settings().icon_font_size;

    ImGui::PushID        (id);
    ImGui::PushFont      (icon_font, size == 0.0f ? font_size : size);
    if (background_color.has_value()) {
        ImGui::PushStyleColor(ImGuiCol_Button, background_color.value());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool result = ImGui::Button(code);
    ImGui::SameLine     ();
    ImGui::PopStyleColor(background_color.has_value() ? 2 : 1);
    ImGui::PopFont      ();
    ImGui::SameLine     ();
    ImGui::PopID        ();
    return result;
}

void Icon_set::item_icon(const std::shared_ptr<erhe::Item_base>& item, const float scale)
{
    ImFont*                    icon_font{nullptr};
    std::optional<const char*> icon_code{};
    glm::vec4 tint_color{0.8f, 0.8f, 0.8f, 1.0f}; // TODO  item->get_wireframe_color(); ?

    const auto content_node = std::dynamic_pointer_cast<Content_library_node>(item);
    if (content_node) {
        if (content_node->item) {
            item_icon(content_node->item, scale);
        } else {
            // Content libray node without item is considered folder
            draw_icon(icons.folder, tint_color);
        }
        return;
    }

    const uint64_t type_mask = item->get_type();
    using namespace erhe::utility;
    for (uint64_t bit_position = 0; bit_position < erhe::Item_type::count; ++bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        if (test_bit_set(type_mask, bit_mask)) {
            const auto& icon_opt = type_icons.at(bit_position);
            if (icon_opt.has_value()) {
                const auto& type_icon = icon_opt.value();
                icon_font = type_icon.font;
                icon_code = type_icon.code;
                if (type_icon.color.has_value()) {
                    tint_color = type_icon.color.value();
                }
                break;
            }
        }
    }

    if (erhe::scene::is_bone(item)) {
        icon_code = icons.bone;
    }
    const auto& material = std::dynamic_pointer_cast<erhe::primitive::Material>(item);
    if (material) {
        tint_color = glm::vec4{material->data.base_color, 1.0f};
        icon_code = icons.material;
    }
    const auto& light = std::dynamic_pointer_cast<erhe::scene::Light>(item);
    if (light) {
        tint_color = glm::vec4{light->color, 1.0f};
        switch (light->type) {
            //using enum erhe::scene::Light_type;
            case erhe::scene::Light_type::spot:        icon_code = icons.spot_light; break;
            case erhe::scene::Light_type::directional: icon_code = icons.directional_light; break;
            case erhe::scene::Light_type::point:       icon_code = icons.point_light; break;
            default: break;
        }
    }

    if (icon_code.has_value()) {
        draw_icon(icon_code.value(), tint_color, icon_font);
    }

}

}
