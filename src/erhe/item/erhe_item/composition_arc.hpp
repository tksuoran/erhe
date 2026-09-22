#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace erhe {

// Which composition arc brought a prim's content in. USD spells the two forms
// differently and a save must write back the one that was authored
// (doc/erhe/usd_compatibility_design.md X1); a glTF external asset is a
// reference.
enum class Composition_arc_kind : unsigned int {
    reference = 0,
    payload   = 1
};

// One entry of the `variants` selection a composition arc carries into the
// target it brings in: the variant of `set_name` chosen for the prim
// `relative_path` names below the arc's target prim, an empty path being the
// target prim itself (doc/erhe/usd_compatibility_design.md C7). This is
// erhe::usd::Usd_variant_selection in terms erhe::item can speak: erhe::usd is
// an optional dependency (ERHE_USD_LIBRARY) and an arc names a glTF file as
// readily as a USD one, so no prim header names it.
class Composition_variant_selection final
{
public:
    std::string relative_path;
    std::string set_name;
    std::string variant_name;

    [[nodiscard]] auto operator< (const Composition_variant_selection& rhs) const -> bool;
    [[nodiscard]] auto operator==(const Composition_variant_selection& rhs) const -> bool;
};

// One composition arc applied to a prim: the prim's subtree was instantiated
// (cloned) from `source_path` - a glTF file, or the prim `prim_path` names of
// a USD file (doc/erhe/usd_compatibility_design.md X1). The record is the
// durable statement of that association: glTF export writes such a prim as a
// glTF 2.1 externalAsset reference instead of flattening the subtree, and a
// USD save writes the arc back as the form `kind` names.
class Composition_arc final
{
public:
    // The file the arc names, canonical.
    std::filesystem::path source_path;
    // The prim of the source file the content was taken from, as authored in
    // the arc. Empty for a glTF file and for an arc naming the target layer's
    // default prim.
    std::string prim_path;
    // The display name of what the arc brings in, as the file authors it (the
    // glTF externalAsset `name`). Carried in the record because it is
    // authored data: it must survive a save of a prim whose target file is
    // not loaded.
    std::string name;
    // The arc form this content was authored as; a USD save writes it back as
    // that form. Always a reference for a glTF file.
    Composition_arc_kind kind{Composition_arc_kind::reference};
    // The `variants` selection this arc carries into the target it brings in.
    // Part of the target's identity - two arcs selecting different variants of
    // one target compose two prim indexes and so bring in two different trees
    // - and what a USD save writes back on the prim.
    std::vector<Composition_variant_selection> variant_selections;

    [[nodiscard]] auto operator==(const Composition_arc& rhs) const -> bool;
};

// The selection as one line of text: "<path> <set> = <variant>" per entry,
// separated by "; ", the target prim itself spelled "."; empty for an empty
// selection. What the read-only Properties row and the logs show.
[[nodiscard]] auto to_string(const std::vector<Composition_variant_selection>& variant_selections) -> std::string;

// One arc as one line of text: the arc form, the source file, the target prim
// in brackets when the arc names one, and the selection the arc carries when
// it carries one. What the read-only Properties row and the logs show.
[[nodiscard]] auto to_string(const Composition_arc& arc) -> std::string;

// The arcs of one prim as one line of text per arc, in authored order.
[[nodiscard]] auto to_string(std::span<const Composition_arc> arcs) -> std::string;

} // namespace erhe
