#include "operations/variant_select_operation.hpp"

#include "operations/compound_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/scene_root.hpp"
#include "scene/variant_table.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/transform.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace editor {

namespace {

// The entry of `variant` for one relative path, null when the variant authors
// nothing there.
[[nodiscard]] auto find_variant_override(
    const Variant&     variant,
    const std::string& relative_path
) -> const erhe::scene::Instance_override*
{
    for (const erhe::scene::Instance_override& entry : variant.overrides) {
        if (entry.relative_path == relative_path) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_override_value(
    const erhe::scene::Instance_override& entry,
    const std::string&                    name
) -> const erhe::scene::Instance_override_value*
{
    for (const erhe::scene::Instance_override_value& value : entry.values) {
        if (value.name == name) {
            return &value;
        }
    }
    return nullptr;
}

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

// The property writes one switch performs. The set's base values name every
// path and property name any of its variants authors, so this visits exactly
// the opinions a switch can leave standing: the chosen variant's value where
// it authors one, the base value - a local value or none at all - where it
// does not.
void append_variant_property_operations(
    const Variant_set&                       set,
    const Variant&                           variant,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    for (const erhe::scene::Instance_override& base : set.base_values) {
        const std::shared_ptr<erhe::Item_base> item = resolve_variant_prim(set, base.relative_path);
        if (!item) {
            continue; // the prim the opinion names is no longer in the scene
        }
        const erhe::scene::Instance_override* const authored = find_variant_override(variant, base.relative_path);
        for (const erhe::scene::Instance_override_value& base_value : base.values) {
            const erhe::property::Dependency_property* const property =
                erhe::scene::find_override_property(*item.get(), base_value.name);
            if (property == nullptr) {
                continue;
            }
            const erhe::scene::Instance_override_value* const authored_value =
                (authored != nullptr) ? find_override_value(*authored, base_value.name) : nullptr;
            const erhe::scene::Instance_override_value& wanted =
                (authored_value != nullptr) ? *authored_value : base_value;
            std::optional<erhe::property::Property_value> after;
            if (wanted.state == erhe::scene::Instance_override_value_state::supplied) {
                after = erhe::property::parse_value(*item.get(), *property, wanted.text);
            }
            std::optional<erhe::property::Property_value> before;
            if (item->has_local_value(*property)) {
                before = item->get_value(*property);
            }
            if (before == after) {
                continue;
            }
            operations.push_back(
                std::make_shared<Property_set_operation>(item, *property, before, after)
            );
        }
        if (!base.transform_overridden) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Xformable> xformable = std::dynamic_pointer_cast<erhe::scene::Xformable>(item);
        if (!xformable) {
            continue;
        }
        const glm::mat4 wanted = ((authored != nullptr) && authored->transform_overridden)
            ? authored->transform
            : base.transform;
        const glm::mat4 current = xformable->parent_from_node_transform().get_matrix();
        if (is_near(current, wanted)) {
            continue;
        }
        Node_transform_operation::Parameters parameters{};
        parameters.node                    = xformable;
        parameters.parent_from_node_before = erhe::scene::Transform{current};
        parameters.parent_from_node_after  = erhe::scene::Transform{wanted};
        parameters.xform_op_stack_before   = xformable->copy_xform_op_stack();
        operations.push_back(std::make_shared<Node_transform_operation>(parameters));
    }
}

// The prims a switch turns on and off (doc/usd-compatibility-plan.md X4).
// Every variant's prims are in
// the scene: the chosen variant's are active - by holding no local `active`
// value at all, which is the state the file's own selection loads in - and
// every other variant's are inactive, which prunes each one and its subtree
// from the render, the pick and the simulation (X2).
void append_variant_prim_operations(
    const Variant_set&                       set,
    const Variant&                           variant,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    for (const Variant& candidate : set.variants) {
        const bool is_chosen = (candidate.name == variant.name);
        for (const Variant_prim& prim : candidate.prims) {
            const std::shared_ptr<erhe::Item_base> item = resolve_variant_prim(set, prim.relative_path);
            if (!item) {
                continue; // the prim is no longer in the scene
            }
            const erhe::property::Dependency_property* const property =
                erhe::scene::find_override_property(*item.get(), "active");
            if (property == nullptr) {
                continue;
            }
            std::optional<erhe::property::Property_value> before;
            if (item->has_local_value(*property)) {
                before = item->get_value(*property);
            }
            std::optional<erhe::property::Property_value> after;
            if (!is_chosen) {
                after = erhe::property::Property_value{false};
            }
            if (before == after) {
                continue;
            }
            operations.push_back(
                std::make_shared<Property_set_operation>(item, *property, before, after)
            );
        }
    }
}

} // anonymous namespace

Variant_select_operation::Variant_select_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description(
        fmt::format(
            "Select variant {} of set {} on {}",
            m_parameters.after_variant_name,
            m_parameters.set_name,
            m_parameters.prim_path
        )
    );
}

Variant_select_operation::~Variant_select_operation() noexcept = default;

void Variant_select_operation::apply(const std::string& variant_name, const Entry_state entry_state)
{
    const std::shared_ptr<Scene_root> scene_root = m_parameters.scene_root.lock();
    if (!scene_root) {
        return; // the scene closed: there is nothing to select in it
    }
    // The table's selection is what the UI, a save and a further switch read.
    scene_root->get_variant_table().set_selected(m_parameters.prim_path, m_parameters.set_name, variant_name);

    // The settings entry is what a saved scene carries the selection in.
    std::vector<Variant_selection>& selections = scene_root->get_scene_settings().variant_selections;
    const auto is_this_set = [this](const Variant_selection& selection) {
        return (selection.prim_path == m_parameters.prim_path) && (selection.set_name == m_parameters.set_name);
    };
    const std::vector<Variant_selection>::iterator i = std::find_if(selections.begin(), selections.end(), is_this_set);
    if (entry_state == Entry_state::absent) {
        if (i != selections.end()) {
            selections.erase(i);
        }
        return;
    }
    if (i != selections.end()) {
        i->variant_name = variant_name;
        return;
    }
    Variant_selection selection{};
    selection.prim_path    = m_parameters.prim_path;
    selection.set_name     = m_parameters.set_name;
    selection.variant_name = variant_name;
    selections.push_back(std::move(selection));
}

void Variant_select_operation::execute(App_context& context)
{
    static_cast<void>(context);
    apply(m_parameters.after_variant_name, Entry_state::present);
}

void Variant_select_operation::undo(App_context& context)
{
    static_cast<void>(context);
    apply(m_parameters.before_variant_name, m_parameters.before_entry_state);
}

auto make_select_variant_operation(
    const std::shared_ptr<Scene_root>& scene_root,
    const std::string&                 prim_path,
    const std::string&                 set_name,
    const std::string&                 variant_name
) -> std::shared_ptr<Operation>
{
    if (!scene_root) {
        return {};
    }
    Variant_table&     variant_table = scene_root->get_variant_table();
    Variant_set* const set           = variant_table.find(prim_path, set_name);
    if (set == nullptr) {
        return {};
    }
    const Variant* const variant = set->find_variant(variant_name);
    if (variant == nullptr) {
        return {};
    }

    std::vector<std::shared_ptr<Operation>> operations;
    Variant_select_operation::Parameters parameters{};
    parameters.scene_root          = scene_root;
    parameters.prim_path           = prim_path;
    parameters.set_name            = set_name;
    parameters.before_variant_name = set->selected;
    parameters.after_variant_name  = variant_name;
    const std::vector<Variant_selection>& selections = scene_root->get_scene_settings().variant_selections;
    parameters.before_entry_state = std::any_of(
        selections.begin(),
        selections.end(),
        [&prim_path, &set_name](const Variant_selection& selection) {
            return (selection.prim_path == prim_path) && (selection.set_name == set_name);
        }
    ) ? Variant_select_operation::Entry_state::present
      : Variant_select_operation::Entry_state::absent;
    operations.push_back(std::make_shared<Variant_select_operation>(std::move(parameters)));

    append_variant_property_operations(*set, *variant, operations);
    append_variant_prim_operations(*set, *variant, operations);

    for (const Variant_binding& binding : variant->bindings) {
        const Variant_binding_target target   = resolve_variant_binding(*set, *variant, binding);
        const std::shared_ptr<erhe::primitive::Material> material = binding.material.lock();
        if (!target.mesh || target.primitive_indices.empty()) {
            continue; // the binding names no mesh of the scene any more
        }
        for (const std::size_t primitive_index : target.primitive_indices) {
            const std::shared_ptr<Mesh_material_assign_operation> assign =
                make_mesh_material_assign_operation(target.mesh, primitive_index, material);
            if (assign) {
                operations.push_back(assign);
            }
        }
    }

    std::shared_ptr<Compound_operation> compound = std::make_shared<Compound_operation>(
        Compound_operation::Parameters{.operations = std::move(operations)}
    );
    compound->set_description(
        fmt::format("[{}] Select variant {} of {} on {}", compound->get_serial(), variant_name, set_name, prim_path)
    );
    return compound;
}

} // namespace editor
