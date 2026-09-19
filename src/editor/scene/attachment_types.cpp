#include "scene/attachment_types.hpp"

#include "grid/grid.hpp"
#include "scene/draw_mode.hpp"
#include "scene/frame_controller.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <memory>

namespace editor {

namespace {

using erhe::scene::Node;

void make_camera(Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_camera(&parent); }
void make_light (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_light (&parent); }
void make_mesh  (Scene_commands& sc, erhe::Hierarchy& parent) { sc.create_new_mesh  (&parent); }

// Single-instance gates: refuse a second attachment of the same kind (issue
// #249 decision: at most one Node_physics / Layout / ... per node).
auto rigid_body_gate      (const Node& node) -> bool { return !erhe::scene::get_attachment<Node_physics           >(&node); }
auto joint_gate           (const Node&     ) -> bool { return true; } // multiple joints per node are legal
auto layout_gate          (const Node& node) -> bool { return !erhe::scene::get_attachment<erhe::scene::Layout    >(&node); }
auto grid_gate            (const Node& node) -> bool { return !erhe::scene::get_attachment<Grid                    >(&node); }
auto frame_controller_gate(const Node& node) -> bool { return !erhe::scene::get_attachment<Frame_controller        >(&node); }
auto draw_mode_gate       (const Node& node) -> bool { return !erhe::scene::get_attachment<Draw_mode               >(&node); }

void make_rigid_body      (Scene_commands& sc, Node& node) { sc.create_new_rigid_body       (&node); }
void make_joint           (Scene_commands& sc, Node& node) { sc.create_new_joint            (&node); }
void make_layout          (Scene_commands& sc, Node& node) { sc.attach_new_layout           (node); }
void make_grid            (Scene_commands& sc, Node& node) { sc.attach_new_grid             (node); }
void make_frame_controller(Scene_commands& sc, Node& node) { sc.attach_new_frame_controller (node); }
void make_draw_mode       (Scene_commands& sc, Node& node) { sc.attach_new_draw_mode        (node); }

} // anonymous namespace

auto get_child_prim_types() -> const std::vector<Child_prim_type_info>&
{
    static const std::vector<Child_prim_type_info> catalog = {
        {"mesh",   "Mesh",   make_mesh  },
        {"camera", "Camera", make_camera},
        {"light",  "Light",  make_light }
    };
    return catalog;
}

auto get_attachment_types() -> const std::vector<Attachment_type_info>&
{
    static const std::vector<Attachment_type_info> catalog = {
        {"rigid_body",       "Rigid Body",       rigid_body_gate,       make_rigid_body      },
        {"joint",            "Joint",            joint_gate,            make_joint           },
        {"layout",           "Layout",           layout_gate,           make_layout          },
        {"grid",             "Grid",             grid_gate,             make_grid            },
        {"frame_controller", "Frame Controller", frame_controller_gate, make_frame_controller},
        {"draw_mode",        "Draw Mode",        draw_mode_gate,        make_draw_mode       }
    };
    return catalog;
}

auto find_child_prim_type(std::string_view key) -> const Child_prim_type_info*
{
    for (const Child_prim_type_info& info : get_child_prim_types()) {
        if (info.key == key) {
            return &info;
        }
    }
    return nullptr;
}

auto find_attachment_type(std::string_view key) -> const Attachment_type_info*
{
    for (const Attachment_type_info& info : get_attachment_types()) {
        if (info.key == key) {
            return &info;
        }
    }
    return nullptr;
}

} // namespace editor
