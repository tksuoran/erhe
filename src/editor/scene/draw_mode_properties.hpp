#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/draw_mode_description.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <optional>
#include <vector>

namespace erhe { class Hierarchy; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

// The effective `UsdGeomModelAPI` values of one prim, read once per use
// (doc/erhe/property_system.md section 4.24). Trivially copyable: a per-frame
// reader takes one of these and allocates nothing. The card texture of a face
// is not in the record, because resolving it against the file that authored it
// costs a path (resolve_card_texture_path).
class Draw_mode_data
{
public:
    // The prim's own opinion, which may be `inherited`.
    erhe::scene::Draw_mode                 draw_mode               {erhe::scene::Draw_mode::inherited};
    // What the prim resolves to: its own opinion when that is not
    // `inherited`, else the nearest ancestor prim's non-inherited one, else
    // `default_`. Never `inherited`.
    erhe::scene::Draw_mode                 resolved_draw_mode      {erhe::scene::Draw_mode::default_};
    erhe::scene::Draw_mode_card_geometry   card_geometry           {erhe::scene::Draw_mode_card_geometry::cross};
    erhe::scene::Draw_mode_card_visibility card_visibility         {erhe::scene::Draw_mode_card_visibility::inherited};
    // Resolved the same way, with the root fallback `full`.
    erhe::scene::Draw_mode_card_visibility resolved_card_visibility{erhe::scene::Draw_mode_card_visibility::full};
    glm::vec3                              draw_mode_color         {0.18f, 0.18f, 0.18f};
    glm::vec3                              extents_hint_min        {0.0f};
    glm::vec3                              extents_hint_max        {0.0f};
    bool                                   has_extents_hint        {false};
};

// `UsdGeomModelAPI` as a value group of the prim itself
// (doc/erhe/property_system.md sections 4.24 and 4.23): the request that a model
// prim's subtree be drawn as a proxy instead of by itself
// (doc/erhe/usd_compatibility.md, "Draw modes").
//
// `Draw_mode` is a registration holder with static members only, not an item
// and not a Dependency_object: it owns the property registrations (owner type
// `Draw_mode`, so the qualified names are `Draw_mode.draw_mode` ..
// `Draw_mode.extents_hint_max`, which is the name a file's opinion of one
// addresses it by) and the holder of every value is an `erhe::scene::Node`.
//
// `apply_draw_mode` is the group's KEY property (D1): the prim carries a draw
// mode exactly while it is true, which is the erhe form of `GeomModelAPI`
// being applied to the prim. The runtime state the group implies - the
// pruning of the prim's children, the cached extent and the card proxy - is
// owned by `Draw_mode_system`, one per scene.
class Draw_mode
{
public:
    Draw_mode() = delete;

    // The registering class's owner type id. Draw_mode has no instances, so
    // it is allocated directly under the root rather than by Item<>.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property, registered first (D1). Its default is false, so a
    // prim carries the feature exactly while something - a local value, a
    // style, the reference layer of an instance - says true.
    static const erhe::property::Property<bool>                                   apply_draw_mode_property;

    // The rest of the `UsdGeomModelAPI` attributes, one property each, named
    // exactly as the mapping table names them. None of them inherits: a
    // value is the prim's own opinion, and `Draw_mode::inherited` is USD's
    // own deferral token, resolved by the ancestor walk of read_draw_mode().
    static const erhe::property::Property<erhe::scene::Draw_mode>                 draw_mode_property;
    static const erhe::property::Property<erhe::scene::Draw_mode_card_geometry>   card_geometry_property;
    static const erhe::property::Property<erhe::scene::Draw_mode_card_visibility> card_visibility_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_x_neg_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_x_pos_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_y_neg_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_y_pos_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_z_neg_property;
    static const erhe::property::Property<erhe::property::Asset_path>             card_texture_z_pos_property;
    static const erhe::property::Property<glm::vec3>                              draw_mode_color_property;
    static const erhe::property::Property<glm::vec3>                              extents_hint_min_property;
    static const erhe::property::Property<glm::vec3>                              extents_hint_max_property;

    // The directory a relative card-texture path of this prim is resolved
    // against: the directory of the file that authored the value. Session
    // state, so it is registered without the serialize flag (D5); an
    // instance reads the template's through the reference layer, which is
    // where the variant block that spelled the relative path lives.
    static const erhe::property::Property<std::string>                            source_directory_property;

    // The card-texture property of one face, in the record's face order.
    [[nodiscard]] static auto get_card_texture_property(erhe::scene::Draw_mode_card_face face)
        -> const erhe::property::Property<erhe::property::Asset_path>&;

    // Every Draw_mode.* property, registration order, for generic walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// True while the node carries the group: the key property's effective value
// differs from its default (erhe::property::carries_attached_group).
[[nodiscard]] auto carries_draw_mode(const erhe::scene::Node& node) -> bool;

// The effective values of one node, or nothing when the node carries no draw
// mode.
[[nodiscard]] auto read_draw_mode(const erhe::scene::Node& node) -> std::optional<Draw_mode_data>;

// The image of one card face as a path that can be opened: the value as it
// stands when it is absolute, and otherwise resolved against the node's
// source directory. Empty when the face names no image.
[[nodiscard]] auto resolve_card_texture_path(const erhe::scene::Node& node, erhe::scene::Draw_mode_card_face face)
    -> std::filesystem::path;

// Tells every node of the loaded tree that carries a draw mode and has no
// source directory yet which file it came out of. An instance whose values
// come from a template already reads the template's, so nothing is written
// on it.
void set_draw_mode_source_directory(erhe::Hierarchy& root, const std::filesystem::path& file_path);

// The record the node holds, with each value's authored flag being whether
// the node holds it as a local value (D32). This is what a save writes.
[[nodiscard]] auto get_draw_mode_description(const erhe::scene::Node& node) -> erhe::scene::Draw_mode_description;

// Takes the record's values as local values, one per authored flag; the
// unauthored ones are left unset, so a reload authors exactly what the file
// spelled. The key property is set unconditionally: the record exists because
// the file applied `GeomModelAPI` to the prim or authored one of its
// attributes, and that is what the key states. Used by the importers.
void set_draw_mode_description(erhe::scene::Node& node, const erhe::scene::Draw_mode_description& description);

} // namespace editor
