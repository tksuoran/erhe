#pragma once

#include "erhe_property/dependency_property.hpp"

#include <optional>
#include <string>
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
// sections 4.11 and 4.23), qualified Kit_piece.shape and Kit_piece.parameters; both are saved.
class Kit_piece
{
public:
    Kit_piece() = delete;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property: the node is a piece while it names a shape.
    static const erhe::property::Property<std::string> shape_property;
    static const erhe::property::Property<std::string> parameters_property;

    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

[[nodiscard]] auto carries_kit_piece(const erhe::scene::Node& node) -> bool;

[[nodiscard]] auto read_kit_piece(const erhe::scene::Node& node) -> std::optional<Kit_piece_data>;

void write_kit_piece(erhe::scene::Node& node, const Kit_piece_data& data);

} // namespace editor
