#pragma once

#include "level/kit_connector.hpp"
#include "level/surface_options.hpp"

#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe         { class Hierarchy; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

// The kit record of one piece: its shape id and the canonical text of its parameter values (D1.3).
class Kit_piece_data
{
public:
    std::string shape;
    std::string parameters;
};

// The kit record as an attached value group of the piece's node (doc/erhe/property_system.md
// sections 4.11 and 4.23), qualified Kit_piece.<name>; every value is saved.
class Kit_piece
{
public:
    Kit_piece() = delete;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property: the node is a piece while it names a shape.
    static const erhe::property::Property<std::string> shape_property;
    static const erhe::property::Property<std::string> parameters_property;
    // The connector switches (U33): all on by default, so a node without values has every connector enabled.
    static const erhe::property::Property<bool>        connectors_enabled_property;
    static const erhe::property::Property<std::string> disabled_connectors_property;
    // The persistent connector join (R5.3, plan step 41): make_attachment_text(); empty when not attached.
    static const erhe::property::Property<std::string> attachment_property;
    // The surface attachment (doc/editor/level_tools.md "Surface attachments"): make_surface_attachment_text(); empty when none.
    static const erhe::property::Property<std::string> surface_attachment_property;
    // The foliage item's radius in meters (doc/editor/level_tools.md "Foliage paint"); outside the mesh key.
    static const erhe::property::Property<float>       foliage_radius_property;

    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// The largest Kit_piece.foliage_radius the property's validation takes, in meters.
inline constexpr float c_foliage_radius_max = 100.0f;

[[nodiscard]] auto carries_kit_piece(const erhe::scene::Node& node) -> bool;

[[nodiscard]] auto read_kit_piece(const erhe::scene::Node& node) -> std::optional<Kit_piece_data>;

void write_kit_piece(erhe::scene::Node& node, const Kit_piece_data& data);

// The piece's own connector switches, the defaults (all enabled) for a node without values; its layout's hiding (U39) is
// added by Level_kits::get_connector_switches().
[[nodiscard]] auto read_piece_connector_switches(const erhe::scene::Node& node) -> Kit_connector_switches;

// The node is a mesh carrying Editor_item_flags::foliage.
[[nodiscard]] auto is_foliage_mesh(const erhe::scene::Node& node) -> bool;

// True for Kit_piece.connectors_enabled and Kit_piece.disabled_connectors.
[[nodiscard]] auto is_connector_switch_property(const erhe::property::Dependency_property& property) -> bool;

// A persistent connector join (R5.3, doc/editor/level_tools.md "Attachments"): the piece's connector stays on the
// target's; targets are named by glTF uid, so the record survives a save and a load.
class Kit_attachment
{
public:
    std::string owner;            // the uid of the node the attachment was made on; a copy of the record on another node is inert
    std::string target;           // the uid of the target piece
    std::string connector;        // of the attached piece
    std::string target_connector;
    int         twist_steps{0};   // 0 - 3
    float       gap        {0.0f};

    [[nodiscard]] auto operator==(const Kit_attachment& other) const -> bool = default;
};

// A Transform tool drag released connector-snapped (step 41): the moved piece's connector and the target's.
class Kit_snap_pair
{
public:
    std::shared_ptr<erhe::scene::Node> piece;
    std::string                        connector;
    std::shared_ptr<erhe::scene::Node> target;
    std::string                        target_connector;
};

// The canonical text of Kit_piece.attachment: a JSON object with sorted keys, the gap a hexadecimal float.
[[nodiscard]] auto make_attachment_text(const Kit_attachment& attachment) -> std::string;
// Returns the problem, empty when the text reads.
[[nodiscard]] auto parse_attachment_text(std::string_view text, Kit_attachment& out_attachment) -> std::string;

// The node's own attachment from its local value: none when it has no record, the record does not read or names another owner.
// With several connections (K6.1), the first by connector name.
[[nodiscard]] auto read_kit_attachment(const erhe::scene::Node& node) -> std::optional<Kit_attachment>;

// K6.1: the connections a piece records, one per connector of the piece: one object (the form of one) or an array sorted by
// connector; empty text for none.
[[nodiscard]] auto make_attachments_text(std::vector<Kit_attachment> attachments) -> std::string;
[[nodiscard]] auto parse_attachments_text(std::string_view text, std::vector<Kit_attachment>& out_attachments) -> std::string;

// The node's own connections (entries naming another owner dropped); empty when the record does not read. Allocates.
[[nodiscard]] auto read_kit_attachments(const erhe::scene::Node& node) -> std::vector<Kit_attachment>;

// The place of a piece on the piece it rests on, its support and parent (doc/editor/level_tools.md "Surface attachments").
class Kit_surface_attachment
{
public:
    std::string       target;                                   // the glTF uid of the support
    glm::dvec3        position {0.0};                           // the rest point: 0 at the support's bounds minimum, 1 at its maximum
    glm::vec3         direction{c_default_surface_direction};   // unit search direction in the support's node space
    Surface_orient    orient   {Surface_orient::off};
    Surface_snap_rest rest     {Surface_snap_rest::leading_face};
    bool              on_surface{true}; // S3.4: false after a carry whose rest again found no face of the support

    [[nodiscard]] auto operator==(const Kit_surface_attachment& other) const -> bool = default;
};

// The canonical text of Kit_piece.surface_attachment: a JSON object with sorted keys, the floats hexadecimal.
[[nodiscard]] auto make_surface_attachment_text(const Kit_surface_attachment& attachment) -> std::string;
// Returns the problem, empty when the text reads.
[[nodiscard]] auto parse_surface_attachment_text(std::string_view text, Kit_surface_attachment& out_attachment) -> std::string;

// The node's record from its local value: none when it has none. A record that does not read sets out_problem.
[[nodiscard]] auto read_kit_surface_attachment(const erhe::scene::Node& node, std::string* out_problem = nullptr) -> std::optional<Kit_surface_attachment>;

// S1.2, S5.1: the node's record names its parent, a mesh node: it is attached there (active or off_surface), the author's
// piece, and the lock of a locked kit layout node above ends at it.
[[nodiscard]] auto is_surface_attached_to_parent(const erhe::scene::Node& node) -> bool;

// S1.3: in a copy of a subtree (the same structure as the source, not yet inserted), a surface attachment record naming a
// node of the source names that node's copy, so the copy is attached as its source is.
void remap_copied_surface_attachments(const erhe::Hierarchy& source, erhe::Hierarchy& copy);

} // namespace editor
