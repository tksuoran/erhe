#include "erhe_item/composition_arc.hpp"

#include <fmt/format.h>

#include <tuple>

namespace erhe {

auto Composition_variant_selection::operator<(const Composition_variant_selection& rhs) const -> bool
{
    return std::tie(relative_path, set_name, variant_name) < std::tie(rhs.relative_path, rhs.set_name, rhs.variant_name);
}

auto Composition_variant_selection::operator==(const Composition_variant_selection& rhs) const -> bool
{
    return std::tie(relative_path, set_name, variant_name) == std::tie(rhs.relative_path, rhs.set_name, rhs.variant_name);
}

auto Composition_arc::operator==(const Composition_arc& rhs) const -> bool
{
    return (source_path == rhs.source_path) &&
        (prim_path == rhs.prim_path) &&
        (name == rhs.name) &&
        (kind == rhs.kind) &&
        (variant_selections == rhs.variant_selections);
}

auto to_string(const std::vector<Composition_variant_selection>& variant_selections) -> std::string
{
    std::string result;
    for (const Composition_variant_selection& entry : variant_selections) {
        if (!result.empty()) {
            result += "; ";
        }
        result += fmt::format(
            "{} {} = {}",
            entry.relative_path.empty() ? std::string{"."} : entry.relative_path,
            entry.set_name,
            entry.variant_name
        );
    }
    return result;
}

auto to_string(const Composition_arc& arc) -> std::string
{
    std::string result = fmt::format(
        "{} {}",
        (arc.kind == Composition_arc_kind::payload) ? "payload" : "reference",
        arc.source_path.generic_string()
    );
    if (!arc.prim_path.empty()) {
        result += fmt::format(" [{}]", arc.prim_path);
    }
    if (!arc.variant_selections.empty()) {
        result += fmt::format(" variants: {}", to_string(arc.variant_selections));
    }
    return result;
}

auto to_string(const std::span<const Composition_arc> arcs) -> std::string
{
    std::string result;
    for (const Composition_arc& arc : arcs) {
        if (!result.empty()) {
            result += "\n";
        }
        result += to_string(arc);
    }
    return result;
}

} // namespace erhe
