#pragma once

#include "level/kit_shape.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
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

} // namespace editor
