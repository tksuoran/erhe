#pragma once

#include "app_message.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace erhe           { class Item_base; }
namespace erhe::primitive { class Material; }
namespace erhe::scene     { class Mesh; }

namespace editor {

// One material binding of one variant (doc/usd-compatibility-plan.md X4).
// `relative_path` is the M1 path of the bound prim below the prim carrying
// the set, empty for that prim itself. The material is held weakly: a
// material an undo takes out of the editor must not be pinned by the table
// (AGENTS.md "Scene-hosted references in editor parts").
class Variant_binding
{
public:
    std::string                             relative_path;
    std::weak_ptr<erhe::primitive::Material> material;
};

// One variant of a variant set: its name and the bindings it authors.
class Variant
{
public:
    std::string                  name;
    std::vector<Variant_binding> bindings;
};

// One variant set of a scene: the prim carrying it, the set's name, its
// variants and which one is selected. Only material-binding variants are
// carried in this slice; `unsupported_opinion_count` is how many other
// opinions the file's variants authored, which a save does not write.
class Variant_set
{
public:
    std::weak_ptr<erhe::Item_base> prim;
    std::string                    set_name;
    std::vector<Variant>           variants;
    std::string                    selected;
    std::size_t                    unsupported_opinion_count{0};

    // The M1 path of the carrying prim, empty when the prim is gone. Built
    // on demand from the item - never call it per frame.
    [[nodiscard]] auto get_prim_path() const -> std::string;
    [[nodiscard]] auto find_variant(const std::string& variant_name) const -> const Variant*;
};

// Which primitives of which mesh one binding names: the mesh the binding's
// path reached and either one primitive of it (a GeomSubset binding) or all
// of the mesh's primitives the same variant does not bind by subset.
class Variant_binding_target
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    std::vector<std::size_t>           primitive_indices;
};

// The variant sets one scene carries, owned by its Scene_root and dying with
// it. Filled by the USD parser when a scene is opened or an asset imported
// (doc/usd-compatibility-plan.md X4); a set whose carrying prim or whose
// bound materials leave the editor is dropped, so a dead set is never
// offered.
class Variant_table
{
public:
    void add  (Variant_set&& set);
    void clear();

    [[nodiscard]] auto get_sets() const -> const std::vector<Variant_set>&;
    [[nodiscard]] auto find(const std::string& prim_path, const std::string& set_name) -> Variant_set*;

    // Records the selection of one set; false when the set or the variant is
    // not there. The materials are assigned by the operation the caller
    // builds, not here.
    auto set_selected(const std::string& prim_path, const std::string& set_name, const std::string& variant_name) -> bool;

    // Drops every set whose carrying prim is gone - expired, or named by an
    // items_removed message (an undo of the import that brought it in).
    void on_items_removed(const Removed_items& removed);
    void drop_expired_sets();

private:
    std::vector<Variant_set> m_sets;
};

// The mesh primitives one binding of `set` names. An empty
// `primitive_indices` means the binding reached nothing (a path that names no
// mesh of the scene any more).
[[nodiscard]] auto resolve_variant_binding(
    const Variant_set&     set,
    const Variant&         variant,
    const Variant_binding& binding
) -> Variant_binding_target;

} // namespace editor
