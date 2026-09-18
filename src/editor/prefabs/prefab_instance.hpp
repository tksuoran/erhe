#pragma once

#include "erhe_scene/node_attachment.hpp"

#include "erhe_property/dependency_property.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

// Which composition arc a prefab instance came from. USD spells the two
// forms differently and a save must write back the one that was authored
// (doc/usd_compatibility_design.md X1); a glTF prefab instance is a reference.
enum class Prefab_arc_kind : unsigned int {
    reference = 0,
    payload   = 1
};

// One entry of the `variants` selection a composition arc carries into the
// template it brings in: the variant of `set_name` chosen for the prim
// `relative_path` names below the arc's target prim, an empty path being the
// target prim itself (doc/usd_compatibility_design.md section 6, "Variant
// selection through a composition arc"). This is
// erhe::usd::Usd_variant_selection in the terms the editor's prefab types can
// speak: erhe::usd is an optional dependency (ERHE_USD_LIBRARY) and a prefab
// is a glTF file as readily as a USD one, so no prefab header names it.
class Prefab_variant_selection final
{
public:
    std::string relative_path;
    std::string set_name;
    std::string variant_name;

    [[nodiscard]] auto operator< (const Prefab_variant_selection& rhs) const -> bool;
    [[nodiscard]] auto operator==(const Prefab_variant_selection& rhs) const -> bool;
};

// One variant set a prefab template's tree declares: the set `set_name` of
// the prim `relative_path` names below the template's root prim, an empty
// path being the root prim itself. The same coordinates
// Prefab_variant_selection uses, so a selection entry names the set it
// selects in exactly when its two path/name fields match one of these.
//
// A template reports the sets it CONSUMES - the ones its own file declares
// plus the ones the files its arcs bring in declare, re-rooted at this
// template's root - and a selection entry naming a set outside that list
// selects nothing anywhere in the template, so it is not part of the
// template's identity (doc/frame-time-after-usd-import-plan.md R4).
class Prefab_variant_set_key final
{
public:
    std::string relative_path;
    std::string set_name;

    [[nodiscard]] auto operator< (const Prefab_variant_set_key& rhs) const -> bool;
    [[nodiscard]] auto operator==(const Prefab_variant_set_key& rhs) const -> bool;
};

// The selection as one line of text: "<path> <set> = <variant>" per entry,
// separated by "; ", the target prim itself spelled "."; empty for an empty
// selection. What the read-only Properties row and the logs show.
[[nodiscard]] auto to_string(const std::vector<Prefab_variant_selection>& variant_selections) -> std::string;

// Marks a node as the root of a prefab instance: the node's subtree was
// instantiated (cloned) from a source file managed by Prefab_library - a glTF
// file, or one prim of a USD file (doc/usd_compatibility_design.md X1). The
// attachment is the durable record of that association -- glTF export writes
// such nodes as glTF 2.1 externalAsset references instead of flattening the
// subtree, and a USD save writes the arc back. A node carries one attachment
// per arc, in the order the arcs were authored. Clonable so clipboard copy /
// paste of an instance yields another instance of the same prefab.
class Prefab_instance : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Prefab_instance, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Prefab_instance();
    explicit Prefab_instance(const Prefab_instance&);
    Prefab_instance& operator=(const Prefab_instance&);
    Prefab_instance(const Prefab_instance& src, erhe::for_clone);
    ~Prefab_instance() noexcept override;

    Prefab_instance(
        const std::filesystem::path& source_path,
        const std::string&           prefab_name,
        const std::string&           prim_path = {},
        Prefab_arc_kind              arc_kind = Prefab_arc_kind::reference,
        const std::vector<Prefab_variant_selection>& variant_selections = {}
    );

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Prefab_instance"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node_attachment | erhe::Item_type::prefab_instance; }

    // Public API
    [[nodiscard]] auto get_prefab_source_path() const -> const std::filesystem::path&;
    [[nodiscard]] auto get_prefab_name       () const -> const std::string&;
    // The prim of the source file the instance was cloned from, as authored in
    // the `references` arc. Empty for a glTF prefab and for an arc that names
    // the target layer's default prim.
    [[nodiscard]] auto get_prefab_prim_path  () const -> const std::string&;
    // The arc form this instance was authored as; a USD save writes it back
    // as that form. Always a reference for a glTF prefab.
    [[nodiscard]] auto get_prefab_arc_kind   () const -> Prefab_arc_kind;
    // The `variants` selection this arc carries into the target it brings in.
    // Part of the template's identity - two carriers selecting different
    // variants of one target compose two prim indexes and so load two
    // templates - and what a USD save writes back on the carrier.
    [[nodiscard]] auto get_prefab_variant_selections     () const -> const std::vector<Prefab_variant_selection>&;
    // get_prefab_variant_selections() as to_string() spells it: the value of
    // the read-only Properties row.
    [[nodiscard]] auto get_prefab_variant_selections_text() const -> std::string;

    static const erhe::property::Property<std::string> variant_selections_property;

private:
    std::filesystem::path m_prefab_source_path;
    std::string           m_prefab_name;
    std::string           m_prefab_prim_path;
    Prefab_arc_kind       m_prefab_arc_kind{Prefab_arc_kind::reference};

    std::vector<Prefab_variant_selection> m_prefab_variant_selections;
};

// Returns the outermost node, walking up from and including the given node,
// that carries a SEALED Prefab_instance attachment (a glTF template,
// is_sealed_prefab_instance); nullptr when the node is inside no sealed
// instance. A sealed instance subtree is not editable in the containing
// scene, so picking anything inside one resolves to the instance root, and
// nested sealed instances resolve to the outermost one. A USD-backed
// instance is not sealed: its interior picks and selects like any other prim
// (doc/usd_compatibility_design.md X2).
[[nodiscard]] auto get_outermost_prefab_instance_node(erhe::scene::Node* node) -> erhe::scene::Node*;

}
