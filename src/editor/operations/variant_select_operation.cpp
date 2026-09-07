#include "operations/variant_select_operation.hpp"

#include "operations/compound_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "scene/scene_root.hpp"
#include "scene/variant_table.hpp"

#include "erhe_primitive/material.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

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
