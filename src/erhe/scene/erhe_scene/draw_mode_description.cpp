#include "erhe_scene/draw_mode_description.hpp"

#include <algorithm>

namespace erhe::scene {

auto c_str(const Draw_mode value) -> const char*
{
    switch (value) {
        case Draw_mode::inherited: return "inherited";
        case Draw_mode::default_ : return "default";
        case Draw_mode::origin   : return "origin";
        case Draw_mode::bounds   : return "bounds";
        case Draw_mode::cards    : return "cards";
        default                  : return "inherited";
    }
}

auto c_str(const Draw_mode_card_geometry value) -> const char*
{
    switch (value) {
        case Draw_mode_card_geometry::cross       : return "cross";
        case Draw_mode_card_geometry::box         : return "box";
        case Draw_mode_card_geometry::from_texture: return "fromTexture";
        default                                   : return "cross";
    }
}

auto c_str(const Draw_mode_card_visibility value) -> const char*
{
    switch (value) {
        case Draw_mode_card_visibility::inherited: return "inherited";
        case Draw_mode_card_visibility::full     : return "full";
        case Draw_mode_card_visibility::simple   : return "simple";
        default                                  : return "inherited";
    }
}

auto c_str(const Draw_mode_card_face value) -> const char*
{
    switch (value) {
        case Draw_mode_card_face::x_neg: return "card_texture_x_neg";
        case Draw_mode_card_face::x_pos: return "card_texture_x_pos";
        case Draw_mode_card_face::y_neg: return "card_texture_y_neg";
        case Draw_mode_card_face::y_pos: return "card_texture_y_pos";
        case Draw_mode_card_face::z_neg: return "card_texture_z_neg";
        case Draw_mode_card_face::z_pos: return "card_texture_z_pos";
        default                        : return "card_texture_x_neg";
    }
}

auto draw_mode_from_string(const std::string_view text, Draw_mode& out_value) -> bool
{
    if (text == "inherited") { out_value = Draw_mode::inherited; return true; }
    if (text == "default"  ) { out_value = Draw_mode::default_;  return true; }
    if (text == "origin"   ) { out_value = Draw_mode::origin;    return true; }
    if (text == "bounds"   ) { out_value = Draw_mode::bounds;    return true; }
    if (text == "cards"    ) { out_value = Draw_mode::cards;     return true; }
    return false;
}

auto draw_mode_card_geometry_from_string(const std::string_view text, Draw_mode_card_geometry& out_value) -> bool
{
    if (text == "cross"      ) { out_value = Draw_mode_card_geometry::cross;        return true; }
    if (text == "box"        ) { out_value = Draw_mode_card_geometry::box;          return true; }
    if (text == "fromTexture") { out_value = Draw_mode_card_geometry::from_texture; return true; }
    return false;
}

auto draw_mode_card_visibility_from_string(const std::string_view text, Draw_mode_card_visibility& out_value) -> bool
{
    if (text == "inherited") { out_value = Draw_mode_card_visibility::inherited; return true; }
    if (text == "full"     ) { out_value = Draw_mode_card_visibility::full;      return true; }
    if (text == "simple"   ) { out_value = Draw_mode_card_visibility::simple;    return true; }
    return false;
}

auto draw_mode_card_face_from_string(const std::string_view text, Draw_mode_card_face& out_value) -> bool
{
    for (std::size_t i = 0; i < c_draw_mode_card_face_count; ++i) {
        const Draw_mode_card_face face = static_cast<Draw_mode_card_face>(i);
        if (text == c_str(face)) {
            out_value = face;
            return true;
        }
    }
    return false;
}

namespace {

constexpr erhe::property::Enum_entry c_draw_mode_entries[] = {
    { "inherited", static_cast<int32_t>(Draw_mode::inherited) },
    { "default",   static_cast<int32_t>(Draw_mode::default_)  },
    { "origin",    static_cast<int32_t>(Draw_mode::origin)    },
    { "bounds",    static_cast<int32_t>(Draw_mode::bounds)    },
    { "cards",     static_cast<int32_t>(Draw_mode::cards)     }
};

constexpr erhe::property::Enum_entry c_draw_mode_card_geometry_entries[] = {
    { "cross",       static_cast<int32_t>(Draw_mode_card_geometry::cross)        },
    { "box",         static_cast<int32_t>(Draw_mode_card_geometry::box)          },
    { "fromTexture", static_cast<int32_t>(Draw_mode_card_geometry::from_texture) }
};

constexpr erhe::property::Enum_entry c_draw_mode_card_visibility_entries[] = {
    { "inherited", static_cast<int32_t>(Draw_mode_card_visibility::inherited) },
    { "full",      static_cast<int32_t>(Draw_mode_card_visibility::full)      },
    { "simple",    static_cast<int32_t>(Draw_mode_card_visibility::simple)    }
};

} // anonymous namespace

const erhe::property::Enum_info c_draw_mode_enum_info                {"Draw_mode",                 c_draw_mode_entries};
const erhe::property::Enum_info c_draw_mode_card_geometry_enum_info  {"Draw_mode_card_geometry",   c_draw_mode_card_geometry_entries};
const erhe::property::Enum_info c_draw_mode_card_visibility_enum_info{"Draw_mode_card_visibility", c_draw_mode_card_visibility_entries};

auto Draw_mode_description::has_authored_value() const -> bool
{
    if (
        draw_mode_authored       || apply_draw_mode_authored  ||
        card_geometry_authored   || card_visibility_authored  ||
        draw_mode_color_authored || extents_hint_authored
    ) {
        return true;
    }
    return std::any_of(
        card_textures.begin(),
        card_textures.end(),
        [](const std::string& path) { return !path.empty(); }
    );
}

} // namespace erhe::scene
