#include "prefabs/instance_structure.hpp"

#include "parsers/usd.hpp"

#include "erhe_file/file.hpp"
#include "erhe_item/composition_arc.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>

#include <memory>
#include <string>
#include <vector>

namespace editor {

namespace {

// The instance carrier at or above `hierarchy` and its first arc; nullptr
// when the walk reaches the root without meeting one.
[[nodiscard]] auto find_carrier(const erhe::Hierarchy* hierarchy, const erhe::Composition_arc*& out_arc) -> const erhe::Hierarchy*
{
    while (hierarchy != nullptr) {
        const erhe::Typed* prim = dynamic_cast<const erhe::Typed*>(hierarchy);
        if ((prim != nullptr) && prim->has_composition_arcs()) {
            out_arc = prim->get_composition_arcs().data();
            return hierarchy;
        }
        const std::shared_ptr<erhe::Hierarchy> parent = hierarchy->get_parent().lock();
        hierarchy = parent.get();
    }
    return nullptr;
}

} // anonymous namespace

auto get_structural_hierarchy(const erhe::Item_base& item) -> const erhe::Hierarchy*
{
    return dynamic_cast<const erhe::Hierarchy*>(&item);
}

auto get_instance_arcs(const erhe::Item_base& item) -> std::span<const erhe::Composition_arc>
{
    const erhe::Typed* prim = dynamic_cast<const erhe::Typed*>(&item);
    if (prim == nullptr) {
        return std::span<const erhe::Composition_arc>{};
    }
    return prim->get_composition_arcs();
}

auto get_first_instance_arc(const erhe::Item_base& item) -> const erhe::Composition_arc*
{
    const std::span<const erhe::Composition_arc> arcs = get_instance_arcs(item);
    return arcs.empty() ? nullptr : arcs.data();
}

auto is_instance_carrier(const erhe::Item_base& item) -> bool
{
    const erhe::Typed* prim = dynamic_cast<const erhe::Typed*>(&item);
    return (prim != nullptr) && prim->has_composition_arcs();
}

auto instance_structure_refusal(const erhe::Item_base& item) -> std::optional<std::string>
{
    const erhe::Hierarchy* hierarchy = get_structural_hierarchy(item);
    if (hierarchy == nullptr) {
        return {};
    }
    // The carrier itself is a normal scene prim: only what hangs BELOW it is
    // instance content, so the walk starts at the parent.
    const std::shared_ptr<erhe::Hierarchy> parent = hierarchy->get_parent().lock();
    const erhe::Composition_arc*           arc = nullptr;
    const erhe::Hierarchy*                 carrier = find_carrier(parent.get(), arc);
    if (carrier == nullptr) {
        return {};
    }
    return fmt::format(
        "'{}' is inside the reference instance '{}'; deactivate it or edit the referenced file '{}'",
        hierarchy->get_path(), carrier->get_name(), erhe::file::to_string(arc->source_path)
    );
}

auto instance_child_refusal(const erhe::Hierarchy& parent) -> std::optional<std::string>
{
    const erhe::Composition_arc* arc = nullptr;
    const erhe::Hierarchy*       carrier = find_carrier(&parent, arc);
    if (carrier == nullptr) {
        return {};
    }
    if (carrier == &parent) {
        return fmt::format(
            "'{}' is a reference instance: its content comes from '{}'; edit the referenced file to add to it",
            parent.get_path(), erhe::file::to_string(arc->source_path)
        );
    }
    return fmt::format(
        "'{}' is inside the reference instance '{}'; deactivate it or edit the referenced file '{}'",
        parent.get_path(), carrier->get_name(), erhe::file::to_string(arc->source_path)
    );
}

auto is_instance_structure_protected(const erhe::Item_base& item) -> bool
{
    const erhe::Hierarchy* hierarchy = get_structural_hierarchy(item);
    if (hierarchy == nullptr) {
        return false;
    }
    const std::shared_ptr<erhe::Hierarchy> parent = hierarchy->get_parent().lock();
    const erhe::Composition_arc*           arc = nullptr;
    return find_carrier(parent.get(), arc) != nullptr;
}

auto refuses_instance_child(const erhe::Hierarchy& parent) -> bool
{
    const erhe::Composition_arc* arc = nullptr;
    return find_carrier(&parent, arc) != nullptr;
}

auto find_instance_position(const erhe::Item_base& item) -> Instance_position
{
    Instance_position      result{};
    const erhe::Hierarchy* hierarchy = get_structural_hierarchy(item);
    if (hierarchy == nullptr) {
        return result;
    }
    // Names are collected walking up to the carrier and joined in reverse;
    // the LAST of them is the carrier's own child, the clone of the arc's
    // target prim, which the USD writer collapses onto the carrier.
    std::vector<const std::string*> names;
    const erhe::Hierarchy*          walk = hierarchy;
    while (walk != nullptr) {
        const std::shared_ptr<erhe::Hierarchy> parent = walk->get_parent().lock();
        if (!parent) {
            return Instance_position{};
        }
        const erhe::Composition_arc* arc = get_first_instance_arc(*parent.get());
        if (arc != nullptr) {
            result.carrier = parent.get();
            result.arc     = arc;
            for (std::size_t i = names.size(); i > 0; --i) {
                if (!result.relative_path.empty()) {
                    result.relative_path.push_back('/');
                }
                result.relative_path.append(*names[i - 1]);
            }
            return result;
        }
        names.push_back(&walk->get_name());
        walk = parent.get();
    }
    return result;
}

auto is_sealed_prefab_instance(const erhe::Composition_arc& arc) -> bool
{
    return !is_usd_file_extension(arc.source_path);
}

auto is_sealed_instance_carrier(const erhe::Item_base& item) -> bool
{
    const erhe::Composition_arc* arc = get_first_instance_arc(item);
    return (arc != nullptr) && is_sealed_prefab_instance(*arc);
}

auto get_outermost_prefab_instance_node(erhe::scene::Node* node) -> erhe::scene::Node*
{
    erhe::scene::Node* outermost = nullptr;
    for (erhe::scene::Node* ancestor = node; ancestor != nullptr; ancestor = ancestor->get_parent_node().get()) {
        if (is_sealed_instance_carrier(*ancestor)) {
            outermost = ancestor;
        }
    }
    return outermost;
}

} // namespace editor
