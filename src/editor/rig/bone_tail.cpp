#include "rig/bone_tail.hpp"

#include "scene/rig_properties.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include <algorithm>
#include <optional>

namespace editor {

namespace {

// Bone length estimate from the vertices the joint actually skins, measured
// along a caller-chosen joint-space direction (unit): the union of the
// per-primitive joint bounding boxes (rest pose, bind space - computed at
// import, see Buffer_mesh::joint_bounding_boxes) of every mesh skinned by
// `skin` is transformed into joint space by the inverse bind matrix, and the
// length is the farthest box corner's projection onto the direction. The
// direction itself is NOT derived here - the tail direction always comes from
// the hierarchy rules in infer_skinned_bone_tail; the bounds only size it.
// Empty when no mesh provides joint bounds for this joint, or when the box
// does not extend along the direction.
[[nodiscard]] auto bone_length_from_skinned_bounds(
    const erhe::scene::Skin& skin,
    const std::size_t        joint_index,
    const erhe::scene::Node& joint,
    const glm::vec3          direction
) -> std::optional<float>
{
    const erhe::scene::Scene* const scene = joint.get_scene();
    if (scene == nullptr) {
        return {};
    }

    erhe::math::Aabb bind_box{};
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& layer : scene->get_mesh_layers()) {
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : layer->meshes) {
            if (!mesh || (mesh->skin.get() != &skin)) {
                continue;
            }
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                if (!mesh_primitive.primitive) {
                    continue;
                }
                const erhe::primitive::Primitive_render_shape* const shape = mesh_primitive.primitive->render_shape.get();
                if (shape == nullptr) {
                    continue;
                }
                const std::vector<erhe::math::Aabb>& joint_boxes =
                    shape->get_renderable_mesh().joint_bounding_boxes;
                if ((joint_index >= joint_boxes.size()) || !joint_boxes[joint_index].is_valid()) {
                    continue;
                }
                bind_box.include(joint_boxes[joint_index]);
            }
        }
    }
    if (!bind_box.is_valid()) {
        return {};
    }

    const glm::mat4 inverse_bind = (joint_index < skin.skin_data.inverse_bind_matrices.size())
        ? skin.skin_data.inverse_bind_matrices[joint_index]
        : glm::mat4{1.0f};
    const erhe::math::Aabb joint_box = bind_box.transformed_by(inverse_bind);

    constexpr float epsilon = 1.0e-6f;
    float extent = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p{
            ((corner & 1) != 0) ? joint_box.max.x : joint_box.min.x,
            ((corner & 2) != 0) ? joint_box.max.y : joint_box.min.y,
            ((corner & 4) != 0) ? joint_box.max.z : joint_box.min.z
        };
        extent = std::max(extent, glm::dot(p, direction));
    }
    if (extent < epsilon) {
        return {};
    }
    return extent;
}

// Accumulates child local translations: whether there was a child, the first
// one, and whether every later one agrees with it.
class Child_translations
{
public:
    void add(const glm::vec3 translation)
    {
        if (!have_child) {
            have_child = true;
            first      = translation;
            return;
        }
        const float tolerance = std::max(1.0e-4f, 1.0e-3f * glm::length(first));
        if (glm::distance(translation, first) > tolerance) {
            agree = false;
        }
    }

    bool      have_child{false};
    bool      agree     {true};
    glm::vec3 first     {0.0f};
};

} // anonymous namespace

auto infer_skinned_bone_tail(const erhe::scene::Skin& skin, const std::size_t joint_index) -> glm::vec3
{
    const std::vector<std::shared_ptr<erhe::scene::Node>>& joints = skin.skin_data.joints;
    if (joint_index >= joints.size()) {
        return glm::vec3{0.2f, 0.0f, 0.0f};
    }
    const std::shared_ptr<erhe::scene::Node>& joint = joints[joint_index];
    if (!joint) {
        return glm::vec3{0.2f, 0.0f, 0.0f};
    }

    // Child joints agreeing on the tail win: their shared local translation IS
    // the tail offset. Multiple children that disagree (a hand joint fanning
    // into fingers) give no single answer, so the skinned-vertex bounds below
    // decide instead.
    Child_translations children;
    for (std::size_t j = 0, end = joints.size(); j < end; ++j) {
        if (j == joint_index) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node>& other = joints[j];
        if (!other || (other->get_parent_node() != joint)) {
            continue;
        }
        children.add(other->parent_from_node_transform().get_translation());
    }
    if (children.have_child && children.agree) {
        return children.first;
    }

    // Leaf joint, or children that don't agree: the DIRECTION still follows
    // the hierarchy (first child's direction when there were children, local
    // +Y for a leaf), and the skinned-vertex bounds only resize it - the
    // length becomes the box extent along that direction.
    constexpr float epsilon = 1.0e-6f;
    const float first_child_distance = glm::length(children.first);
    const glm::vec3 direction = (children.have_child && (first_child_distance > epsilon))
        ? (children.first / first_child_distance)
        : glm::vec3{0.0f, 1.0f, 0.0f};
    const std::optional<float> length_from_bounds = bone_length_from_skinned_bounds(skin, joint_index, *joint, direction);
    if (length_from_bounds.has_value()) {
        return direction * length_from_bounds.value();
    }

    // No skinned bounds available. Disagreeing children: fall back to the
    // first child.
    if (children.have_child) {
        return children.first;
    }

    // Leaf joint: point along local +Y, as long as this joint's own offset
    // from its parent.
    const std::shared_ptr<erhe::scene::Node> parent = joint->get_parent_node();
    if (parent) {
        const float length = glm::length(joint->parent_from_node_transform().get_translation());
        if (length > 0.0f) {
            return glm::vec3{0.0f, length, 0.0f};
        }
    }
    return glm::vec3{0.2f, 0.0f, 0.0f};
}

auto compute_default_bone_tail(const erhe::scene::Node& node) -> glm::vec3
{
    const std::optional<erhe::scene::Skin_joint> skin_joint = erhe::scene::find_skin_joint(node);
    if (skin_joint.has_value()) {
        return infer_skinned_bone_tail(*skin_joint.value().skin, skin_joint.value().joint_index);
    }

    // The first bone child's head; children that disagree (a hand fanning
    // into fingers) have no single answer and no skinned bounds to size one.
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const erhe::scene::Node* const child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
        if ((child_node != nullptr) && erhe::scene::is_bone(child_node)) {
            return child_node->parent_from_node_transform().get_translation();
        }
    }

    // A leaf under a bone parent keeps the parent's bone length. The parent
    // has this bone as a child, so its own default comes from its children
    // (or its skin) and never reads back here.
    const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
    if (parent && erhe::scene::is_bone(parent.get())) {
        const float length = glm::length(parent->get_value(Rig::tail_property()));
        if (length > 0.0f) {
            return glm::vec3{0.0f, length, 0.0f};
        }
    }
    return glm::vec3{0.0f, 1.0f, 0.0f};
}

} // namespace editor
