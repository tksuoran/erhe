#pragma once

// Format-neutral, plain-data description of the draw mode of one prim: the
// `UsdGeomModelAPI` attributes that ask the imaging layer to draw a subtree as
// a proxy (doc/erhe/usd_compatibility.md, "Draw modes"). The USD reader fills it
// from the schema and the USD writer authors it back; the editor turns each
// record into the `Draw_mode` attachment of the prim it names.
//
// The vocabulary is USD's own: every enumerator's label is the schema token
// the file spells, so a value travels as that token wherever it travels as
// text - an override opinion, a property row, a written attribute.
//
// This header stays data-only: glm + std types plus the enumerator tables the
// property registration of the attachment uses.

#include "erhe_property/enum_info.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace erhe::scene {

// `model:drawMode`. `inherited` defers to the nearest ancestor that authors
// one; the fallback of a hierarchy that authors none is `default_`.
enum class Draw_mode : int {
    inherited = 0,
    default_,
    origin,
    bounds,
    cards
};

// `model:cardGeometry`: which quads `cards` generates.
enum class Draw_mode_card_geometry : int {
    cross = 0,
    box,
    from_texture
};

// `model:cardVisibility`: `simple` leaves out the cards of the stage's up
// axis, `inherited` defers to the nearest ancestor and behaves as `full`
// when no ancestor authors one.
enum class Draw_mode_card_visibility : int {
    inherited = 0,
    full,
    simple
};

// The six card faces, in the order `Draw_mode_description::card_textures`
// holds them.
enum class Draw_mode_card_face : int {
    x_neg = 0,
    x_pos,
    y_neg,
    y_pos,
    z_neg,
    z_pos
};

inline constexpr std::size_t c_draw_mode_card_face_count = 6;

[[nodiscard]] auto c_str(Draw_mode value) -> const char*;
[[nodiscard]] auto c_str(Draw_mode_card_geometry value) -> const char*;
[[nodiscard]] auto c_str(Draw_mode_card_visibility value) -> const char*;

// The erhe property name of one card texture (`card_texture_x_neg`, ...),
// which is also the suffix the override spelling uses.
[[nodiscard]] auto c_str(Draw_mode_card_face value) -> const char*;

// The enumerator the token names, false when the token names none.
[[nodiscard]] auto draw_mode_from_string(std::string_view text, Draw_mode& out_value) -> bool;
[[nodiscard]] auto draw_mode_card_geometry_from_string(std::string_view text, Draw_mode_card_geometry& out_value) -> bool;
[[nodiscard]] auto draw_mode_card_visibility_from_string(std::string_view text, Draw_mode_card_visibility& out_value) -> bool;
[[nodiscard]] auto draw_mode_card_face_from_string(std::string_view text, Draw_mode_card_face& out_value) -> bool;

extern const erhe::property::Enum_info c_draw_mode_enum_info;
extern const erhe::property::Enum_info c_draw_mode_card_geometry_enum_info;
extern const erhe::property::Enum_info c_draw_mode_card_visibility_enum_info;

// The `UsdGeomModelAPI` of one prim. Every `*_authored` flag says whether the
// file spelled the value at all: USD's schema fallbacks are not erhe's
// defaults to author, so a writer states exactly what was authored and leaves
// the rest to the schema (D32).
class Draw_mode_description
{
public:
    Draw_mode                 draw_mode      {Draw_mode::inherited};
    bool                      draw_mode_authored{false};
    bool                      apply_draw_mode{false};
    bool                      apply_draw_mode_authored{false};
    Draw_mode_card_geometry   card_geometry  {Draw_mode_card_geometry::cross};
    bool                      card_geometry_authored{false};
    Draw_mode_card_visibility card_visibility{Draw_mode_card_visibility::inherited};
    bool                      card_visibility_authored{false};
    // Resolved absolute file paths of `model:cardTexture{X,Y,Z}{Neg,Pos}`,
    // indexed by Draw_mode_card_face. An empty entry is an unauthored face.
    std::array<std::string, c_draw_mode_card_face_count> card_textures{};
    glm::vec3                 draw_mode_color{0.18f, 0.18f, 0.18f};
    bool                      draw_mode_color_authored{false};
    // `extentsHint`, the model bounds the proxies are sized from: the first
    // pair of the `float3[]`, which is the default purpose's.
    glm::vec3                 extents_hint_min{0.0f, 0.0f, 0.0f};
    glm::vec3                 extents_hint_max{0.0f, 0.0f, 0.0f};
    bool                      extents_hint_authored{false};

    // Whether the record says anything at all: a prim that applies the schema
    // and authors nothing has one of these with every flag clear.
    [[nodiscard]] auto has_authored_value() const -> bool;
};

} // namespace erhe::scene
