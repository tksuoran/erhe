#pragma once

#include "level/kit_shape.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

// What a replace (D3.10) did with one parameter of the new shape.
enum class Kit_carry : unsigned int {
    carried = 0, // the old shape has it with the same name and type; the value (or its seed source) kept
    clamped,     // as carried, the value clamped into the new shape's range
    defaulted    // the old shape has no such parameter, or an enum name the new shape lacks
};

[[nodiscard]] auto c_str(Kit_carry carry) -> const char*;

class Kit_parameter_carry
{
public:
    std::string name;
    Kit_carry   carry{Kit_carry::defaulted};
};

// D3.10: the values of shape `to` with those of `from` carried over by name and type, the masked values derived
// at the lengths; out_carry has one entry per parameter of `to`, in its order. The caller validates.
void carry_parameter_values(
    const Kit_parameter_values&       from,
    const Kit_shape_definition&       to,
    const Kit_lengths&                lengths,
    Kit_parameter_values&             out_values,
    std::vector<Kit_parameter_carry>& out_carry
);

// For each slot of to_slots, the index of the slot of the same name in from_slots, else none.
void match_material_slots(
    std::span<const std::string>             from_slots,
    std::span<const std::string>             to_slots,
    std::vector<std::optional<std::size_t>>& out_matches
);

// Why a replace (D3.10, U24) leaves a node out; the other nodes are replaced and the call lists the skipped ones.
enum class Kit_replace_skip : unsigned int {
    kit_layout_node = 0, // its items' shapes are its own values
    locked_layout_child, // below a locked kit layout node
    group,               // an Xform group node
    not_a_kit_piece,     // no kit piece record (a camera, a light, a curve, a plain mesh)
    unreadable_record,   // a record that does not read
    loaded_mesh          // its mesh has another primitive count than its shape has slots
};

[[nodiscard]] auto c_str   (Kit_replace_skip skip) -> const char*; // the MCP reply's reason
[[nodiscard]] auto describe(Kit_replace_skip skip) -> const char*; // "a kit layout node"

// What replace_shape() reads of one node, in the order the skips are decided.
class Kit_replace_target_facts
{
public:
    bool kit_layout_node    {false};
    bool below_locked_layout{false};
    bool mesh               {false};
    bool group_node         {false};
    bool kit_piece_record   {false};
    bool record_reads       {false};
    bool slot_count_matches {false};
};

// The skip of the node, none when it can be replaced.
[[nodiscard]] auto classify_replace_target(const Kit_replace_target_facts& facts) -> std::optional<Kit_replace_skip>;

class Kit_replace_skipped_name
{
public:
    std::string      name;
    Kit_replace_skip skip{Kit_replace_skip::not_a_kit_piece};
};

class Kit_replace_summary
{
public:
    std::string_view                          shape_name;
    std::size_t                               replaced_count {0};
    std::size_t                               unchanged_count{0}; // pieces of the shape already
    std::optional<std::size_t>                snapped_count;      // none when the replace did not snap
    std::span<const Kit_replace_skipped_name> skipped;
};

inline constexpr std::size_t c_replace_summary_max_names = 8;

// The Level Kit window's line after a replace: what was replaced, snapped and skipped (with each skip's reason).
[[nodiscard]] auto make_replace_summary(const Kit_replace_summary& summary) -> std::string;

} // namespace editor
