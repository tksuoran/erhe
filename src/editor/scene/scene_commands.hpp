#pragma once

#include "config/generated/add_cameras_args.hpp"
#include "config/generated/add_lights_args.hpp"
#include "config/generated/add_room_args.hpp"
#include "scene/make_mesh_config.hpp"

#include "app_message.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <memory>
#include <string_view>
#include <vector>

struct Make_mesh_args;

class btCollisionShape;

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::physics {
    class Collision_filter;
    class IRigid_body_create_info;
    class Physics_joint_settings;
    class Physics_material;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Layout;
    class Light;
    class Mesh;
    class Xformable; using Node = Xformable;
    class Node_attachment;
}

namespace editor {

class App_context;
class App_message_bus;
class Draw_mode;
class Frame_controller;
class Graph_mesh;
class Graph_texture;
class Grid;
class Headset_view;
class Mesh_rendertarget_view;
class Node_joint;
class Node_physics;
class Operation_stack;
class Rendertarget_mesh;
class Rendertarget_imgui_host;
class Scene_commands;
class Scene_root;
class Selection_tool;
class Scene_views;
class Style;

class Create_new_scene_command : public erhe::commands::Command
{
public:
    Create_new_scene_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_camera_command : public erhe::commands::Command
{
public:
    Create_new_camera_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_xform_command : public erhe::commands::Command
{
public:
    Create_new_xform_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_scope_command : public erhe::commands::Command
{
public:
    Create_new_scope_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_light_command : public erhe::commands::Command
{
public:
    Create_new_light_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_rendertarget_command : public erhe::commands::Command
{
public:
    Create_new_rendertarget_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_layout_command : public erhe::commands::Command
{
public:
    Create_new_layout_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_rigid_body_command : public erhe::commands::Command
{
public:
    Create_new_rigid_body_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_joint_command : public erhe::commands::Command
{
public:
    Create_new_joint_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Add_cameras_command : public erhe::commands::Command
{
public:
    Add_cameras_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void apply_args(const Add_cameras_args& args);

private:
    App_context&     m_context;
    Add_cameras_args m_args{};
};

class Add_room_command : public erhe::commands::Command
{
public:
    Add_room_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void apply_args(const Add_room_args& args);

private:
    App_context&  m_context;
    Add_room_args m_args{};
};

class Add_lights_command : public erhe::commands::Command
{
public:
    Add_lights_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void apply_args(const Add_lights_args& args);

private:
    App_context&    m_context;
    Add_lights_args m_args{};
};

class Add_platonic_solids_command : public erhe::commands::Command
{
public:
    Add_platonic_solids_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_johnson_solids_command : public erhe::commands::Command
{
public:
    Add_johnson_solids_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_curved_shapes_command : public erhe::commands::Command
{
public:
    Add_curved_shapes_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_chain_command : public erhe::commands::Command
{
public:
    Add_chain_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_toruses_command : public erhe::commands::Command
{
public:
    Add_toruses_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Scene_commands
{
public:
    Scene_commands(erhe::commands::Commands& commands, App_context& context, App_message_bus& app_message_bus);

    // Public API

    // Creates a new empty scene: a fresh Scene_root with its own (empty)
    // content library, holding just a default camera -- no lights, no meshes.
    // The scene is registered to the editor scene list, given its browser
    // window and a viewport window looking through the camera, and announced
    // via Scene_created_message. Not undoable (like loading a scene).
    //
    // Registers ImGui windows, so this must NOT be called from inside ImGui
    // window iteration (e.g. from a menu); the scene.create_new_scene command
    // queues Create_scene_message and the actual creation runs from the
    // message bus pump.
    auto create_new_scene       () -> std::shared_ptr<Scene_root>;

    // Prim creation. Any prim may parent any other prim
    // (doc/erhe/usd_compatibility_design.md C5), so every creator below takes the
    // parent as the Hierarchy it is: a Camera is created under a Scope or a
    // Material as readily as under an Xform. Each queues one undoable insert
    // of the new prim as the last child of `parent`; without a parent the
    // prim lands under the active scene's root node.
    auto create_new_camera      (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::scene::Camera>;
    auto create_new_xform       (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::scene::Node>;
    // An empty Mesh prim (no primitives).
    auto create_new_mesh        (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::scene::Mesh>;
    // A Scope prim: children and no transform, the prim resources are
    // conventionally gathered under (C5). Undoable, like every other creation
    // here; inserted on the next editor frame.
    auto create_new_scope       (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::Scope>;

    // Adds an empty child node at the tip of every leaf bone (a bone with no
    // bone children) in the target subtrees, placed with
    // bone_tail_in_joint_space. Targets: the selection when clicked_node is
    // selected, otherwise clicked_node's subtree alone. One undoable compound
    // operation; returns the number of tip nodes created.
    auto add_bone_tip_nodes(const std::shared_ptr<erhe::scene::Node>& clicked_node) -> std::size_t;
    auto create_new_light       (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::scene::Light>;
    // An Xform carrying a Layout attachment.
    auto create_new_layout      (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::scene::Layout>;
    // An Xform holding a Rendertarget_mesh showing a viewport of the selected
    // camera; returns empty when no camera is selected.
    auto create_new_rendertarget(erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<Rendertarget_mesh>;

    // Resource prim creation (doc/erhe/usd_compatibility_design.md U4): each queues
    // one undoable insert of a new resource as the last child of `parent`,
    // any prim; without a parent the resource lands in its kind scope of the
    // active scene's content library (make_library_insert_operation). The
    // content library indexes the resource wherever it sits.
    auto create_new_material        (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::primitive::Material>;
    auto create_new_physics_material(erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::physics::Physics_material>;
    auto create_new_collision_filter(erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::physics::Collision_filter>;
    auto create_new_joint_settings  (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<erhe::physics::Physics_joint_settings>;
    // An empty style named uniquely in the scene's library (doc/editor/style_library.md R1).
    auto create_new_style           (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<Style>;
    // A texture graph asset; the Texture Graph window is pointed at it (#252).
    auto create_new_graph_texture   (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<Graph_texture>;
    // A geometry graph asset; the Geometry Graph window is pointed at it (#252).
    auto create_new_graph_mesh      (erhe::Hierarchy* parent = nullptr) -> std::shared_ptr<Graph_mesh>;

    // Attaches a new Node_physics to the node (undoable). With no node, uses
    // the last selected node; with no selection, creates a new empty node
    // with the body. The default create info is a dynamic body of mass 1
    // with a convex hull shape built from the node's mesh (a unit box when
    // the node has no usable mesh geometry). Returns empty when the node
    // already has a Node_physics (one rigid body per node).
    auto create_new_rigid_body(erhe::scene::Node* node = nullptr) -> std::shared_ptr<Node_physics>;
    auto create_new_rigid_body(erhe::scene::Node* node, const erhe::physics::IRigid_body_create_info& create_info) -> std::shared_ptr<Node_physics>;

    // Attaches a new Node_joint to the node (undoable), joining the nearest
    // self-or-ancestor rigid body of the node to that of connected_node (no
    // connected node = the world). Settings may be empty (a free six-dof
    // joint); assign shared Physics_joint_settings later in the properties.
    // With no node, uses the last selected node; with no selection, creates
    // a new empty node with the joint.
    auto create_new_joint(
        erhe::scene::Node*                                            node             = nullptr,
        const std::shared_ptr<erhe::scene::Node>&                     connected_node   = {},
        const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings         = {},
        bool                                                          enable_collision = false
    ) -> std::shared_ptr<Node_joint>;

    // Node attachment management (issue #249). Each of these attaches a new
    // attachment to an EXISTING in-scene node via a bare (undoable)
    // Node_attach_operation -- no node creation, no Compound_operation. They
    // gate single-instance kinds via get_attachment<T> and return empty when
    // the node already carries that attachment (Scene_root is resolved from the
    // node's item host). See scene/attachment_types.{hpp,cpp} for the user
    // catalog that drives them; Rigid Body / Joint reuse create_new_rigid_body
    // / create_new_joint above.
    auto attach_new_layout         (erhe::scene::Node& node) -> std::shared_ptr<erhe::scene::Layout>;
    auto attach_new_grid            (erhe::scene::Node& node) -> std::shared_ptr<Grid>;
    auto attach_new_frame_controller(erhe::scene::Node& node) -> std::shared_ptr<Frame_controller>;
    auto attach_new_draw_mode       (erhe::scene::Node& node) -> std::shared_ptr<Draw_mode>;

    // Queues an undoable pure detach of the attachment from its current node
    // (Node_attach_operation with an empty host node). No-op on a null pointer.
    void remove_attachment(const std::shared_ptr<erhe::scene::Node_attachment>& attachment);

    auto get_scene_root         (erhe::Hierarchy* parent) const -> Scene_root*;
    auto get_scene_root         (erhe::primitive::Material* material) const -> Scene_root*;

    // The Hierarchy a prim created under `parent` in `scene_root` is inserted
    // under: `parent` itself, or the scene root node without one.
    [[nodiscard]] auto get_insert_parent(Scene_root& scene_root, erhe::Hierarchy* parent) const -> std::shared_ptr<erhe::Hierarchy>;

    [[nodiscard]] auto get_add_cameras_command        () -> Add_cameras_command&;
    [[nodiscard]] auto get_add_room_command           () -> Add_room_command&;
    [[nodiscard]] auto get_add_lights_command         () -> Add_lights_command&;
    [[nodiscard]] auto get_add_platonic_solids_command() -> Add_platonic_solids_command&;
    [[nodiscard]] auto get_add_johnson_solids_command () -> Add_johnson_solids_command&;
    [[nodiscard]] auto get_add_curved_shapes_command  () -> Add_curved_shapes_command&;
    [[nodiscard]] auto get_add_chain_command          () -> Add_chain_command&;
    [[nodiscard]] auto get_add_toruses_command        () -> Add_toruses_command&;

private:
    // The scene a resource created under `parent` belongs to: the scene
    // hosting `parent`, else the active scene.
    [[nodiscard]] auto get_resource_scene_root(erhe::Hierarchy* parent) const -> Scene_root*;
    // Queues the undoable insert of `item` under `parent`, or into its kind
    // scope of `scene_root`'s content library without a parent.
    void queue_resource_insert(Scene_root& scene_root, erhe::Hierarchy* parent, const std::shared_ptr<erhe::Hierarchy>& item);

    App_context& m_context;

    erhe::message_bus::Subscription<Create_scene_message> m_create_scene_subscription;

    Create_new_scene_command        m_create_new_scene_command;
    Create_new_camera_command       m_create_new_camera_command;
    Create_new_xform_command   m_create_new_xform_command;
    Create_new_scope_command        m_create_new_scope_command;
    Create_new_light_command        m_create_new_light_command;
    Create_new_layout_command       m_create_new_layout_command;
    Create_new_rendertarget_command m_create_new_rendertarget_command;
    Create_new_rigid_body_command   m_create_new_rigid_body_command;
    Create_new_joint_command        m_create_new_joint_command;
    Add_cameras_command             m_add_cameras_command;
    Add_room_command                m_add_room_command;
    Add_lights_command              m_add_lights_command;
    Add_platonic_solids_command     m_add_platonic_solids_command;
    Add_johnson_solids_command      m_add_johnson_solids_command;
    Add_curved_shapes_command       m_add_curved_shapes_command;
    Add_chain_command               m_add_chain_command;
    Add_toruses_command             m_add_toruses_command;

    // TODO Figure out who should have ownership of these. The views are
    // declared before the hosts so that, at destruction, the hosts (which hold
    // a non-owning Rendertarget_view*) are destroyed before the views.
    std::vector<std::shared_ptr<Mesh_rendertarget_view>>  m_keep_alive_views;
    std::vector<std::shared_ptr<Rendertarget_imgui_host>> m_keep_alive;
};

}
