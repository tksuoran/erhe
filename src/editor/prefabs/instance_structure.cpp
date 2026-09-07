#include "prefabs/instance_structure.hpp"

#include "parsers/usd.hpp"
#include "prefabs/prefab_instance.hpp"

#include "erhe_file/file.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"

#include <fmt/format.h>

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

// The Hierarchy an item's structural position is that of: the item itself,
// or - for a Node_attachment, whose position in the tree is its prim's - the
// prim it is attached to.
[[nodiscard]] auto structural_hierarchy_of(const erhe::Item_base& item) -> const erhe::Hierarchy*
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

} // anonymous namespace

auto instance_structure_refusal(const erhe::Item_base& item) -> std::optional<std::string>
{
    const erhe::Hierarchy* hierarchy = structural_hierarchy_of(item);
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
    const erhe::Hierarchy* hierarchy = structural_hierarchy_of(item);
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

auto is_sealed_prefab_instance(const Prefab_instance& prefab_instance) -> bool
{
    return !is_usd_file_extension(prefab_instance.get_prefab_source_path());
}

} // namespace editor
