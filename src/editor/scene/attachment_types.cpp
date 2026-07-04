#include "scene/attachment_types.hpp"

#include "grid/grid.hpp"
#include "scene/frame_controller.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"

#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <memory>

namespace editor {

namespace {

using erhe::scene::Node;

// Single-instance gates: refuse a second attachment of the same kind (issue
// #249 decision: at most one Camera / Light / Mesh / ... per node).
auto camera_gate          (const Node& node) -> bool { return !erhe::scene::get_attachment<erhe::scene::Camera    >(&node); }
auto light_gate           (const Node& node) -> bool { return !erhe::scene::get_attachment<erhe::scene::Light     >(&node); }
auto mesh_gate            (const Node& node) -> bool { return !erhe::scene::get_attachment<erhe::scene::Mesh      >(&node); }
auto rigid_body_gate      (const Node& node) -> bool { return !erhe::scene::get_attachment<Node_physics           >(&node); }
auto joint_gate           (const Node&     ) -> bool { return true; } // multiple joints per node are legal
auto layout_gate          (const Node& node) -> bool { return !erhe::scene::get_attachment<erhe::scene::Layout    >(&node); }
auto grid_gate            (const Node& node) -> bool { return !erhe::scene::get_attachment<Grid                    >(&node); }
auto frame_controller_gate(const Node& node) -> bool { return !erhe::scene::get_attachment<Frame_controller        >(&node); }

// A Layout_item is meaningful only on a direct child of a layout node, and a
// node holds at most one (same gate as the former one-off Properties button).
auto layout_item_gate(const Node& node) -> bool
{
    const std::shared_ptr<Node> parent = node.get_parent_node();
    return
        parent &&
        static_cast<bool>(erhe::scene::get_attachment<erhe::scene::Layout>(parent.get())) &&
        !erhe::scene::get_attachment<erhe::scene::Layout_item>(&node);
}

void make_camera          (Scene_commands& sc, Node& node) { sc.attach_new_camera          (node); }
void make_light           (Scene_commands& sc, Node& node) { sc.attach_new_light           (node); }
void make_mesh            (Scene_commands& sc, Node& node) { sc.attach_new_empty_mesh       (node); }
void make_rigid_body      (Scene_commands& sc, Node& node) { sc.create_new_rigid_body       (&node); }
void make_joint           (Scene_commands& sc, Node& node) { sc.create_new_joint            (&node); }
void make_layout          (Scene_commands& sc, Node& node) { sc.attach_new_layout           (node); }
void make_layout_item     (Scene_commands& sc, Node& node) { sc.attach_new_layout_item      (node); }
void make_grid            (Scene_commands& sc, Node& node) { sc.attach_new_grid             (node); }
void make_frame_controller(Scene_commands& sc, Node& node) { sc.attach_new_frame_controller (node); }

} // anonymous namespace

auto get_attachment_types() -> const std::vector<Attachment_type_info>&
{
    static const std::vector<Attachment_type_info> catalog = {
        {"camera",           "Camera",           camera_gate,           make_camera          },
        {"light",            "Light",            light_gate,            make_light           },
        {"mesh",             "Mesh",             mesh_gate,             make_mesh            },
        {"rigid_body",       "Rigid Body",       rigid_body_gate,       make_rigid_body      },
        {"joint",            "Joint",            joint_gate,            make_joint           },
        {"layout",           "Layout",           layout_gate,           make_layout          },
        {"layout_item",      "Layout Item",      layout_item_gate,      make_layout_item     },
        {"grid",             "Grid",             grid_gate,             make_grid            },
        {"frame_controller", "Frame Controller", frame_controller_gate, make_frame_controller}
    };
    return catalog;
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
