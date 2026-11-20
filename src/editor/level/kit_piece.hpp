#pragma once

#include "level/kit_connector.hpp"

#include "erhe_property/dependency_property.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

[[nodiscard]] auto carries_kit_piece(const erhe::scene::Node& node) -> bool;

[[nodiscard]] auto read_kit_piece(const erhe::scene::Node& node) -> std::optional<Kit_piece_data>;

void write_kit_piece(erhe::scene::Node& node, const Kit_piece_data& data);

// The piece's own connector switches, the defaults (all enabled) for a node without values; its layout's hiding (U39) is
// added by Level_kits::get_connector_switches().
[[nodiscard]] auto read_piece_connector_switches(const erhe::scene::Node& node) -> Kit_connector_switches;

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

} // namespace editor
