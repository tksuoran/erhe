#include "level/kit_replace.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace editor {

auto c_str(const Kit_carry carry) -> const char*
{
    switch (carry) {
        case Kit_carry::carried:   return "carried";
        case Kit_carry::clamped:   return "clamped";
        case Kit_carry::defaulted: return "defaulted";
        default:                   return "?";
    }
}

void carry_parameter_values(
    const Kit_parameter_values&       from,
    const Kit_shape_definition&       to,
    const Kit_lengths&                lengths,
    Kit_parameter_values&             out_values,
    std::vector<Kit_parameter_carry>& out_carry
)
{
    out_values = make_default_values(to, lengths.module, lengths.wall_height);
    out_carry.clear();
    const Kit_shape_definition* const from_shape = from.get_shape();
    for (std::size_t j = 0, end = to.parameters.size(); j < end; ++j) {
        const Kit_parameter& parameter = to.parameters[j];
        Kit_parameter_carry  carry{.name = parameter.name, .carry = Kit_carry::defaulted};
        const std::optional<std::size_t> i = (from_shape != nullptr) ? from_shape->find_parameter(parameter.name) : std::optional<std::size_t>{};
        if (!i.has_value() || (from_shape->parameters[i.value()].type != parameter.type)) {
            out_carry.push_back(std::move(carry));
            continue;
        }
        const Kit_parameter_value old_value = from.get_values()[i.value()];
        Kit_parameter_value       value     = old_value;
        switch (parameter.type) {
            case Kit_parameter_type::float_value: {
                value.float_value = std::clamp(old_value.float_value, parameter.float_min, parameter.float_max);
                break;
            }
            case Kit_parameter_type::int_value: {
                value.int_value = std::clamp(old_value.int_value, parameter.int_min, parameter.int_max);
                break;
            }
            case Kit_parameter_type::enum_value: {
                const std::string_view name = from.get_enum(parameter.name);
                const std::vector<std::string>::const_iterator k = std::find(parameter.enum_values.begin(), parameter.enum_values.end(), name);
                if (k == parameter.enum_values.end()) {
                    out_carry.push_back(std::move(carry));
                    continue;
                }
                value.int_value = static_cast<int>(k - parameter.enum_values.begin());
                break;
            }
            default: {
                out_carry.push_back(std::move(carry));
                continue;
            }
        }
        // A seed-derived value stays derived where the new parameter can be; it is derived again in the new range.
        const bool from_seed = (from.get_value_source(i.value()) == Kit_value_source::seed) && (parameter.derive == Kit_derive::derivable);
        out_values.set_value(j, value);
        out_values.set_value_source(j, from_seed ? Kit_value_source::seed : Kit_value_source::explicit_value);
        carry.carry = (!from_seed && !(value == old_value) && (parameter.type != Kit_parameter_type::enum_value))
            ? Kit_carry::clamped
            : Kit_carry::carried;
        out_carry.push_back(std::move(carry));
    }
    out_values.resolve_derived(lengths.module, lengths.wall_height);
}

void match_material_slots(
    const std::span<const std::string>       from_slots,
    const std::span<const std::string>       to_slots,
    std::vector<std::optional<std::size_t>>& out_matches
)
{
    out_matches.clear();
    for (const std::string& slot : to_slots) {
        const std::span<const std::string>::iterator i = std::find(from_slots.begin(), from_slots.end(), slot);
        out_matches.push_back((i != from_slots.end()) ? std::optional<std::size_t>{static_cast<std::size_t>(i - from_slots.begin())} : std::optional<std::size_t>{});
    }
}

auto c_str(const Kit_replace_skip skip) -> const char*
{
    switch (skip) {
        case Kit_replace_skip::kit_layout_node:     return "kit_layout_node";
        case Kit_replace_skip::locked_layout_child: return "locked_layout_child";
        case Kit_replace_skip::group:               return "group";
        case Kit_replace_skip::not_a_kit_piece:     return "not_a_kit_piece";
        case Kit_replace_skip::unreadable_record:   return "unreadable_record";
        case Kit_replace_skip::loaded_mesh:         return "loaded_mesh";
        default:                                    return "?";
    }
}

auto describe(const Kit_replace_skip skip) -> const char*
{
    switch (skip) {
        case Kit_replace_skip::kit_layout_node:     return "a kit layout node";
        case Kit_replace_skip::locked_layout_child: return "a child of a locked kit layout node";
        case Kit_replace_skip::group:               return "a group";
        case Kit_replace_skip::not_a_kit_piece:     return "not a kit piece";
        case Kit_replace_skip::unreadable_record:   return "its record does not read";
        case Kit_replace_skip::loaded_mesh:         return "a loaded mesh";
        default:                                    return "?";
    }
}

auto classify_replace_target(const Kit_replace_target_facts& facts) -> std::optional<Kit_replace_skip>
{
    if (facts.kit_layout_node) {
        return Kit_replace_skip::kit_layout_node;
    }
    if (facts.below_locked_layout) {
        return Kit_replace_skip::locked_layout_child;
    }
    if (!facts.mesh) {
        return facts.group_node ? Kit_replace_skip::group : Kit_replace_skip::not_a_kit_piece;
    }
    if (!facts.kit_piece_record) {
        return Kit_replace_skip::not_a_kit_piece;
    }
    if (!facts.record_reads) {
        return Kit_replace_skip::unreadable_record;
    }
    if (!facts.slot_count_matches) {
        return Kit_replace_skip::loaded_mesh;
    }
    return {};
}

auto make_replace_summary(const Kit_replace_summary& summary) -> std::string
{
    std::string text;
    if (summary.replaced_count > 0) {
        text = fmt::format(
            "Replaced {} {} with {}.",
            summary.replaced_count, (summary.replaced_count == 1) ? "piece" : "pieces", summary.shape_name
        );
        if (summary.snapped_count.has_value()) {
            text += fmt::format(" Snapped {} of them to the surface.", summary.snapped_count.value());
        }
    } else if (summary.unchanged_count > 0) {
        text = fmt::format("The selected pieces are {} already.", summary.shape_name);
    } else {
        text = "Replaced no piece.";
    }
    if (!summary.skipped.empty()) {
        text += fmt::format(" Skipped {}:", summary.skipped.size());
        const std::size_t shown = std::min(summary.skipped.size(), c_replace_summary_max_names);
        for (std::size_t i = 0; i < shown; ++i) {
            const Kit_replace_skipped_name& entry = summary.skipped[i];
            text += fmt::format("{} '{}' ({})", (i == 0) ? "" : ",", entry.name, describe(entry.skip));
        }
        if (summary.skipped.size() > shown) {
            text += fmt::format(" and {} more", summary.skipped.size() - shown);
        }
        text += ".";
    }
    return text;
}

} // namespace editor
