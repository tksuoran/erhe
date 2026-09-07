#include "prefabs/instance_structure.hpp"

#include "parsers/usd.hpp"
#include "prefabs/prefab_instance.hpp"

#include "erhe_file/file.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"

#include <fmt/format.h>

#include <memory>
#include <string>
#include <vector>

namespace editor {

namespace {

// The instance carrier at or above `hierarchy`, and the carrier prim it
// hangs off; nullptr when the walk reaches the root without meeting one.
[[nodiscard]] auto find_carrier(const erhe::Hierarchy* hierarchy, std::shared_ptr<Prefab_instance>& out_prefab_instance) -> const erhe::Hierarchy*
{
    while (hierarchy != nullptr) {
        const erhe::scene::Xformable* prim = dynamic_cast<const erhe::scene::Xformable*>(hierarchy);
        if (prim != nullptr) {
            const std::shared_ptr<Prefab_instance> prefab_instance = erhe::scene::get_attachment<Prefab_instance>(prim);
            if (prefab_instance) {
                out_prefab_instance = prefab_instance;
                return hierarchy;
            }
        }
        const std::shared_ptr<erhe::Hierarchy> parent = hierarchy->get_parent().lock();
        hierarchy = parent.get();
    }
    return nullptr;
}

} // anonymous namespace

auto get_structural_hierarchy(const erhe::Item_base& item) -> const erhe::Hierarchy*
{
    const erhe::Hierarchy* hierarchy = dynamic_cast<const erhe::Hierarchy*>(&item);
    if (hierarchy != nullptr) {
        return hierarchy;
    }
    const erhe::scene::Node_attachment* attachment = dynamic_cast<const erhe::scene::Node_attachment*>(&item);
    if (attachment != nullptr) {
        return attachment->get_node();
    }
    return nullptr;
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
    std::shared_ptr<Prefab_instance>       prefab_instance{};
    const erhe::Hierarchy*                 carrier = find_carrier(parent.get(), prefab_instance);
    if (carrier == nullptr) {
        return {};
    }
    return fmt::format(
        "'{}' is inside the reference instance '{}'; deactivate it or edit the referenced file '{}'",
        hierarchy->get_path(), carrier->get_name(), erhe::file::to_string(prefab_instance->get_prefab_source_path())
    );
}

auto instance_child_refusal(const erhe::Hierarchy& parent) -> std::optional<std::string>
{
    std::shared_ptr<Prefab_instance> prefab_instance{};
    const erhe::Hierarchy*           carrier = find_carrier(&parent, prefab_instance);
    if (carrier == nullptr) {
        return {};
    }
    if (carrier == &parent) {
        return fmt::format(
            "'{}' is a reference instance: its content comes from '{}'; edit the referenced file to add to it",
            parent.get_path(), erhe::file::to_string(prefab_instance->get_prefab_source_path())
        );
    }
    return fmt::format(
        "'{}' is inside the reference instance '{}'; deactivate it or edit the referenced file '{}'",
        parent.get_path(), carrier->get_name(), erhe::file::to_string(prefab_instance->get_prefab_source_path())
    );
}

auto is_instance_structure_protected(const erhe::Item_base& item) -> bool
{
    const erhe::Hierarchy* hierarchy = get_structural_hierarchy(item);
    if (hierarchy == nullptr) {
        return false;
    }
    const std::shared_ptr<erhe::Hierarchy> parent = hierarchy->get_parent().lock();
    std::shared_ptr<Prefab_instance>       prefab_instance{};
    return find_carrier(parent.get(), prefab_instance) != nullptr;
}

auto refuses_instance_child(const erhe::Hierarchy& parent) -> bool
{
    std::shared_ptr<Prefab_instance> prefab_instance{};
    return find_carrier(&parent, prefab_instance) != nullptr;
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
        std::shared_ptr<Prefab_instance> prefab_instance{};
        const erhe::scene::Xformable*    parent_prim = dynamic_cast<const erhe::scene::Xformable*>(parent.get());
        if (parent_prim != nullptr) {
            prefab_instance = erhe::scene::get_attachment<Prefab_instance>(parent_prim);
        }
        if (prefab_instance) {
            result.carrier         = parent.get();
            result.prefab_instance = prefab_instance;
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

auto is_sealed_prefab_instance(const Prefab_instance& prefab_instance) -> bool
{
    return !is_usd_file_extension(prefab_instance.get_prefab_source_path());
}

} // namespace editor
