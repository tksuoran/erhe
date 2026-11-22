#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The foliage paint tool's palette (doc/plans/foliage_paint_tool_design.md FD7).
namespace editor {

class Kit_lengths;
class Kit_parameter_values;
class Kit_shape_library;

inline constexpr int c_foliage_min_variants = 1;
inline constexpr int c_foliage_max_variants = 64;

class Foliage_palette_entry
{
public:
    std::string              shape;
    std::string              parameters;      // Kit_parameter_values canonical text; empty: the defaults
    std::vector<std::string> materials{};     // a name per slot; empty: the slot default
    std::uint32_t            weight         {1};
    float                    radius         {1.0f}; // meters
    float                    yaw_min_degrees{0.0f};
    float                    yaw_max_degrees{360.0f};
    int                      variants       {1};
    int                      seed           {0};

    [[nodiscard]] auto operator==(const Foliage_palette_entry& other) const -> bool = default;
};

// FD7.3: a JSON list of objects with sorted keys, floats as hexadecimal strings; equal values give equal text.
// 'parameters' is a JSON string holding the canonical text verbatim; the reader takes only that form.
[[nodiscard]] auto make_foliage_palette_text(std::span<const Foliage_palette_entry> entries) -> std::string;

// Returns the problem, empty when read; out is written only on success. A syntax error names its byte, a repeated key
// is refused; value ranges beyond the integer widths are check_foliage_palette()'s.
[[nodiscard]] auto parse_foliage_palette_text(std::string_view text, std::vector<Foliage_palette_entry>& out_entries) -> std::string;

// FD7.4: names the first entry that cannot be painted; empty when every entry can.
[[nodiscard]] auto check_foliage_palette(const Kit_shape_library& library, std::span<const Foliage_palette_entry> entries) -> std::string;

// FD7.2: the seed of one variant of an entry.
[[nodiscard]] auto get_foliage_variant_seed(int entry_seed, std::uint32_t variant) -> int;

// FD7.2: the entry's variants for values that derive from the seed or a seeded shape, else 1.
[[nodiscard]] auto get_foliage_variant_count(const Foliage_palette_entry& entry, const Kit_parameter_values& values) -> std::uint32_t;

// FD7.2: whether the variants of values take their seeds from the entry (values that derive from the seed, a seeded shape).
[[nodiscard]] auto is_foliage_seed_derived(const Kit_parameter_values& values) -> bool;

// FD7.2: each variant's values from the entry's base values at the lengths, its seed applied, checked by validate() and
// the shape's combination check; clears and fills out_variants. Returns the first problem ("variant <v>: ..."), empty when none.
[[nodiscard]] auto derive_foliage_variants(
    const Foliage_palette_entry&       entry,
    const Kit_parameter_values&        base,
    const Kit_lengths&                 lengths,
    std::vector<Kit_parameter_values>& out_variants
) -> std::string;

} // namespace editor
