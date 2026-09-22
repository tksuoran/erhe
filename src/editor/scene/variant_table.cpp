#include "scene/variant_table.hpp"

#include "app_message.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_property/dependency_property.hpp"

#include "erhe_property/property_string.hpp"

#include "erhe_scene/instance_override.hpp"

#include "erhe_scene/mesh.hpp"

#include "erhe_scene/node.hpp"

#include "erhe_scene/transform.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>

namespace editor {

// The GeomSubset name one primitive of a mesh has, in the spelling the USD
// writer gives it: the primitive's geometry name with the mesh name prefix
// dropped (the importer names a primitive "<mesh name>.<subset name>"), and
// "<mesh name>_<index>" for a primitive built from a triangle soup, which
// carries no geometry name.
auto primitive_subset_name(const erhe::scene::Mesh& mesh, const std::size_t primitive_index) -> std::string
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitive_index >= primitives.size()) {
        return std::string{};
    }
    const erhe::primitive::Primitive_render_shape* render_shape =
        primitives[primitive_index].primitive ? primitives[primitive_index].primitive->render_shape.get() : nullptr;
    const std::shared_ptr<erhe::geometry::Geometry> geometry =
        (render_shape != nullptr) ? render_shape->get_geometry_const() : std::shared_ptr<erhe::geometry::Geometry>{};
    const std::string mesh_name = mesh.get_name();
    if (!geometry) {
        return fmt::format("{}_{}", mesh_name, primitive_index);
    }
    const std::string geometry_name = geometry->get_name();
    const std::string prefix        = mesh_name + ".";
    if ((geometry_name.size() > prefix.size()) && (geometry_name.compare(0, prefix.size(), prefix) == 0)) {
        return geometry_name.substr(prefix.size());
    }
    return geometry_name;
}

namespace {

// The item one path of a variant set names below the prim carrying it: the
// prim itself for the empty path, the item the path names below it where the
// tree holds one, and otherwise the item the path names through the clone of
// every composition arc it crosses. erhe composes no arc - the editor
// instantiates each one after a load returns (doc/erhe/usd_compatibility_design.md
// C6) - so a variant that authors an opinion or a binding for a prim an arc
// supplies names it through the arc's target clone, and a switch of the set
// reaches the same prim the load did (erhe::scene::find_instance_item, which
// parsers/usd.cpp apply_pending_variant_opinions() applies such an entry
// with).
[[nodiscard]] auto find_binding_item(
    const std::shared_ptr<erhe::Item_base>& prim,
    const std::string&                      relative_path
) -> erhe::Hierarchy*
{
    erhe::Hierarchy* const hierarchy = dynamic_cast<erhe::Hierarchy*>(prim.get());
    if (hierarchy == nullptr) {
        return nullptr;
    }
    if (relative_path.empty()) {
        return hierarchy;
    }
    erhe::Hierarchy* const own = erhe::find_by_path(*hierarchy, relative_path);
    return (own != nullptr) ? own : erhe::scene::find_instance_item(*hierarchy, relative_path);
}

// Everything before the last path separator, empty when the path holds none.
[[nodiscard]] auto parent_of_path(const std::string& path) -> std::string
{
    const std::size_t separator = path.rfind('/');
    return (separator == std::string::npos) ? std::string{} : path.substr(0, separator);
}

[[nodiscard]] auto last_name_of_path(const std::string& path) -> std::string
{
    const std::size_t separator = path.rfind('/');
    return (separator == std::string::npos) ? path : path.substr(separator + 1);
}

} // anonymous namespace

auto Variant_set::get_prim_path() const -> std::string
{
    const std::shared_ptr<erhe::Item_base> item = prim.lock();
    if (!item) {
        return std::string{};
    }
    const erhe::Hierarchy* const hierarchy = dynamic_cast<const erhe::Hierarchy*>(item.get());
    return (hierarchy != nullptr) ? hierarchy->get_path() : item->get_name();
}

auto Variant_set::get_key() const -> Variant_set_key
{
    return Variant_set_key{
        .prim_path              = get_prim_path(),
        .set_name               = set_name,
        .enclosing_set_name     = enclosing_set_name,
        .enclosing_variant_name = enclosing_variant_name
    };
}

auto Variant_set::find_variant(const std::string& variant_name) const -> const Variant*
{
    for (const Variant& variant : variants) {
        if (variant.name == variant_name) {
            return &variant;
        }
    }
    return nullptr;
}

void Variant_table::add(Variant_set&& set)
{
    m_sets.push_back(std::move(set));
}

void Variant_table::clear()
{
    m_sets.clear();
}

auto Variant_table::get_sets() const -> const std::vector<Variant_set>&
{
    return m_sets;
}

auto Variant_table::get_sets() -> std::vector<Variant_set>&
{
    return m_sets;
}

auto Variant_table::find(const Variant_set_key& key) -> Variant_set*
{
    for (Variant_set& set : m_sets) {
        if (set.prim.expired()) {
            // get_prim_path() of a gone prim is the empty string, which is
            // also the path of a set the scene's root prim carries.
            continue;
        }
        if ((set.set_name               == key.set_name) &&
            (set.enclosing_set_name     == key.enclosing_set_name) &&
            (set.enclosing_variant_name == key.enclosing_variant_name) &&
            (set.get_prim_path()        == key.prim_path))
        {
            return &set;
        }
    }
    return nullptr;
}

auto Variant_table::find_enclosing_set(const Variant_set& set) -> Variant_set*
{
    if (set.enclosing_set_name.empty()) {
        return nullptr;
    }
    const std::shared_ptr<erhe::Item_base> prim = set.prim.lock();
    if (!prim) {
        return nullptr;
    }
    // A set of the same prim, of the enclosing name, holding the block this
    // set is declared inside. Nesting repeats while the table names one level
    // up, so a chain declaring one set name at two depths offers more than one
    // candidate: the one whose own chain is selected is the one this set
    // contributes through, and the first match answers for everything else.
    Variant_set* first_match = nullptr;
    for (Variant_set& candidate : m_sets) {
        if ((&candidate == &set) ||
            (candidate.prim.lock() != prim) ||
            (candidate.set_name != set.enclosing_set_name) ||
            (candidate.find_variant(set.enclosing_variant_name) == nullptr))
        {
            continue;
        }
        if (first_match == nullptr) {
            first_match = &candidate;
        }
        if (is_live(candidate)) {
            return &candidate;
        }
    }
    return first_match;
}

auto Variant_table::is_live(const Variant_set& set) -> bool
{
    // The chain is at most as long as the table, and a table read from a file
    // whose enclosing names form a cycle would loop without the bound.
    const Variant_set* current = &set;
    for (std::size_t depth = 0, end = m_sets.size() + 1; depth < end; ++depth) {
        if (current->enclosing_set_name.empty()) {
            return true;
        }
        const std::shared_ptr<erhe::Item_base> prim = current->prim.lock();
        if (!prim) {
            return false;
        }
        const Variant_set* enclosing = nullptr;
        for (const Variant_set& candidate : m_sets) {
            if ((&candidate == current) ||
                (candidate.prim.lock() != prim) ||
                (candidate.set_name != current->enclosing_set_name) ||
                (candidate.selected != current->enclosing_variant_name))
            {
                continue;
            }
            enclosing = &candidate;
            break;
        }
        if (enclosing == nullptr) {
            return false; // the block this set is declared inside is not the selected one
        }
        current = enclosing;
    }
    return false;
}

auto Variant_table::find_nested_sets(const Variant_set& set, const std::string& variant_name) -> std::vector<Variant_set*>
{
    std::vector<Variant_set*> nested;
    const std::shared_ptr<erhe::Item_base> prim = set.prim.lock();
    if (!prim) {
        return nested;
    }
    for (Variant_set& candidate : m_sets) {
        if ((&candidate == &set) ||
            (candidate.prim.lock() != prim) ||
            (candidate.enclosing_set_name != set.set_name) ||
            (candidate.enclosing_variant_name != variant_name))
        {
            continue;
        }
        nested.push_back(&candidate);
    }
    return nested;
}

auto Variant_table::set_selected(
    const Variant_set_key& key,
    const std::string&     variant_name
) -> bool
{
    Variant_set* const set = find(key);
    if ((set == nullptr) || (set->find_variant(variant_name) == nullptr)) {
        return false;
    }
    set->selected = variant_name;
    return true;
}

void Variant_table::on_items_removed(const Removed_items& removed)
{
    // Membership test only: undoing a large import announces thousands of
    // items in one message (AGENTS.md "Scene-hosted references in editor
    // parts").
    m_sets.erase(
        std::remove_if(
            m_sets.begin(),
            m_sets.end(),
            [&removed](const Variant_set& set) {
                const std::shared_ptr<erhe::Item_base> item = set.prim.lock();
                return !item || (removed.lookup.count(item.get()) != 0);
            }
        ),
        m_sets.end()
    );
}

void Variant_table::drop_expired_sets()
{
    m_sets.erase(
        std::remove_if(
            m_sets.begin(),
            m_sets.end(),
            [](const Variant_set& set) { return set.prim.expired(); }
        ),
        m_sets.end()
    );
}

auto resolve_variant_prim(const Variant_set& set, const std::string& relative_path) -> std::shared_ptr<erhe::Item_base>
{
    const std::shared_ptr<erhe::Item_base> prim = set.prim.lock();
    if (!prim) {
        return {};
    }
    erhe::Hierarchy* const item = find_binding_item(prim, relative_path);
    return (item != nullptr) ? item->shared_from_this() : std::shared_ptr<erhe::Item_base>{};
}

auto make_variant_binding_path(
    const erhe::Hierarchy&   carrier,
    const erhe::scene::Mesh& mesh,
    const std::size_t        primitive_index
) -> std::string
{
    if (primitive_index >= mesh.get_primitives().size()) {
        return std::string{};
    }
    // Names collected leaf first, up to (not including) the carrier.
    std::vector<std::string> names;
    const erhe::Hierarchy* node = &mesh;
    while (node != &carrier) {
        const std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();
        if (!parent) {
            return std::string{}; // the mesh does not sit below the carrier
        }
        names.push_back(node->get_name());
        node = parent.get();
    }
    std::string path;
    for (std::size_t i = names.size(); i > 0; --i) {
        path += names[i - 1];
        if (i > 1) {
            path += '/';
        }
    }
    path += fmt::format("#{}", primitive_index);
    return path;
}

void capture_variant_base_value(
    erhe::Hierarchy&                             target,
    const erhe::scene::Instance_override&        entry,
    std::vector<erhe::scene::Instance_override>& base_values
)
{
    erhe::scene::Instance_override* base = nullptr;
    for (erhe::scene::Instance_override& candidate : base_values) {
        if (candidate.relative_path == entry.relative_path) {
            base = &candidate;
            break;
        }
    }
    if (base == nullptr) {
        erhe::scene::Instance_override new_base{};
        new_base.relative_path = entry.relative_path;
        base_values.push_back(std::move(new_base));
        base = &base_values.back();
    }
    for (const erhe::scene::Instance_override_value& value : entry.values) {
        bool already_recorded = false;
        for (const erhe::scene::Instance_override_value& recorded : base->values) {
            if (recorded.name == value.name) {
                already_recorded = true;
                break;
            }
        }
        if (already_recorded) {
            continue;
        }
        // The same lookup apply_property_values makes, so the base value is
        // taken from the object the opinion will be applied to - which for an
        // applied schema's value is the prim itself
        // (erhe::scene::find_override_property_target).
        const erhe::scene::Override_property_target property_target = erhe::scene::find_override_property_target(target, value.name);
        if (property_target.property == nullptr) {
            continue; // apply_property_values warns about the name once
        }
        if (property_target.object->has_local_value(*property_target.property)) {
            base->values.push_back(
                erhe::scene::Instance_override_value{
                    .name  = value.name,
                    .text  = erhe::property::to_string(*property_target.property, property_target.object->get_value(*property_target.property)),
                    .state = erhe::scene::Instance_override_value_state::supplied
                }
            );
        } else {
            base->values.push_back(
                erhe::scene::Instance_override_value{
                    .name  = value.name,
                    .text  = std::string{},
                    .state = erhe::scene::Instance_override_value_state::cleared
                }
            );
        }
    }
    if (entry.transform_overridden && !base->transform_overridden) {
        const erhe::scene::Xformable* const xformable = dynamic_cast<const erhe::scene::Xformable*>(&target);
        if (xformable != nullptr) {
            base->transform_overridden = true;
            base->transform            = xformable->parent_from_node_transform().get_matrix();
            base->xform_op_stack       = xformable->copy_xform_op_stack();
        }
    }
}

void capture_variant_base_values(Variant_set& set)
{
    for (const Variant& variant : set.variants) {
        for (const erhe::scene::Instance_override& entry : variant.overrides) {
            const std::shared_ptr<erhe::Item_base> item = resolve_variant_prim(set, entry.relative_path);
            erhe::Hierarchy* const target = dynamic_cast<erhe::Hierarchy*>(item.get());
            if (target != nullptr) {
                capture_variant_base_value(*target, entry, set.base_values);
            }
        }
    }
}

auto resolve_variant_binding(
    const Variant_set&     set,
    const Variant&         variant,
    const Variant_binding& binding
) -> Variant_binding_target
{
    Variant_binding_target target{};
    const std::shared_ptr<erhe::Item_base> prim = set.prim.lock();
    if (!prim) {
        return target;
    }
    // `<mesh path>#<primitive index>`: one primitive of a mesh, named by its
    // position (see make_variant_binding_path).
    const std::size_t hash = binding.relative_path.rfind('#');
    if (hash != std::string::npos) {
        erhe::Hierarchy* const indexed = find_binding_item(prim, binding.relative_path.substr(0, hash));
        if ((indexed == nullptr) || !erhe::is<erhe::scene::Mesh>(indexed)) {
            return target;
        }
        erhe::scene::Mesh* const indexed_mesh = static_cast<erhe::scene::Mesh*>(indexed);
        const std::string index_text = binding.relative_path.substr(hash + 1);
        std::size_t primitive_index = 0;
        const std::from_chars_result parsed = std::from_chars(
            index_text.data(), index_text.data() + index_text.size(), primitive_index
        );
        if ((parsed.ec != std::errc{}) ||
            (parsed.ptr != (index_text.data() + index_text.size())) ||
            (primitive_index >= indexed_mesh->get_primitives().size()))
        {
            return target;
        }
        target.mesh = std::static_pointer_cast<erhe::scene::Mesh>(indexed_mesh->shared_from_this());
        target.primitive_indices.push_back(primitive_index);
        return target;
    }
    erhe::Hierarchy* const bound = find_binding_item(prim, binding.relative_path);
    if ((bound != nullptr) && erhe::is<erhe::scene::Mesh>(bound)) {
        // A binding on a mesh covers the primitives the same variant does not
        // bind by subset, the way a USD binding on a prim is the fallback for
        // the descendants that author one of their own.
        erhe::scene::Mesh* const mesh = static_cast<erhe::scene::Mesh*>(bound);
        target.mesh = std::static_pointer_cast<erhe::scene::Mesh>(mesh->shared_from_this());
        for (std::size_t index = 0, end = mesh->get_primitives().size(); index < end; ++index) {
            const std::string subset_name = primitive_subset_name(*mesh, index);
            const std::string subset_path = binding.relative_path.empty()
                ? subset_name
                : (binding.relative_path + "/" + subset_name);
            const bool bound_by_subset = !subset_name.empty() && std::any_of(
                variant.bindings.begin(),
                variant.bindings.end(),
                [&subset_path](const Variant_binding& other) { return other.relative_path == subset_path; }
            );
            if (!bound_by_subset) {
                target.primitive_indices.push_back(index);
            }
        }
        return target;
    }
    // Not a mesh of the scene: a GeomSubset name below a mesh, which erhe
    // holds as one primitive of that mesh rather than as an item of its own.
    const std::string parent_path = parent_of_path(binding.relative_path);
    const std::string subset_name = last_name_of_path(binding.relative_path);
    erhe::Hierarchy* const parent = find_binding_item(prim, parent_path);
    if ((parent == nullptr) || !erhe::is<erhe::scene::Mesh>(parent)) {
        return target;
    }
    erhe::scene::Mesh* const mesh = static_cast<erhe::scene::Mesh*>(parent);
    for (std::size_t index = 0, end = mesh->get_primitives().size(); index < end; ++index) {
        if (primitive_subset_name(*mesh, index) == subset_name) {
            target.mesh = std::static_pointer_cast<erhe::scene::Mesh>(mesh->shared_from_this());
            target.primitive_indices.push_back(index);
            return target;
        }
    }
    return target;
}

} // namespace editor
