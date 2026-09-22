#include "scene/draw_mode_properties.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_system.hpp"

namespace editor {

using erhe::property::Asset_path;
using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Property;
using erhe::property::Property_changed_args;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Value_source;
using erhe::scene::Draw_mode_card_face;
using erhe::scene::Draw_mode_card_geometry;
using erhe::scene::Draw_mode_card_visibility;
using erhe::scene::Draw_mode_description;
using erhe::scene::c_draw_mode_card_face_count;

namespace {

constexpr std::string_view c_group  = "Draw Mode";
constexpr std::string_view c_cards  = "Cards";
constexpr std::string_view c_extent = "Extents Hint";

// A value of the group implies the prim carries the feature: in USD the
// `model:` attributes exist because `GeomModelAPI` is applied to the prim, so
// a file - or a user - that authors one has applied it, and the key property
// is the erhe form of that application
// (doc/plans/node_attachments_to_properties.md D9).
//
// Delivered callbacks may write values (Dependency_object::deliver), and the
// key property's own callback is node_system_property_changed alone, so the
// write here recurses exactly one level.
void draw_mode_value_changed(Dependency_object& object, const Property_changed_args& args)
{
    erhe::scene::Node* const node = dynamic_cast<erhe::scene::Node*>(&object);
    if (
        (node != nullptr) &&
        (args.new_source == Value_source::local) &&
        !carries_draw_mode(*node)
    ) {
        node->set_value(Draw_mode::apply_draw_mode_property, true);
    }
    erhe::scene::node_system_property_changed(object, args);
}

} // anonymous namespace

auto Draw_mode::property_owner_type() -> erhe::property::Owner_type
{
    // Draw_mode is not a Dependency_object, so there is no Item<> to allocate
    // the id: the registering class's own id sits directly under the root and
    // serves only to qualify the names (Draw_mode.draw_mode).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Draw_mode"
    );
    return s_id;
}

// The key property comes first, so its registration is complete when the rest
// of the group takes attached_group_visible_when on it (D1).
const Property<bool> Draw_mode::apply_draw_mode_property = Property<bool>::register_attached(
    "apply_draw_mode", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = false,
        .property_changed = erhe::scene::node_system_property_changed,
        .ui               = Property_ui{
            .group   = c_group,
            .tooltip = "This prim asks for a draw mode - the erhe form of GeomModelAPI being applied to it. The mode applies without it being true in the file, the way the imaging adapter honours drawMode on every model prim",
            .label   = "Apply Draw Mode"
        }
    }
);

namespace {

// Every non-key value of the group is listed exactly on the prims carrying it
// (D1), and a change of any of them reaches the scene's draw-mode system.
[[nodiscard]] auto group_ui(
    const std::string_view group,
    const std::string_view label,
    const std::string_view tooltip
) -> Property_ui
{
    return Property_ui{
        .group        = group,
        .tooltip      = tooltip,
        .label        = label,
        .visible_when = erhe::property::attached_group_visible_when(Draw_mode::apply_draw_mode_property.get())
    };
}

// The card rows are of use only while the prim asks for cards.
[[nodiscard]] auto is_cards_prim(const Dependency_object& object) -> bool
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&object);
    if (node == nullptr) {
        return false;
    }
    const std::optional<Draw_mode_data> data = read_draw_mode(*node);
    return data.has_value() && (data.value().resolved_draw_mode == erhe::scene::Draw_mode::cards);
}

[[nodiscard]] auto cards_ui(const std::string_view label, const std::string_view tooltip) -> Property_ui
{
    return Property_ui{
        .group        = c_cards,
        .tooltip      = tooltip,
        .label        = label,
        .visible_when = erhe::property::attached_group_visible_when(Draw_mode::apply_draw_mode_property.get(), is_cards_prim)
    };
}

} // anonymous namespace

const Property<erhe::scene::Draw_mode> Draw_mode::draw_mode_property = Property<erhe::scene::Draw_mode>::register_attached(
    "draw_mode", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), erhe::scene::c_draw_mode_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(erhe::scene::Draw_mode::inherited),
        .property_changed = draw_mode_value_changed,
        .ui               = group_ui(c_group, "Draw Mode", "How this prim's subtree is drawn; 'inherited' defers to the nearest ancestor asking for one")
    }
);
const Property<Draw_mode_card_geometry> Draw_mode::card_geometry_property = Property<Draw_mode_card_geometry>::register_attached(
    "card_geometry", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), erhe::scene::c_draw_mode_card_geometry_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Draw_mode_card_geometry::cross),
        .property_changed = draw_mode_value_changed,
        .ui               = cards_ui("Card Geometry", "Which quads a 'cards' draw mode generates")
    }
);
const Property<Draw_mode_card_visibility> Draw_mode::card_visibility_property = Property<Draw_mode_card_visibility>::register_attached(
    "card_visibility", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), erhe::scene::c_draw_mode_card_visibility_enum_info,
    Property_metadata{
        .default_value    = erhe::property::make_value(Draw_mode_card_visibility::inherited),
        .property_changed = draw_mode_value_changed,
        .ui               = cards_ui("Card Visibility", "'simple' leaves out the cards of the stage's up axis")
    }
);

namespace {

constexpr std::string_view c_card_texture_tooltip =
    "The image of this card face; an empty path draws the face in the draw-mode color";

[[nodiscard]] auto card_texture_metadata(const std::string_view label) -> Property_metadata
{
    return Property_metadata{
        .default_value    = Asset_path{},
        .property_changed = draw_mode_value_changed,
        .ui               = cards_ui(label, c_card_texture_tooltip)
    };
}

} // anonymous namespace

const Property<Asset_path> Draw_mode::card_texture_x_neg_property = Property<Asset_path>::register_attached(
    "card_texture_x_neg", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), card_texture_metadata("Card Texture X-")
);
const Property<Asset_path> Draw_mode::card_texture_x_pos_property = Property<Asset_path>::register_attached(
    "card_texture_x_pos", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), card_texture_metadata("Card Texture X+")
);
const Property<Asset_path> Draw_mode::card_texture_y_neg_property = Property<Asset_path>::register_attached(
    "card_texture_y_neg", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), card_texture_metadata("Card Texture Y-")
);
const Property<Asset_path> Draw_mode::card_texture_y_pos_property = Property<Asset_path>::register_attached(
    "card_texture_y_pos", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), card_texture_metadata("Card Texture Y+")
);
const Property<Asset_path> Draw_mode::card_texture_z_neg_property = Property<Asset_path>::register_attached(
    "card_texture_z_neg", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), card_texture_metadata("Card Texture Z-")
);
const Property<Asset_path> Draw_mode::card_texture_z_pos_property = Property<Asset_path>::register_attached(
    "card_texture_z_pos", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(), card_texture_metadata("Card Texture Z+")
);
const Property<glm::vec3> Draw_mode::draw_mode_color_property = Property<glm::vec3>::register_attached(
    "draw_mode_color", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.18f, 0.18f, 0.18f},
        .property_changed = draw_mode_value_changed,
        .ui               = Property_ui{
            .presentation = Property_ui::Presentation::color,
            .group        = c_group,
            .tooltip      = "The line color of 'origin' and 'bounds', and the fallback card color",
            .label        = "Draw Mode Color",
            .visible_when = erhe::property::attached_group_visible_when(Draw_mode::apply_draw_mode_property.get())
        }
    }
);
const Property<glm::vec3> Draw_mode::extents_hint_min_property = Property<glm::vec3>::register_attached(
    "extents_hint_min", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.0f},
        .property_changed = draw_mode_value_changed,
        .ui               = Property_ui{
            .step         = 0.01f,
            .group        = c_extent,
            .tooltip      = "The model bounds the proxies are sized from, in this prim's space; without one the bounds of the meshes below are used",
            .label        = "Minimum",
            .visible_when = erhe::property::attached_group_visible_when(Draw_mode::apply_draw_mode_property.get())
        }
    }
);
const Property<glm::vec3> Draw_mode::extents_hint_max_property = Property<glm::vec3>::register_attached(
    "extents_hint_max", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = glm::vec3{0.0f},
        .property_changed = draw_mode_value_changed,
        .ui               = Property_ui{
            .step         = 0.01f,
            .group        = c_extent,
            .label        = "Maximum",
            .visible_when = erhe::property::attached_group_visible_when(Draw_mode::apply_draw_mode_property.get())
        }
    }
);
// Session state (D5): which file spelled the relative card-texture paths of
// this prim. It is never written to a file - a save states the paths relative
// to the file it writes - so it carries no serialize flag.
const Property<std::string> Draw_mode::source_directory_property = Property<std::string>::register_attached(
    "source_directory", Draw_mode::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value = std::string{},
        .flags         = Property_flags::none,
        .ui            = Property_ui{
            .group          = c_cards,
            .tooltip        = "Directory a relative card-texture path of this prim is resolved against; session state, never saved",
            .developer_only = true,
            .label          = "Source Directory",
            .visible_when   = erhe::property::attached_group_visible_when(Draw_mode::apply_draw_mode_property.get())
        }
    }
);

auto Draw_mode::get_card_texture_property(const Draw_mode_card_face face) -> const Property<Asset_path>&
{
    switch (face) {
        case Draw_mode_card_face::x_neg: return card_texture_x_neg_property;
        case Draw_mode_card_face::x_pos: return card_texture_x_pos_property;
        case Draw_mode_card_face::y_neg: return card_texture_y_neg_property;
        case Draw_mode_card_face::y_pos: return card_texture_y_pos_property;
        case Draw_mode_card_face::z_neg: return card_texture_z_neg_property;
        case Draw_mode_card_face::z_pos: return card_texture_z_pos_property;
        default                        : return card_texture_x_neg_property;
    }
}

auto Draw_mode::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        apply_draw_mode_property   .get_ptr(),
        draw_mode_property         .get_ptr(),
        card_geometry_property     .get_ptr(),
        card_visibility_property   .get_ptr(),
        card_texture_x_neg_property.get_ptr(),
        card_texture_x_pos_property.get_ptr(),
        card_texture_y_neg_property.get_ptr(),
        card_texture_y_pos_property.get_ptr(),
        card_texture_z_neg_property.get_ptr(),
        card_texture_z_pos_property.get_ptr(),
        draw_mode_color_property   .get_ptr(),
        extents_hint_min_property  .get_ptr(),
        extents_hint_max_property  .get_ptr(),
        source_directory_property  .get_ptr()
    };
    return s_properties;
}

auto carries_draw_mode(const erhe::scene::Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Draw_mode::apply_draw_mode_property.get());
}

namespace {

// The nearest ancestor prim carrying a draw mode whose own opinion is not
// `inherited`, which is how USD's `inherited` token defers up the namespace.
template <typename T>
[[nodiscard]] auto resolve_up(
    const erhe::scene::Node&                 node,
    const erhe::property::Property<T>&       property,
    const T                                  deferral,
    const T                                  root_fallback
) -> T
{
    const T own = node.get_value(property);
    if (own != deferral) {
        return own;
    }
    std::shared_ptr<erhe::scene::Xformable> ancestor = node.get_parent_node();
    while (ancestor) {
        if (carries_draw_mode(*ancestor.get())) {
            const T value = ancestor->get_value(property);
            if (value != deferral) {
                return value;
            }
        }
        ancestor = ancestor->get_parent_node();
    }
    return root_fallback;
}

} // anonymous namespace

auto read_draw_mode(const erhe::scene::Node& node) -> std::optional<Draw_mode_data>
{
    if (!carries_draw_mode(node)) {
        return {};
    }
    Draw_mode_data data{};
    data.draw_mode                = node.get_value(Draw_mode::draw_mode_property);
    data.resolved_draw_mode       = resolve_up(
        node, Draw_mode::draw_mode_property, erhe::scene::Draw_mode::inherited, erhe::scene::Draw_mode::default_
    );
    data.card_geometry            = node.get_value(Draw_mode::card_geometry_property);
    data.card_visibility          = node.get_value(Draw_mode::card_visibility_property);
    data.resolved_card_visibility = resolve_up(
        node, Draw_mode::card_visibility_property, Draw_mode_card_visibility::inherited, Draw_mode_card_visibility::full
    );
    data.draw_mode_color          = node.get_value(Draw_mode::draw_mode_color_property);
    data.extents_hint_min         = node.get_value(Draw_mode::extents_hint_min_property);
    data.extents_hint_max         = node.get_value(Draw_mode::extents_hint_max_property);
    data.has_extents_hint         =
        (node.get_value_source(Draw_mode::extents_hint_min_property) != Value_source::default_value) ||
        (node.get_value_source(Draw_mode::extents_hint_max_property) != Value_source::default_value);
    return data;
}

auto resolve_card_texture_path(const erhe::scene::Node& node, const Draw_mode_card_face face) -> std::filesystem::path
{
    const Asset_path value = node.get_value(Draw_mode::get_card_texture_property(face));
    if (value.path.empty()) {
        return std::filesystem::path{};
    }
    std::filesystem::path path{value.path};
    if (!path.is_relative()) {
        return path;
    }
    // The directory of the file that authored the value. It travels with the
    // value: a node reading the texture through the reference layer has no
    // source directory of its own either, so both come from the template,
    // which is the file whose variant block spelled the relative path.
    const std::string directory = node.get_value(Draw_mode::source_directory_property);
    if (directory.empty()) {
        return path;
    }
    return (std::filesystem::path{directory} / path).lexically_normal();
}

void set_draw_mode_source_directory(erhe::Hierarchy& root, const std::filesystem::path& file_path)
{
    const std::string directory = file_path.parent_path().generic_string();
    root.for_each<erhe::scene::Xformable>(
        [&directory](erhe::scene::Xformable& prim) -> bool {
            if (carries_draw_mode(prim) && prim.get_value(Draw_mode::source_directory_property).empty()) {
                prim.set_value(Draw_mode::source_directory_property, directory);
            }
            return true;
        }
    );
}

auto get_draw_mode_description(const erhe::scene::Node& node) -> Draw_mode_description
{
    Draw_mode_description description{};
    description.draw_mode                = node.get_value(Draw_mode::draw_mode_property);
    description.draw_mode_authored       = node.has_local_value(Draw_mode::draw_mode_property.get());
    description.apply_draw_mode          = node.get_value(Draw_mode::apply_draw_mode_property);
    description.apply_draw_mode_authored = node.has_local_value(Draw_mode::apply_draw_mode_property.get());
    description.card_geometry            = node.get_value(Draw_mode::card_geometry_property);
    description.card_geometry_authored   = node.has_local_value(Draw_mode::card_geometry_property.get());
    description.card_visibility          = node.get_value(Draw_mode::card_visibility_property);
    description.card_visibility_authored = node.has_local_value(Draw_mode::card_visibility_property.get());
    for (std::size_t i = 0; i < c_draw_mode_card_face_count; ++i) {
        const Draw_mode_card_face face = static_cast<Draw_mode_card_face>(i);
        // A card texture is stated the way every other value is (D32): only
        // what this node holds locally is what the prim authored. One the
        // reference layer supplies is the template's opinion, written where
        // the template writes it. The path is stated resolved, so the writer
        // names the image relative to the file it writes whatever spelling
        // the value arrived in.
        if (!node.has_local_value(Draw_mode::get_card_texture_property(face).get())) {
            continue;
        }
        description.card_textures[i] = resolve_card_texture_path(node, face).generic_string();
    }
    description.draw_mode_color          = node.get_value(Draw_mode::draw_mode_color_property);
    description.draw_mode_color_authored = node.has_local_value(Draw_mode::draw_mode_color_property.get());
    description.extents_hint_min         = node.get_value(Draw_mode::extents_hint_min_property);
    description.extents_hint_max         = node.get_value(Draw_mode::extents_hint_max_property);
    description.extents_hint_authored    =
        node.has_local_value(Draw_mode::extents_hint_min_property.get()) ||
        node.has_local_value(Draw_mode::extents_hint_max_property.get());
    return description;
}

void set_draw_mode_description(erhe::scene::Node& node, const Draw_mode_description& description)
{
    // The record exists because the file applied GeomModelAPI to the prim or
    // authored one of its attributes; the key property is the erhe form of
    // that, so it is authored whatever the file said of `model:applyDrawMode`.
    node.set_value(Draw_mode::apply_draw_mode_property, true);
    if (description.draw_mode_authored) {
        node.set_value(Draw_mode::draw_mode_property, description.draw_mode);
    }
    if (description.card_geometry_authored) {
        node.set_value(Draw_mode::card_geometry_property, description.card_geometry);
    }
    if (description.card_visibility_authored) {
        node.set_value(Draw_mode::card_visibility_property, description.card_visibility);
    }
    for (std::size_t i = 0; i < c_draw_mode_card_face_count; ++i) {
        if (description.card_textures[i].empty()) {
            continue;
        }
        const Draw_mode_card_face face = static_cast<Draw_mode_card_face>(i);
        node.set_value(Draw_mode::get_card_texture_property(face), Asset_path{description.card_textures[i]});
    }
    if (description.draw_mode_color_authored) {
        node.set_value(Draw_mode::draw_mode_color_property, description.draw_mode_color);
    }
    if (description.extents_hint_authored) {
        node.set_value(Draw_mode::extents_hint_min_property, description.extents_hint_min);
        node.set_value(Draw_mode::extents_hint_max_property, description.extents_hint_max);
    }
}

} // namespace editor
