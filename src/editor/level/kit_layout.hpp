#pragma once

#include "level/kit_layout_values.hpp"
#include "level/level_queries.hpp"

#include "erhe_property/dependency_property.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe              { class Hierarchy; class Item_base; }
namespace erhe::property    { class Dependency_property; }
namespace erhe::scene       { class Xformable; using Node = Xformable; }

namespace editor {

// Whether each element takes its item's seed or a seed of its own (R5.12.1).
enum class Kit_layout_seeds : unsigned int {
    item = 0,
    per_element // derived from the layout seed and the element index
};

// Who edits the children (R5.12.2 - R5.12.4).
enum class Kit_layout_lock : unsigned int {
    unlocked = 0, // the children are edited directly
    locked        // the layout makes its children; they are not edited directly
};

// The values of one kit layout node (D3.9).
class Kit_layout_data
{
public:
    Kit_layout_pattern           pattern;
    std::vector<Kit_layout_item> items;
    Kit_snap                     snap          {Kit_snap::none};
    Surface_snap                 surface_snap  {Surface_snap::off};
    Surface_orient               surface_orient{Surface_orient::off};
    Kit_layout_seeds             seeds         {Kit_layout_seeds::item};
    int                          seed          {0};
    Kit_layout_lock              lock          {Kit_layout_lock::locked};
};

// The attached value group of a kit layout node (doc/erhe/property_system.md sections 4.11 and 4.23), qualified
// Kit_layout.<name>; every value is saved with the node.
class Kit_layout
{
public:
    Kit_layout() = delete;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property: the node is a kit layout node while it holds a pattern text.
    static const erhe::property::Property<std::string> pattern_property;
    static const erhe::property::Property<std::string> items_property;
    static const erhe::property::Property<bool>        module_snap_property;
    static const erhe::property::Property<bool>        surface_snap_property;
    static const erhe::property::Property<bool>        surface_orient_property;
    static const erhe::property::Property<bool>        random_seeds_property;
    static const erhe::property::Property<int>         seed_property;
    static const erhe::property::Property<bool>        locked_property;
};

[[nodiscard]] auto carries_kit_layout(const erhe::scene::Node& node) -> bool;

// Reads the node's values; returns the problem (not a layout node, a text that does not read), empty when read.
[[nodiscard]] auto read_kit_layout(const erhe::scene::Node& node, Kit_layout_data& out_data) -> std::string;

// Writes every value on the node without an operation (a node not yet in a scene).
void write_kit_layout(erhe::scene::Node& node, const Kit_layout_data& data);

[[nodiscard]] auto is_locked_kit_layout(const erhe::scene::Node& node) -> bool;

// The outermost locked kit layout node strictly above the item; null when there is none.
[[nodiscard]] auto find_locking_kit_layout(const erhe::Item_base& item) -> std::shared_ptr<erhe::scene::Node>;

// The reason a direct edit of the item is refused: it is below a locked kit layout node (R5.12.3).
[[nodiscard]] auto locked_layout_refusal(const erhe::Item_base& item) -> std::optional<std::string>;

// The reason nothing may be added under the parent: it is a locked kit layout node or below one.
[[nodiscard]] auto locked_layout_child_refusal(const erhe::Hierarchy& parent) -> std::optional<std::string>;

// The refusal of the first of the items below a locked kit layout node; nothing when none is.
template <typename T>
[[nodiscard]] auto locked_layout_refusal_of_any(std::span<const std::shared_ptr<T>> items) -> std::optional<std::string>
{
    for (const std::shared_ptr<T>& item : items) {
        if (item) {
            std::optional<std::string> refusal = locked_layout_refusal(*item);
            if (refusal.has_value()) {
                return refusal;
            }
        }
    }
    return {};
}

// True for the properties of the Kit_layout group.
[[nodiscard]] auto is_kit_layout_property(const erhe::property::Dependency_property& property) -> bool;

} // namespace editor
