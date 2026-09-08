#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_string.hpp"

#include <cmath>
#include <optional>
#include <string_view>

namespace erhe::scene {

namespace {

[[nodiscard]] auto is_near(const glm::mat4& lhs, const glm::mat4& rhs) -> bool
{
    constexpr float tolerance = 1e-5f;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            if (std::abs(lhs[j][i] - rhs[j][i]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// The transform half of the override rule: a bridged transform always holds a
// local value, so only a difference from the counterpart is an override.
[[nodiscard]] auto is_transform_overridden(const erhe::Item_base& item, const erhe::property::Dependency_object& counterpart) -> bool
{
    const Xformable* xformable             = dynamic_cast<const Xformable*>(&item);
    const Xformable* counterpart_xformable = dynamic_cast<const Xformable*>(&counterpart);
    if ((xformable == nullptr) || (counterpart_xformable == nullptr)) {
        return false;
    }
    return !is_near(
        xformable->parent_from_node_transform().get_matrix(),
        counterpart_xformable->parent_from_node_transform().get_matrix()
    );
}

// The name of the group of facets one primitive of a mesh covers, as a file
// names the group: the importer names a primitive's geometry
// `<mesh name>.<subset name>` (a USD GeomSubset below its Mesh prim), so the
// mesh's own name is dropped again here. Empty when the primitive carries no
// geometry to name, which leaves the group unaddressable by name.
[[nodiscard]] auto subset_name_of_primitive(const Mesh& mesh, const std::size_t primitive_index) -> std::string
{
    const std::vector<Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitive_index >= primitives.size()) {
        return std::string{};
    }
    const erhe::primitive::Primitive* primitive = primitives[primitive_index].primitive.get();
    if (primitive == nullptr) {
        return std::string{};
    }
    const erhe::primitive::Primitive_render_shape* render_shape = primitive->render_shape.get();
    if (render_shape == nullptr) {
        return std::string{};
    }
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = render_shape->get_geometry_const();
    if (!geometry) {
        return std::string{};
    }
    const std::string  geometry_name = geometry->get_name();
    const std::string  prefix        = mesh.get_name() + ".";
    return (geometry_name.compare(0, prefix.size(), prefix) == 0)
        ? geometry_name.substr(prefix.size())
        : geometry_name;
}

// The material half of the override rule: a primitive whose material differs
// from the one the counterpart mesh's primitive at the same index binds is an
// override. A primitive that binds nothing cannot be spelled as a binding, so
// it is not one.
void collect_material_overrides(
    const erhe::Item_base&                    item,
    const erhe::property::Dependency_object&  counterpart,
    std::vector<Instance_override_material>&  out_materials
)
{
    const Mesh* mesh             = dynamic_cast<const Mesh*>(&item);
    const Mesh* counterpart_mesh = dynamic_cast<const Mesh*>(&counterpart);
    if ((mesh == nullptr) || (counterpart_mesh == nullptr)) {
        return;
    }
    const std::vector<Mesh_primitive>& primitives             = mesh->get_primitives();
    const std::vector<Mesh_primitive>& counterpart_primitives = counterpart_mesh->get_primitives();
    for (std::size_t index = 0, end = primitives.size(); index < end; ++index) {
        const std::shared_ptr<erhe::primitive::Material>& material = primitives[index].material;
        if (!material) {
            continue;
        }
        if ((index < counterpart_primitives.size()) && (counterpart_primitives[index].material == material)) {
            continue;
        }
        out_materials.push_back(Instance_override_material{.primitive_index = index, .material = material});
    }
}

void collect_item(
    const erhe::Hierarchy&              item,
    const std::string&                  relative_path,
    std::vector<Instance_override_item>& out_items
)
{
    const std::shared_ptr<const erhe::property::Dependency_object>& counterpart = item.get_reference();
    if (!counterpart) {
        return; // not instance content: parented under the carrier by hand
    }

    const erhe::property::Owner_type owner_type = item.get_property_owner_type();
    bool                             has_value  = false;
    item.for_each_local_value(
        [&has_value, &item, owner_type](
            const erhe::property::Dependency_property& property,
            const erhe::property::Property_value&      value
        ) {
            static_cast<void>(value);
            if (has_value) {
                return;
            }
            const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
            if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                return;
            }
            if (metadata.bridge.is_bound() || metadata.is_computed()) {
                return;
            }
            if (item.get_expression(property).has_value()) {
                return;
            }
            has_value = true;
        }
    );

    const bool transform_overridden = is_transform_overridden(item, *counterpart.get());
    std::vector<Instance_override_material> materials;
    collect_material_overrides(item, *counterpart.get(), materials);
    if (has_value || transform_overridden || !materials.empty()) {
        out_items.push_back(
            Instance_override_item{
                .relative_path        = relative_path,
                .item                 = &item,
                .transform_overridden = transform_overridden,
                .materials            = std::move(materials)
            }
        );
    }

    for (const std::shared_ptr<erhe::Hierarchy>& child : item.get_children()) {
        if (!child) {
            continue;
        }
        const std::string child_path = relative_path.empty()
            ? child->get_name()
            : (relative_path + "/" + child->get_name());
        collect_item(*child.get(), child_path, out_items);
    }
}

// The item `relative_path` names below a carrier: an entry names the item at
// its path below the first of the carrier's children that has one. An empty
// path is that child itself.
[[nodiscard]] auto find_instance_item(erhe::Hierarchy& carrier, const std::string& relative_path) -> erhe::Hierarchy*
{
    for (const std::shared_ptr<erhe::Hierarchy>& clone : carrier.get_children()) {
        if (!clone) {
            continue;
        }
        erhe::Hierarchy* target = erhe::find_by_path(*clone.get(), relative_path);
        if (target != nullptr) {
            return target;
        }
    }
    return nullptr;
}

[[nodiscard]] auto to_material(erhe::Hierarchy* item) -> std::shared_ptr<erhe::primitive::Material>
{
    if (dynamic_cast<erhe::primitive::Material*>(item) == nullptr) {
        return {};
    }
    return std::dynamic_pointer_cast<erhe::primitive::Material>(item->shared_from_this());
}

// The path a file spells a bound material by, as the path of the material
// item: the M1 path of the item in the scene tree. Empty for a material
// outside the tree, which no path names.
[[nodiscard]] auto material_reference_path(const erhe::primitive::Material& material) -> std::string
{
    const erhe::Hierarchy* hierarchy = dynamic_cast<const erhe::Hierarchy*>(&material);
    return (hierarchy != nullptr) ? hierarchy->get_path() : std::string{};
}

// The item `path` names below `item`, with the extra level an instance keeps
// treated as transparent: USD composes an arc's content directly under the
// referencing prim, while erhe keeps the arc's target clone as one level of
// its own (doc/usd-compatibility-plan.md X1), so a name that matches no child
// is looked for below each child that is such a clone (a child that names a
// counterpart).
[[nodiscard]] auto find_by_composed_path(erhe::Hierarchy& item, const std::string_view path) -> erhe::Hierarchy*
{
    if (path.empty()) {
        return &item;
    }
    const std::size_t      separator = path.find('/');
    const std::string_view name      = (separator == std::string_view::npos) ? path : path.substr(0, separator);
    const std::string_view rest      = (separator == std::string_view::npos) ? std::string_view{} : path.substr(separator + 1);
    for (const std::shared_ptr<erhe::Hierarchy>& child : item.get_children()) {
        if (child && (child->get_name() == name)) {
            erhe::Hierarchy* found = find_by_composed_path(*child.get(), rest);
            if (found != nullptr) {
                return found;
            }
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : item.get_children()) {
        if (child && child->get_reference()) {
            erhe::Hierarchy* found = find_by_composed_path(*child.get(), path);
            if (found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

// The material a binding names. A path an instance's own layer authors starts
// at the arc's target prim, which is the clone the carrier holds, so the path
// below that target is tried below each of the carrier's children first; then
// the path whole, from the carrier's ancestors nearest first, so that a file
// opened below a wrapper is reached as well as one opened at the root.
[[nodiscard]] auto find_bound_material(erhe::Hierarchy& carrier, const std::string& path) -> std::shared_ptr<erhe::primitive::Material>
{
    std::string_view rest{path};
    while (!rest.empty() && (rest.front() == '/')) {
        rest.remove_prefix(1);
    }
    const std::size_t      separator    = rest.find('/');
    const std::string_view below_target = (separator == std::string_view::npos)
        ? std::string_view{}
        : rest.substr(separator + 1);
    for (const std::shared_ptr<erhe::Hierarchy>& clone : carrier.get_children()) {
        if (!clone) {
            continue;
        }
        std::shared_ptr<erhe::primitive::Material> material;
        if (!below_target.empty()) {
            material = to_material(find_by_composed_path(*clone.get(), below_target));
        }
        if (!material) {
            material = to_material(find_by_composed_path(*clone.get(), rest));
        }
        if (material) {
            return material;
        }
    }
    erhe::Hierarchy* ancestor = &carrier;
    for (;;) {
        const std::shared_ptr<erhe::Hierarchy> parent = ancestor->get_parent().lock();
        if (!parent) {
            break;
        }
        ancestor = parent.get();
        const std::shared_ptr<erhe::primitive::Material> material = to_material(find_by_composed_path(*ancestor, rest));
        if (material) {
            return material;
        }
    }
    return {};
}

// One material binding of an override, on the item the entry names: the mesh
// takes the material for the group of facets `subset` names, or for every
// primitive when the binding names no group. A target that is no mesh, a
// path that names no material and a group name no primitive carries cost one
// warning each.
void apply_material_binding(
    erhe::Hierarchy&       carrier,
    erhe::Hierarchy&       target,
    const std::string&     relative_path,
    const std::string&     material_path,
    const std::string_view subset
)
{
    Mesh* mesh = dynamic_cast<Mesh*>(&target);
    if (mesh == nullptr) {
        log->warn(
            "instance '{}': the override of '{}' binds a material to an item that is no mesh",
            carrier.get_name(),
            relative_path
        );
        return;
    }
    const std::shared_ptr<erhe::primitive::Material> material = find_bound_material(carrier, material_path);
    if (!material) {
        log->warn(
            "instance '{}': the override of '{}' binds '{}', which names no material - the binding is dropped",
            carrier.get_name(),
            relative_path,
            material_path
        );
        return;
    }
    const std::size_t primitive_count = mesh->get_primitives().size();
    if (subset.empty()) {
        for (std::size_t index = 0; index < primitive_count; ++index) {
            mesh->set_primitive_material(index, material);
        }
        return;
    }
    for (std::size_t index = 0; index < primitive_count; ++index) {
        if (subset_name_of_primitive(*mesh, index) == subset) {
            mesh->set_primitive_material(index, material);
            return;
        }
    }
    log->warn(
        "instance '{}': the override of '{}' binds a group of facets the mesh does not hold - the binding is dropped",
        carrier.get_name(),
        relative_path
    );
}

// An override whose path names no item, holding nothing but a material
// binding: a group of facets is a prim of its own in a file and a primitive
// of the erhe mesh, so the last name of the path is the group and the rest
// names the mesh. True when the entry was taken this way.
[[nodiscard]] auto apply_subset_binding(erhe::Hierarchy& carrier, const Instance_override& entry) -> bool
{
    if (entry.material_path.empty() || !entry.values.empty() || entry.transform_overridden) {
        return false;
    }
    const std::size_t      separator = entry.relative_path.rfind('/');
    const std::string      mesh_path = (separator == std::string::npos)
        ? std::string{}
        : entry.relative_path.substr(0, separator);
    const std::string_view subset    = (separator == std::string::npos)
        ? std::string_view{entry.relative_path}
        : std::string_view{entry.relative_path}.substr(separator + 1);
    erhe::Hierarchy* target = find_instance_item(carrier, mesh_path);
    if ((target == nullptr) || (dynamic_cast<Mesh*>(target) == nullptr)) {
        return false;
    }
    apply_material_binding(carrier, *target, entry.relative_path, entry.material_path, subset);
    return true;
}

} // anonymous namespace

// The property one override value names. A collected override spells the name
// the way the registry does (`Owner.name` only where the object holds the
// value for another class), while a file can spell it qualified whatever the
// object is - `erhe:Mesh:shadow_cast` on a Mesh prim - so a qualified name
// that resolves to nothing is retried as the bare name, with the owner type
// it names checked against the property's own.
auto find_override_property(
    const erhe::property::Dependency_object& object,
    const std::string&                       name
) -> const erhe::property::Dependency_property*
{
    const erhe::property::Property_registry&   registry = erhe::property::Property_registry::get();
    const erhe::property::Dependency_property* property = registry.find_for_object(object, name);
    if (property != nullptr) {
        return property;
    }
    const std::size_t dot = name.find('.');
    if (dot == std::string::npos) {
        return nullptr;
    }
    const std::optional<erhe::property::Owner_type> named_owner = registry.find_owner_type(std::string_view{name}.substr(0, dot));
    if (!named_owner.has_value()) {
        return nullptr;
    }
    property = registry.find_for_object(object, std::string_view{name}.substr(dot + 1));
    if ((property != nullptr) && (property->get_owner_type() != named_owner.value())) {
        return nullptr;
    }
    return property;
}

void apply_property_values(
    erhe::Item_base&                            item,
    const std::vector<Instance_override_value>& values,
    const std::string_view                      owner
)
{
    for (const Instance_override_value& value : values) {
        const erhe::property::Dependency_property* property = find_override_property(item, value.name);
        if (property == nullptr) {
            log->warn("'{}': the value '{}' names no property of '{}'", owner, value.name, item.get_name());
            continue;
        }
        if (value.state == Instance_override_value_state::cleared) {
            item.clear_value(*property);
            continue;
        }
        const std::optional<erhe::property::Property_value> parsed =
            erhe::property::parse_value(item, *property, value.text);
        if (!parsed.has_value()) {
            log->warn("'{}': the value '{}' text '{}' does not parse", owner, value.name, value.text);
            continue;
        }
        item.set_value(*property, parsed.value());
    }
}

auto collect_instance_override_items(const erhe::Hierarchy& carrier) -> std::vector<Instance_override_item>
{
    std::vector<Instance_override_item> result;
    for (const std::shared_ptr<erhe::Hierarchy>& clone : carrier.get_children()) {
        if (clone) {
            collect_item(*clone.get(), std::string{}, result);
        }
    }
    return result;
}

auto collect_instance_overrides(const erhe::Hierarchy& carrier) -> std::vector<Instance_override>
{
    std::vector<Instance_override>            result;
    const std::vector<Instance_override_item> items = collect_instance_override_items(carrier);
    for (const Instance_override_item& entry : items) {
        const erhe::Item_base* item = entry.item;
        Instance_override      override_entry{};
        override_entry.relative_path = entry.relative_path;

        const erhe::property::Owner_type owner_type = item->get_property_owner_type();
        item->for_each_local_value(
            [&override_entry, item, owner_type](
                const erhe::property::Dependency_property& property,
                const erhe::property::Property_value&      value
            ) {
                const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
                if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                    return;
                }
                if (metadata.bridge.is_bound() || metadata.is_computed()) {
                    return;
                }
                if (item->get_expression(property).has_value()) {
                    return;
                }
                override_entry.values.push_back(
                    Instance_override_value{
                        .name = erhe::property::Property_registry::get().qualified_name(*item, property),
                        .text = erhe::property::to_string(property, value)
                    }
                );
            }
        );

        override_entry.transform_overridden = entry.transform_overridden;
        const Xformable* xformable = dynamic_cast<const Xformable*>(item);
        if (entry.transform_overridden && (xformable != nullptr)) {
            override_entry.transform      = xformable->parent_from_node_transform().get_matrix();
            override_entry.xform_op_stack = xformable->copy_xform_op_stack();
        }

        // A binding that covers the whole mesh is the item's own; a binding
        // that covers one group of facets is an entry of its own, named by
        // the group below the item.
        const Mesh*                    mesh = dynamic_cast<const Mesh*>(item);
        std::vector<Instance_override> subset_entries;
        for (const Instance_override_material& material : entry.materials) {
            const std::string material_path = material_reference_path(*material.material.get());
            if (material_path.empty()) {
                log->warn(
                    "the material '{}' bound inside an instance is outside the scene tree - the binding is not carried",
                    material.material->get_name()
                );
                continue;
            }
            if ((mesh != nullptr) && (mesh->get_primitives().size() == 1)) {
                override_entry.material_path = material_path;
                continue;
            }
            const std::string subset_name = (mesh != nullptr)
                ? subset_name_of_primitive(*mesh, material.primitive_index)
                : std::string{};
            if (subset_name.empty()) {
                log->warn(
                    "the material '{}' is bound to a group of facets of '{}' that has no name - the binding is not carried",
                    material.material->get_name(),
                    item->get_name()
                );
                continue;
            }
            subset_entries.push_back(
                Instance_override{
                    .relative_path = entry.relative_path.empty() ? subset_name : (entry.relative_path + "/" + subset_name),
                    .material_path = material_path
                }
            );
        }
        result.push_back(std::move(override_entry));
        for (Instance_override& subset_entry : subset_entries) {
            result.push_back(std::move(subset_entry));
        }
    }
    return result;
}

void apply_instance_overrides(erhe::Hierarchy& carrier, const std::vector<Instance_override>& overrides)
{
    for (const Instance_override& entry : overrides) {
        erhe::Hierarchy* target = find_instance_item(carrier, entry.relative_path);
        if (target == nullptr) {
            // A group of facets is a prim of its own in a file (a USD
            // GeomSubset) and a primitive of the erhe mesh, so a binding of
            // one names a path the instance has no item for: the mesh that
            // holds the group takes it.
            if (apply_subset_binding(carrier, entry)) {
                continue;
            }
            log->warn(
                "instance '{}': the override of '{}' names no item of the instance",
                carrier.get_name(),
                entry.relative_path
            );
            continue;
        }
        apply_property_values(*target, entry.values, carrier.get_name());
        if (!entry.material_path.empty()) {
            apply_material_binding(carrier, *target, entry.relative_path, entry.material_path, std::string_view{});
        }
        if (entry.transform_overridden) {
            Xformable* xformable = dynamic_cast<Xformable*>(target);
            if (xformable == nullptr) {
                log->warn("instance '{}': the transform override of '{}' names an item without one", carrier.get_name(), entry.relative_path);
                continue;
            }
            if (entry.xform_op_stack.has_value()) {
                xformable->set_xform_op_stack(entry.xform_op_stack.value());
            } else {
                xformable->set_parent_from_node(entry.transform);
            }
        }
    }
}

} // namespace erhe::scene
