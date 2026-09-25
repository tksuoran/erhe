#pragma once

#include "app_message.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::primitive {
    class Material;
    class Primitive;
}
namespace erhe::scene {
    class Mesh;
    class Xformable; using Node = Xformable;
    class Skin;
}
namespace erhe::scene_renderer {
    class Mesh_memory;
}

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

// Editor-generated, pickable proxy geometry for skeleton bones.
//
// Two shared unit-bone Primitives - the octahedron (head at the origin, tail
// at +Y, square ring at y = 0.1) and a square prism (x, z in [-1, 1], y in
// [0, 1]) - are instanced once per bone (a node carrying Item_flags::bone in
// an editor scene, skinned or not): a proxy Node parented under the bone
// node, carrying a Mesh in the scene's bone layer. The bone's
// Rig.display_shape (doc/plans/rigging/skeleton_editing.md R17) picks the
// primitive - octahedral: the octahedron; box: the prism as wide as the
// octahedron's ring; stick: the prism a quarter as wide. The tail is the
// bone's Rig.tail (R3), read at each shape refresh. The per-instance
// transform does all the work - orient +Y onto the tail direction, scale by
// (width, length, width) - which is what lets the raytrace side reuse one BVH
// geometry per shape and pose it with the instance transform.
//
// The solid style shades an unselected proxy N.V times its material's color
// (Shader_debug::vdotn_tinted): a bone in the style colors
// (Rig.display_color_mode `style`) carries the white "bone" material (plain
// N.V grey), a `custom` bone a material of its Rig.display_color - one
// builtin material per distinct color (8 bits per channel), shared by the
// bones of every scene that use the color.
//
// Proxies live in the content scene (so they inherit joint transforms for free)
// but are flagged Item_flags::bone_proxy, which keeps them out of the item tree,
// save, export and prefabs, and out of picking unless bone mode asks for them.
//
// Every input drives its own part of the state; there is no per-frame update:
//   - Bone_changed_message (Rig_system, rig/rig_system.hpp: a bone entering or
//     leaving its scene, the bone flag, a Rig.tail or Rig.display_* edit)
//     creates, drops, reshapes or re-materials the node's proxy and reshapes
//     its parent's.
//   - Skin_registered_message (Scene_root::register_skin / unregister_skin)
//     reconciles the skin's joints, whose default tails follow the skin.
//   - Close_scene_message drops the closed scene's proxies and its material
//     registration.
//   - Node_touched_message and Animation_update_message refresh the bone shape
//     (a default tail follows the child bone's head).
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

    // Push the current Debug_visualizations_style bone colors into the
    // selected and hover bone materials. Called at material creation and from the settings UI at the
    // moment a bone color is edited - not polled per frame.
    void apply_style_colors();

    // Re-read Debug_visualizations_style bone aspect ratio and solid style. Called
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

    // The bone's tail in its local frame as the display last read it (the
    // proxy's shape), for the line style drawn every frame; Rig.tail read
    // directly for a node without a proxy.
    [[nodiscard]] auto get_bone_tail(const erhe::scene::Node& joint) const -> glm::vec3;


private:
    class Proxy
    {
    public:
        std::weak_ptr<erhe::scene::Node>   joint     {};
        std::shared_ptr<erhe::scene::Node> node      {};
        std::shared_ptr<erhe::scene::Mesh> mesh      {};
        glm::vec3                          tail_local{0.0f}; // shape the transform was built from
        Bone_display_shape                 shape     {Bone_display_shape::octahedral}; // primitive the mesh carries
        // Negative sentinel: the ratio is never negative, so a fresh proxy always
        // fails the "shape unchanged" compare and gets its first transform.
        float                              aspect_ratio{-1.0f};
        bool                               selected  {false}; // flag currently mirrored
        bool                               hovered   {false}; // flag currently mirrored
    };

    void ensure_primitive();
    [[nodiscard]] auto get_shape_primitive  (Bone_display_shape shape) const -> const std::shared_ptr<erhe::primitive::Primitive>&;
    // The material of an unselected, unhovered proxy of `joint`: m_material
    // for the style colors, else the (created on first use) material of its
    // Rig.display_color.
    [[nodiscard]] auto get_display_material (const erhe::scene::Node& joint) -> std::shared_ptr<erhe::primitive::Material>;
    auto make_proxy(const std::shared_ptr<erhe::scene::Node>& joint) -> Proxy;
    void set_proxy_transform(Proxy& proxy, glm::vec3 tail_local);

    // Message / call targets. Each updates exactly the state that depends on
    // the change it announces.
    void on_skin_registered  (Skin_registered_message& message);
    void on_bone_changed     (Bone_changed_message& message);
    void on_close_scene      (Close_scene_message& message);
    void on_selection        (Selection_message& message);
    void on_node_touched     (erhe::scene::Node* node);
    void on_animation_update ();
    void on_mode_changed     ();

    // Creates, refreshes or drops the node's proxy to match whether it is a
    // bone of a scene now.
    void reconcile_bone      (const std::shared_ptr<erhe::scene::Node>& node);
    // Detaches the proxy's node; the caller erases the entry.
    void remove_proxy        (Proxy& proxy);
    void drop_expired_proxies();
    void refresh_proxy_shape (Proxy& proxy);
    void apply_proxy_flags   (Proxy& proxy);
    void update_proxy_material(Proxy& proxy);

    App_context&                               m_context;
    erhe::scene_renderer::Mesh_memory&         m_mesh_memory;
    std::shared_ptr<erhe::primitive::Primitive> m_bone_primitive{}; // octahedral
    std::shared_ptr<erhe::primitive::Primitive> m_box_primitive {}; // stick, box
    std::shared_ptr<erhe::primitive::Material>  m_material         {};
    std::shared_ptr<erhe::primitive::Material>  m_selected_material{};
    std::shared_ptr<erhe::primitive::Material>  m_hover_material   {};
    // Rig.display_color materials keyed by the 8-bit RGB they carry
    // (0xRRGGBB); builtin assets, kept for the session like the three above.
    std::map<uint32_t, std::shared_ptr<erhe::primitive::Material>> m_display_color_materials;
    float                                      m_aspect_ratio{0.1f};
    bool                                       m_solid      {false};
    bool                                       m_bone_mode  {false};

    erhe::message_bus::Subscription<Skin_registered_message>             m_skin_registered_subscription;
    erhe::message_bus::Subscription<Bone_changed_message>                m_bone_changed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>                 m_close_scene_subscription;
    erhe::message_bus::Subscription<Selection_message>                   m_selection_subscription;
    erhe::message_bus::Subscription<Node_touched_message>                m_node_touched_subscription;
    erhe::message_bus::Subscription<Animation_update_message>            m_animation_update_subscription;
    erhe::message_bus::Subscription<Mesh_component_mode_changed_message> m_mode_changed_subscription;

    // Keyed by bone node pointer; the entry holds a weak ref so a dropped bone
    // is detected when it is reported again or its scene closes.
    std::unordered_map<const erhe::scene::Node*, Proxy> m_proxies;
    // Reverse lookup for picking: proxy mesh -> joint node.
    std::unordered_map<const erhe::scene::Mesh*, std::weak_ptr<erhe::scene::Node>> m_joint_by_proxy_mesh;
};

}
