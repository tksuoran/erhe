#include "erhe_scene/skin.hpp"
#include "erhe_scene/mesh.hpp"
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

namespace {

[[nodiscard]] auto find_joint_index(const Skin& skin, const Node* const node) -> std::optional<std::size_t>
{
    const std::vector<std::shared_ptr<Node>>& joints = skin.skin_data.joints;
    for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
        if (joints[i].get() == node) {
            return i;
        }
    }
    return std::nullopt;
}

// An inverse bind matrix maps the skin's bind space into the joint's space at
// bind time, so its inverse is the joint's bind-time frame. Not
// get_world_from_bind(): that is the skinning matrix, the joint's deviation
// from bind, which follows the current pose.
[[nodiscard]] auto get_joint_from_bind(const Skin& skin, const std::size_t joint_index) -> glm::mat4
{
    const std::vector<glm::mat4>& inverse_binds = skin.skin_data.inverse_bind_matrices;
    return (joint_index < inverse_binds.size()) ? inverse_binds[joint_index] : glm::mat4{1.0f};
}

// World transform of the node of the first mesh that the skin deforms;
// identity when no mesh uses the skin.
[[nodiscard]] auto get_world_from_skinned_mesh(const Skin& skin, const Scene& scene) -> glm::mat4
{
    for (const std::shared_ptr<Mesh_layer>& layer : scene.get_mesh_layers()) {
        for (const std::shared_ptr<Mesh>& mesh : layer->meshes) {
            if (mesh && (mesh->skin.get() == &skin)) {
                return mesh->world_from_node();
            }
        }
    }
    return glm::mat4{1.0f};
}

} // anonymous namespace

auto find_skin_joint(const Node& node) -> std::optional<Skin_joint>
{
    const Scene* const scene = node.get_scene();
    if (scene == nullptr) {
        return std::nullopt;
    }
    for (const std::shared_ptr<Skin>& candidate : scene->get_skins()) {
        if (!candidate) {
            continue;
        }
        const std::optional<std::size_t> index = find_joint_index(*candidate, &node);
        if (index.has_value()) {
            return Skin_joint{.skin = candidate, .joint_index = index.value()};
        }
    }
    return std::nullopt;
}

auto get_bind_pose_parent_from_node(const Node& node) -> std::optional<glm::mat4>
{
    const Scene* const scene = node.get_scene();
    if (scene == nullptr) {
        return std::nullopt;
    }
    const std::shared_ptr<Node> parent = node.get_parent_node();

    // The skin: the first listing the node and its parent, else the first
    // listing the node.
    const Skin*                skin{nullptr};
    std::size_t                joint_index{0};
    std::optional<std::size_t> parent_index{};
    for (const std::shared_ptr<Skin>& candidate : scene->get_skins()) {
        if (!candidate) {
            continue;
        }
        const std::optional<std::size_t> index = find_joint_index(*candidate, &node);
        if (!index.has_value()) {
            continue;
        }
        const std::optional<std::size_t> candidate_parent_index = parent ? find_joint_index(*candidate, parent.get()) : std::nullopt;
        if (candidate_parent_index.has_value()) {
            skin         = candidate.get();
            joint_index  = index.value();
            parent_index = candidate_parent_index;
            break;
        }
        if (skin == nullptr) {
            skin        = candidate.get();
            joint_index = index.value();
        }
    }
    if (skin == nullptr) {
        return std::nullopt;
    }

    const glm::mat4 joint_from_bind = get_joint_from_bind(*skin, joint_index);
    if (parent_index.has_value()) {
        return get_joint_from_bind(*skin, parent_index.value()) * glm::inverse(joint_from_bind);
    }

    const glm::mat4 world_from_mesh = get_world_from_skinned_mesh(*skin, *scene);
    const glm::mat4 world_from_bind = world_from_mesh * glm::inverse(joint_from_bind);
    if (!parent) {
        return world_from_bind;
    }
    // The parent's world once the skin's joints sit on their bind pose: the
    // nearest ancestor joint's bind-time world carried down by the local
    // transforms of the non-joint nodes between.
    glm::mat4 ancestor_from_parent{1.0f};
    for (std::shared_ptr<Node> ancestor = parent; ancestor; ancestor = ancestor->get_parent_node()) {
        const std::optional<std::size_t> ancestor_index = find_joint_index(*skin, ancestor.get());
        if (ancestor_index.has_value()) {
            const glm::mat4 world_from_parent = world_from_mesh * glm::inverse(get_joint_from_bind(*skin, ancestor_index.value())) * ancestor_from_parent;
            return glm::inverse(world_from_parent) * world_from_bind;
        }
        ancestor_from_parent = ancestor->parent_from_node() * ancestor_from_parent;
    }
    return glm::inverse(parent->world_from_node()) * world_from_bind;
}

using namespace erhe::utility;

auto is_bone(const Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    // Item_flags, not Item_type: Item_type is per-class (Item<>::get_type()
    // returns Self::get_static_type()), and a bone is an ordinary Node - there
    // is no Bone class for it to report. Item_flags::bone is authored (and set
    // on the nodes a Skin lists in skin_data.joints, see mark_skin_joints).
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

