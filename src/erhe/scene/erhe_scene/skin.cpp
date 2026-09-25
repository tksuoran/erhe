#include "erhe_scene/skin.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"

namespace erhe::scene {

const erhe::property::Property<std::string> Skin::skeleton_property = erhe::property::Property<std::string>::register_computed(
    "skeleton", Skin::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        const std::shared_ptr<Node>& skeleton = static_cast<const Skin&>(object).skin_data.skeleton;
        return skeleton ? skeleton->get_name() : std::string{};
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "Skin", .tooltip = "Name of the skeleton root node; empty when the skin has none (computed)", .label = "Skeleton"}
    }
);
const erhe::property::Property<int> Skin::joint_count_property = erhe::property::Property<int>::register_computed(
    "joint_count", Skin::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        return static_cast<int>(static_cast<const Skin&>(object).skin_data.joints.size());
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "Skin", .tooltip = "Number of joint nodes (computed)", .label = "Joint Count"}
    }
);

auto Skin_data::get_world_from_bind(const std::size_t joint_index) const -> std::optional<glm::mat4>
{
    if (joint_index >= joints.size()) {
        return std::nullopt;
    }
    const std::shared_ptr<Node>& joint = joints[joint_index];
    if (!joint) {
        return std::nullopt;
    }
    const glm::mat4 joint_from_bind = (joint_index < inverse_bind_matrices.size())
        ? inverse_bind_matrices[joint_index]
        : glm::mat4{1.0f};
    return joint->world_from_node() * joint_from_bind;
}

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

auto get_bind_pose_local_rotation(const Node& node) -> std::optional<glm::quat>
{
    const std::shared_ptr<Node> parent = node.get_parent_node();
    const Scene* const          scene  = node.get_scene();
    if (!parent || (scene == nullptr)) {
        return std::nullopt;
    }
    for (const std::shared_ptr<Skin>& skin : scene->get_skins()) {
        if (!skin) {
            continue;
        }
        const std::vector<std::shared_ptr<Node>>& joints = skin->skin_data.joints;
        std::size_t node_index  {joints.size()};
        std::size_t parent_index{joints.size()};
        for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
            if (joints[i].get() == &node) {
                node_index = i;
            }
            if (joints[i] == parent) {
                parent_index = i;
            }
        }
        if ((node_index == joints.size()) || (parent_index == joints.size())) {
            continue;
        }
        // An inverse bind matrix maps the skin's bind space into the joint's
        // space at bind time, so its inverse is the joint's bind-time frame.
        // Not get_world_from_bind(): that is the skinning matrix, the joint's
        // deviation from bind, which follows the current pose.
        const std::vector<glm::mat4>& inverse_binds = skin->skin_data.inverse_bind_matrices;
        const glm::mat4 joint_from_bind  = (node_index   < inverse_binds.size()) ? inverse_binds[node_index]   : glm::mat4{1.0f};
        const glm::mat4 parent_from_bind = (parent_index < inverse_binds.size()) ? inverse_binds[parent_index] : glm::mat4{1.0f};
        const glm::mat4 parent_from_joint_bind = parent_from_bind * glm::inverse(joint_from_bind);
        const glm::mat3 basis{
            glm::normalize(glm::vec3{parent_from_joint_bind[0]}),
            glm::normalize(glm::vec3{parent_from_joint_bind[1]}),
            glm::normalize(glm::vec3{parent_from_joint_bind[2]})
        };
        return glm::normalize(glm::quat_cast(basis));
    }
    return std::nullopt;
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

