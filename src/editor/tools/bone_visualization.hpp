#pragma once

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <set>
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
class App_message_bus;
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
//
// Every input drives its own part of the state; there is no per-frame update:
//   - Skin_registered_message (Scene_root::register_skin / unregister_skin)
//     creates and drops the proxy set.
//   - Close_scene_message drops the closed scene's proxies and its material
//     registration.
//   - Node_touched_message and Animation_update_message refresh the bone shape
//     (tail offset / length).
//   - Selection_message and a direct call from Hover_tool (update_hover) swap
//     the selected / hovered materials.
//   - Mesh_component_mode_changed_message gates visibility and pickability on
//     bone selection mode.
//   - The settings UI calls apply_style_colors / apply_style_shape at the edit.
class Bone_visualization
{
public:
    // The message bus is passed explicitly: construction happens inside the
    // init taskflow, before fill_app_context() populates the App_context
    // pointers. Everything else read through m_context is only touched at
    // runtime, after they are set.
    Bone_visualization(App_context& context, App_message_bus& app_message_bus, erhe::scene_renderer::Mesh_memory& mesh_memory);
    ~Bone_visualization() noexcept;

    // Push the current Debug_visualizations_style bone colors into the two bone
    // materials. Called at material creation and from the settings UI at the
    // moment a bone color is edited - not polled per frame.
    void apply_style_colors();

    // Re-read Debug_visualizations_style bone width and solid style. Called
    // from the settings UI at the moment either value is edited (the same
    // pattern as apply_style_colors). Width rebuilds the proxy transforms;
    // solid re-derives visibility. In bone selection mode the proxies are
    // pickable and visible whether or not the solid style is on - you cannot
    // click what you cannot see; outside it they are visible only for the
    // solid style and a click passes straight through to the mesh.
    void apply_style_shape();

    // Hover material swap, called directly by Hover_tool::on_hover_mesh at the
    // moment the hovered node changes. A hover_mesh subscription here could run
    // before Hover_tool's own (subscriber order is registration order), reading
    // the joint's hovered flag before it was set - the direct call cannot.
    void update_hover(const erhe::scene::Node* old_joint, const erhe::scene::Node* new_joint);

    // The joint a proxy mesh stands for; null when the mesh is not a bone proxy.
    [[nodiscard]] auto get_joint_for_proxy(const erhe::scene::Mesh* mesh) const -> std::shared_ptr<erhe::scene::Node>;


private:
    class Proxy
    {
    public:
        std::weak_ptr<erhe::scene::Node>   joint     {};
        // The skin this proxy was created for: the weak ref feeds
        // bone_tail_in_joint_space on shape refresh, the raw key matches the
        // unregister message without locking.
        std::weak_ptr<erhe::scene::Skin>   skin      {};
        const erhe::scene::Skin*           skin_key  {nullptr};
        std::size_t                        joint_index{0};
        std::shared_ptr<erhe::scene::Node> node      {};
        std::shared_ptr<erhe::scene::Mesh> mesh      {};
        glm::vec3                          tail_local{0.0f}; // shape the transform was built from
        // Negative sentinel: width is never negative, so a fresh proxy always
        // fails the "shape unchanged" compare and gets its first transform.
        float                              width_scale{-1.0f};
        bool                               selected  {false}; // material currently applied
        bool                               hovered   {false}; // material currently applied
    };

    void ensure_primitive();
    // R5.2b explicit registration: the bone materials are created here, so they
    // must be listed in the content library of every scene whose proxies use
    // them. Without it a proxy carries a material with no buffer slot and the
    // shader falls back to a default - which is only visible on the selected
    // bones, since the unselected ones are drawn through the vdotn override
    // that replaces the fragment color anyway. Idempotent per scene root.
    void register_materials(Scene_root& scene_root);
    auto make_proxy(const std::shared_ptr<erhe::scene::Node>& joint) -> Proxy;
    void set_proxy_transform(Proxy& proxy, glm::vec3 tail_local);

    // Message / call targets. Each updates exactly the state that depends on
    // the change it announces.
    void on_skin_registered  (Skin_registered_message& message);
    void on_close_scene      (Close_scene_message& message);
    void on_selection        (Selection_message& message);
    void on_node_touched     (erhe::scene::Node* node);
    void on_animation_update ();
    void on_mode_changed     ();

    void add_skin_proxies    (Scene_root& scene_root, const std::shared_ptr<erhe::scene::Skin>& skin);
    void remove_skin_proxies (const erhe::scene::Skin* skin);
    void refresh_proxy_shape (Proxy& proxy);
    void apply_proxy_flags   (Proxy& proxy);
    void update_proxy_material(Proxy& proxy);

    App_context&                               m_context;
    erhe::scene_renderer::Mesh_memory&         m_mesh_memory;
    std::shared_ptr<erhe::primitive::Primitive> m_bone_primitive{};
    std::shared_ptr<erhe::primitive::Material>  m_material         {};
    std::shared_ptr<erhe::primitive::Material>  m_selected_material{};
    std::shared_ptr<erhe::primitive::Material>  m_hover_material   {};
    float                                      m_width_scale{0.1f};
    bool                                       m_solid      {false};
    bool                                       m_bone_mode  {false};

    erhe::message_bus::Subscription<Skin_registered_message>             m_skin_registered_subscription;
    erhe::message_bus::Subscription<Close_scene_message>                 m_close_scene_subscription;
    erhe::message_bus::Subscription<Selection_message>                   m_selection_subscription;
    erhe::message_bus::Subscription<Node_touched_message>                m_node_touched_subscription;
    erhe::message_bus::Subscription<Animation_update_message>            m_animation_update_subscription;
    erhe::message_bus::Subscription<Mesh_component_mode_changed_message> m_mode_changed_subscription;

    // Keyed by joint node pointer; the entry holds a weak ref so a dropped joint
    // is detected when its skin unregisters or its scene closes.
    std::unordered_map<const erhe::scene::Node*, Proxy> m_proxies;
    // Reverse lookup for picking: proxy mesh -> joint node.
    std::unordered_map<const erhe::scene::Mesh*, std::weak_ptr<erhe::scene::Node>> m_joint_by_proxy_mesh;
    // Scene roots the bone materials have been registered with.
    std::set<const Scene_root*> m_material_scene_roots;
};

}
