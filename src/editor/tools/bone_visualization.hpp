#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::primitive {
    class Material;
    class Primitive;
}
namespace erhe::scene {
    class Mesh;
    class Node;
    class Skin;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
}

namespace editor {

class App_context;
class Scene_root;

// Where a bone's tail sits, expressed in its joint node's LOCAL space (the head
// is always the joint origin). Local rather than world on purpose: the value
// depends only on the child joint's local translation, so a rotation-only
// animation - the common case - never changes it, and a proxy parented under
// the joint needs no per-frame transform refresh at all.
//
// Rule (unchanged from the long-standing line visualization, factored out here
// so lines and solid bones cannot disagree): the first child joint's local
// translation; else, for a joint with a parent, the parent offset length along
// local +Y; else a short local +X stub.
[[nodiscard]] auto bone_tail_in_joint_space(const erhe::scene::Skin& skin, std::size_t joint_index) -> glm::vec3;

// Editor-generated, pickable proxy geometry for skeleton bones.
//
// One shared unit-bone Primitive (head at the origin, tail at +Y, square ring at
// y = 0.1) is instanced once per joint: a proxy Node parented under the joint
// node, carrying a Mesh in the scene's bone layer. The per-instance transform
// does all the work - orient +Y onto the tail direction, scale by (width,
// length, width) - which is what lets the raytrace side reuse a single BVH
// geometry and pose it with the instance transform.
//
// Proxies live in the content scene (so they inherit joint transforms for free)
// but are flagged Item_flags::bone_proxy, which keeps them out of the item tree,
// save, export and prefabs, and out of picking unless bone mode asks for them.
class Bone_visualization
{
public:
    Bone_visualization(App_context& context, erhe::scene_renderer::Mesh_memory& mesh_memory);
    ~Bone_visualization() noexcept;

    // Reconcile proxies with the skins of every open scene: create one per joint,
    // drop those whose joint or skin is gone, and refresh transforms whose bone
    // shape changed. `visible` shows/hides them; `pickable` toggles
    // Item_flags::id and the raytrace mask, so outside bone mode a click passes
    // straight through to the mesh. Called once per frame from the editor tick.
    void update(bool visible, bool pickable);

    // The joint a proxy mesh stands for; null when the mesh is not a bone proxy.
    [[nodiscard]] auto get_joint_for_proxy(const erhe::scene::Mesh* mesh) const -> std::shared_ptr<erhe::scene::Node>;

    // Width of a bone as a fraction of its length, and the absolute floor that
    // keeps a degenerate (zero-length) bone pickable.
    void set_width_scale(float width_scale);
    [[nodiscard]] auto get_width_scale() const -> float;

private:
    class Proxy
    {
    public:
        std::weak_ptr<erhe::scene::Node> joint      {};
        std::shared_ptr<erhe::scene::Node> node     {};
        std::shared_ptr<erhe::scene::Mesh> mesh     {};
        glm::vec3                          tail_local{0.0f}; // shape the transform was built from
        float                              width_scale{0.0f};
        bool                               alive     {false};
    };

    void ensure_primitive();
    void update_scene(Scene_root& scene_root, bool visible, bool pickable);
    auto make_proxy(const std::shared_ptr<erhe::scene::Node>& joint) -> Proxy;
    void set_proxy_transform(Proxy& proxy, glm::vec3 tail_local);

    App_context&                               m_context;
    erhe::scene_renderer::Mesh_memory&         m_mesh_memory;
    std::shared_ptr<erhe::primitive::Primitive> m_bone_primitive{};
    std::shared_ptr<erhe::primitive::Material>  m_material      {};
    float                                      m_width_scale{0.1f};

    // Keyed by joint node pointer; the entry holds a weak ref so a dropped joint
    // is detected on the next sweep.
    std::unordered_map<const erhe::scene::Node*, Proxy> m_proxies;
    // Reverse lookup for picking: proxy mesh -> joint node.
    std::unordered_map<const erhe::scene::Mesh*, std::weak_ptr<erhe::scene::Node>> m_joint_by_proxy_mesh;
};

}
