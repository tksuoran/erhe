#include "scene/variant_table.hpp"

#include "app_message.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>

namespace editor {

namespace {

// The GeomSubset name one primitive of a mesh has, in the spelling the USD
// writer gives it: the primitive's geometry name with the mesh name prefix
// dropped (the importer names a primitive "<mesh name>.<subset name>"), and
// "<mesh name>_<index>" for a primitive built from a triangle soup, which
// carries no geometry name.
[[nodiscard]] auto primitive_subset_name(const erhe::scene::Mesh& mesh, const std::size_t primitive_index) -> std::string
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

// The item one binding path names below the prim carrying the set: the prim
// itself for an empty path, and the item find_by_path reaches otherwise.
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
    return erhe::find_by_path(*hierarchy, relative_path);
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

auto Variant_table::find(const std::string& prim_path, const std::string& set_name) -> Variant_set*
{
    for (Variant_set& set : m_sets) {
        if (set.prim.expired()) {
            // get_prim_path() of a gone prim is the empty string, which is
            // also the path of a set the scene's root prim carries.
            continue;
        }
        if ((set.set_name == set_name) && (set.get_prim_path() == prim_path)) {
            return &set;
        }
    }
    return nullptr;
}

auto Variant_table::set_selected(
    const std::string& prim_path,
    const std::string& set_name,
    const std::string& variant_name
) -> bool
{
    Variant_set* const set = find(prim_path, set_name);
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
