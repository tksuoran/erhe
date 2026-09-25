#include "rig/bone_bind.hpp"

#include "scene/rig_properties.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <optional>

namespace editor {

auto get_point_segment_distance_squared(const glm::vec3& point, const Bone_segment& segment) -> float
{
    const glm::vec3 axis           = segment.tail - segment.head;
    const float     length_squared = glm::dot(axis, axis);
    if (length_squared <= 0.0f) {
        const glm::vec3 d = point - segment.head;
        return glm::dot(d, d);
    }
    const float     t       = std::clamp(glm::dot(point - segment.head, axis) / length_squared, 0.0f, 1.0f);
    const glm::vec3 nearest = segment.head + (t * axis);
    const glm::vec3 d       = point - nearest;
    return glm::dot(d, d);
}

auto find_nearest_bone_segment(const std::span<const Bone_segment> segments, const glm::vec3& point) -> std::size_t
{
    ERHE_VERIFY(!segments.empty());
    std::size_t best_index    = 0;
    float       best_distance = get_point_segment_distance_squared(point, segments[0]);
    for (std::size_t i = 1, end = segments.size(); i < end; ++i) {
        const float distance = get_point_segment_distance_squared(point, segments[i]);
        if (distance < best_distance) {
            best_distance = distance;
            best_index    = i;
        }
    }
    return best_index;
}

namespace {

class Rest_world_solver
{
public:
    explicit Rest_world_solver(const std::vector<std::shared_ptr<erhe::scene::Node>>& joints)
        : m_joints{joints}
        , m_results(joints.size())
    {
    }

    auto get(const std::size_t joint_index) -> glm::mat4
    {
        if (m_results[joint_index].has_value()) {
            return m_results[joint_index].value();
        }
        // The frame above the joint: its nearest ancestor joint's rest world
        // carried down by the current locals of the nodes between, else the
        // product of every ancestor's local (the parent's current world).
        glm::mat4 anchor_from_parent{1.0f};
        glm::mat4 world_from_anchor {1.0f};
        for (
            std::shared_ptr<erhe::scene::Node> ancestor = m_joints[joint_index]->get_parent_node();
            ancestor;
            ancestor = ancestor->get_parent_node()
        ) {
            const std::optional<std::size_t> ancestor_index = find(ancestor.get());
            if (ancestor_index.has_value()) {
                world_from_anchor = get(ancestor_index.value());
                break;
            }
            anchor_from_parent = ancestor->parent_from_node() * anchor_from_parent;
        }
        const glm::mat4 rest_world = world_from_anchor * anchor_from_parent * read_rest_transform(*m_joints[joint_index]).get_matrix();
        m_results[joint_index] = rest_world;
        return rest_world;
    }

private:
    [[nodiscard]] auto find(const erhe::scene::Node* const node) const -> std::optional<std::size_t>
    {
        for (std::size_t i = 0, end = m_joints.size(); i < end; ++i) {
            if (m_joints[i].get() == node) {
                return i;
            }
        }
        return std::nullopt;
    }

    const std::vector<std::shared_ptr<erhe::scene::Node>>& m_joints;
    std::vector<std::optional<glm::mat4>>                  m_results;
};

} // anonymous namespace

auto get_rest_world_transforms(const std::vector<std::shared_ptr<erhe::scene::Node>>& joints) -> std::vector<glm::mat4>
{
    Rest_world_solver      solver{joints};
    std::vector<glm::mat4> result;
    result.reserve(joints.size());
    for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
        result.push_back(solver.get(i));
    }
    return result;
}

auto get_rest_bone_segments(
    const std::vector<std::shared_ptr<erhe::scene::Node>>& joints,
    const std::span<const glm::mat4>                       rest_world_transforms
) -> std::vector<Bone_segment>
{
    ERHE_VERIFY(joints.size() == rest_world_transforms.size());
    std::vector<Bone_segment> segments;
    segments.reserve(joints.size());
    for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
        const glm::mat4& world_from_rest = rest_world_transforms[i];
        const glm::vec3  tail_local      = joints[i]->get_value(Rig::tail_property());
        segments.push_back(
            Bone_segment{
                .head = glm::vec3{world_from_rest * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}},
                .tail = glm::vec3{world_from_rest * glm::vec4{tail_local, 1.0f}}
            }
        );
    }
    return segments;
}

} // namespace editor
