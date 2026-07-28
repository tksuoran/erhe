#include "erhe_scene/skin.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"

namespace erhe::scene {

namespace {

// Lowest common ancestor of two nodes, counting each node as an ancestor of
// itself (lca(n, n) == n, and lca(parent, child) == parent). Returns nullptr
// when the two nodes are in different trees.
[[nodiscard]] auto lowest_common_ancestor(
    std::shared_ptr<Node> lhs,
    std::shared_ptr<Node> rhs
) -> std::shared_ptr<Node>
{
    if (!lhs || !rhs) {
        return {};
    }
    while (lhs->get_depth() > rhs->get_depth()) {
        lhs = lhs->get_parent_node();
        if (!lhs) {
            return {};
        }
    }
    while (rhs->get_depth() > lhs->get_depth()) {
        rhs = rhs->get_parent_node();
        if (!rhs) {
            return {};
        }
    }
    while (lhs != rhs) {
        lhs = lhs->get_parent_node();
        rhs = rhs->get_parent_node();
        if (!lhs || !rhs) {
            return {};
        }
    }
    return lhs;
}

} // anonymous namespace

Skin::Skin()                       = default;
Skin::Skin(const Skin&)            = default;
Skin& Skin::operator=(const Skin&) = default;
Skin::~Skin() noexcept             = default;

Skin::Skin(const std::string_view name)
    : Item{name}
{
}

auto operator<(const Skin& lhs, const Skin& rhs) -> bool
{
    return lhs.get_id() < rhs.get_id();
}

auto get_skin_transform_root(const Skin& skin) -> std::shared_ptr<Node>
{
    const Skin_data& skin_data = skin.skin_data;
    if (skin_data.skeleton) {
        return skin_data.skeleton;
    }

    std::shared_ptr<Node> root{};
    for (const std::shared_ptr<Node>& joint : skin_data.joints) {
        if (!joint) {
            continue;
        }
        if (!root) {
            root = joint;
            continue;
        }
        root = lowest_common_ancestor(root, joint);
        if (!root) {
            return {}; // joints span disjoint trees
        }
    }
    return root;
}

using namespace erhe::utility;

auto is_bone(const Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    // Item_flags, not Item_type: Item_type is per-class (Item<>::get_type()
    // returns Self::get_static_type()), and a joint is an ordinary Node - there
    // is no Bone class for it to report. Item_flags::bone is set on the nodes a
    // Skin lists in skin_data.joints (see mark_skin_joints).
    return test_bit_set(item->get_flag_bits(), Item_flags::bone);
}

auto is_bone(const std::shared_ptr<Item_base>& item) -> bool
{
    return is_bone(item.get());
}

void mark_skin_joints(const Skin& skin)
{
    for (const std::shared_ptr<Node>& joint : skin.skin_data.joints) {
        if (joint) {
            joint->enable_flag_bits(Item_flags::bone);
        }
    }
}

} // namespace erhe::scene

