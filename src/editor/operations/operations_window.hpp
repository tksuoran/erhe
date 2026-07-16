#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include "scene/scene_builder.hpp"
#include "operations/mesh_operation.hpp"
#include "windows/property_editor.hpp"

#include <imgui/imgui.h>

namespace erhe::imgui { class Imgui_windows; }
namespace erhe::commands { class Command; }

struct Scene_config;
struct Operation_params; // codegen struct (config/generated/operation_params.hpp)

namespace editor {

struct Tool_slot
{
    // OpenXR trigger (both left and right hand):
    // - click and drag are using the same slot for now

    //                                                                     FlyCam | Select | Tr. | Ro. | Brush | Paint | Phys | HotBar  .
    static constexpr std::size_t slot_mouse_move                   =  0; //        |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_mouse_left_click             =  1; //        |   EH   |     |     |       |       |      |         .
    static constexpr std::size_t slot_mouse_left_drag              =  2; //    X   |        | EH  | EH  |       |       |      |         .
    static constexpr std::size_t slot_mouse_middle_click           =  3; //        |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_mouse_middle_drag            =  4; //        |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_mouse_right_click            =  5; //        |        |     |     |  H    |       |      |         .
    static constexpr std::size_t slot_mouse_right_drag             =  6; //        |        |     |     |       |  AH   |  AH  |         .
    static constexpr std::size_t slot_openxr_trigger_left          =  7; //    -   |    -   |  -  |     |  -    |  -    |  -   |    -    .
    static constexpr std::size_t slot_openxr_trigger_right         =  8; //    -   |   EHC  | EHD | EHD |  EC   |  ACD  |  AD  |    X    .
    static constexpr std::size_t slot_openxr_trackpad_touch        =  9; //    -   |        | ESD | ESD |       |       |      |         .
    static constexpr std::size_t slot_openxr_trackpad_click        = 10; //    -   |        |     |     |       |       |      |    X    .
    static constexpr std::size_t slot_openxr_trackpad_click_center = 11; //    -   |        |     |     |       |       |      |    X    .
    static constexpr std::size_t slot_openxr_trackpad_click_left   = 12; //    -   |        |     |     |       |       |      |    X    .
    static constexpr std::size_t slot_openxr_trackpad_click_right  = 13; //    -   |        |     |     |       |       |      |    X    .
    static constexpr std::size_t slot_openxr_trackpad_click_up     = 14; //    -   |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_openxr_trackpad_click_down   = 15; //    -   |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_openxr_menu_click            = 16; //    -   |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_space_mouse                  = 17; //    X   |        | ES  | ES  |       |       |      |         .
    static constexpr std::size_t slot_space_mouse_left             = 18; //        |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_space_mouse_right            = 19; //        |        |     |     |       |       |      |         .
    static constexpr std::size_t slot_count                        = 20; //        |        |     |     |       |       |      |         .

    static constexpr const char* c_strings[] =
    {
        "Mouse Move",
        "Mouse Left Click",
        "Mouse Left Drag",
        "Mouse Middle Click",
        "Mouse Middle Drag",
        "Mouse Right Click",
        "Mouse Right Drag",
        "OpenXR Trigger Left",
        "OpenXR Trigger Right",
        "OpenXR Trackpad Touch",
        "OpenXR Trackpad Click",
        "OpenXR Menu Click",
        "Space Mouse Mode",
        "Space Mouse Left Button",
        "Space Mouse Right Button",
    };
};

class App_context;
class App_message_bus;
class Scene_root;

// Obstacles the Add Joint initial-orientation search treats as blocking when
// looking for a non-intersecting placement.
enum class Add_joint_avoidance : unsigned int {
    joint_pair  = 0, // only the other body being joined
    whole_world = 1  // every other body in the physics world
};

class Operations : public erhe::imgui::Imgui_window
{
public:
    Operations(
        const Scene_config&          scene_config,
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        App_message_bus&             app_message_bus
    );

    // Implements Window
    void imgui() override;

    // Invoke an operation that was captured into an inventory slot: dispatches to
    // the matching parameterized operation method using the frozen params snapshot,
    // operating on the current selection. No-op when command is not a recognised
    // draggable operation. Called by Inventory_window when a command slot is clicked.
    void run_operation(erhe::commands::Command* command, const Operation_params& params);

    void merge();
    auto align_selection(bool apply_scale) -> bool;
    // Compound: align the two selected components, then create a physics joint
    // between the two nodes' rigid bodies (ball for vertex, hinge about the edge
    // for edge, hinge about the common normal for face). Aborts with a warning if
    // either node lacks a rigid body.
    auto add_joint() -> bool;
    auto add_joint(Add_joint_avoidance avoidance) -> bool;
    // Flips the selected rigid-body party of a hinge joint 180 degrees about an axis
    // perpendicular to the hinge (through the joint pivot), as if the hinge edge's
    // endpoints were swapped. Only the selected body moves; the other party is left
    // untouched. The same collision-avoidance roll search as Add Joint is applied and
    // the operation is rejected (not created) when no non-intersecting orientation is
    // found. The selected body's joint frame is re-pinned and the constraint rebuilt
    // so the hinge stays valid in the flipped pose.
    auto flip_joint() -> bool;
    auto flip_joint(Add_joint_avoidance avoidance) -> bool;
    void triangulate();
    void normalize();
    void bake_transform();
    void center_transform();
    void reverse();
    void repair();
    void weld();

    // Remesh
    void remesh();
    void remesh(unsigned int target_point_count, bool regenerate_attributes);
    void anisotropic_remesh();
    void anisotropic_remesh(unsigned int target_point_count, float anisotropy, bool regenerate_attributes);
    void decimate();
    void decimate(unsigned int nb_bins, bool regenerate_attributes);
    void smooth();
    void smooth(unsigned int iterations, float strength, bool regenerate_attributes);

    // Texturing
    void make_atlas();
    void make_atlas(std::size_t usage_index, float hard_angles_threshold, int parameterizer, int packer);

    // CSG
    void difference();
    void intersection();
    void union_();

    // Subdivision
    void catmull_clark();
    void sqrt3();

    // Conway operations
    void dual();
    void join();
    void kis();
    void kis(float height);
    void meta();
    void ortho();
    void ambo();
    void truncate();
    void truncate(float ratio);
    void gyro();
    void gyro(float ratio);
    void chamfer3();
    void chamfer3(float bevel_ratio);
    void merge_faces();

    // Blender Select More / Select Less for the active mesh-component selection.
    // Not geometry edits and not undoable - they only change the selection set
    // (see Mesh_component_selection::grow / shrink).
    void grow_component_selection();
    void shrink_component_selection();

    void generate_tangents();
    void generate_frame_field_tangents();
    void generate_frame_field_tangents(float sharp_angle_deg);
    void make_geometry();
    void make_raytrace();

    void export_gltf();
    void export_callback(const char* const* filelist, int filter);

    // File > Save glTF (erhe): a scene opened/loaded from a glTF file saves
    // back to its own source file (when that file is a loaded prefab, every
    // instance in every scene refreshes); a scene with no source file saves
    // to res/editor/scenes/<scene name>.glb, confirming overwrite.
    void save_scene();
    // Renders pending modal confirmation dialogs (the File > Save glTF (erhe)
    // overwrite prompt). Called once per frame from App_windows::viewport_menu
    // -- the imgui-host begin callback -- so the modal shows regardless of the
    // Operations window's own visibility.
    void imgui_modal_dialogs();
    void load_scene();
    void load_scene_callback(const char* const* filelist, int filter);

    void create_material();
    void create_brush();
    void create_physics_material();
    void create_collision_filter();
    void create_joint_settings();

private:
    // Saves the scene as a single erhe-authored glTF file (shared by
    // save_scene and the overwrite-confirmation modal).
    void save_scene_to_file(Scene_root& scene_root, const std::filesystem::path& path);

    void async_for_selected_nodes_with_mesh(std::function<void(Mesh_operation_parameters&&)> op, bool selection_aware = false);

    // Snapshot the live operation-parameter slider values into an Operation_params,
    // for an operation dragged out of the Operations window into an inventory slot.
    [[nodiscard]] auto current_params() const -> Operation_params;

    // Emit an ImGui drag-drop source for the operation button just drawn, carrying
    // the command plus current_params(). Call immediately after make_button() for
    // each operation backed by a registered command; only fires while that button
    // item is being dragged.
    void operation_drag_source(erhe::commands::Command* command, const char* label);

    // make_button() for an operation plus its operation_drag_source(): draws the
    // button, registers the drag source, and returns true when the button was
    // pressed this frame. Use in place of make_button() for any draggable operation.
    auto operation_button(const char* label, erhe::commands::Command* command, erhe::imgui::Item_mode mode, ImVec2 size) -> bool;

    [[nodiscard]] auto count_selected_meshes() const -> size_t;

    // True when a mesh-component selection (vertex / edge / face mode) is active and
    // non-empty. Geometry operations that are not selection-aware are unavailable in
    // this state; a selection-aware operation (Catmull-Clark) acts on the selection.
    [[nodiscard]] auto is_component_selection_active() const -> bool;

    // Decide which items a geometry operation runs on. Returns false (operation
    // skipped) when a component selection is active but the operation is not
    // selection-aware. Otherwise fills out_items with the component-selected nodes
    // (when a component selection is active and the operation is selection-aware) or
    // the object selection.
    [[nodiscard]] auto resolve_operation_items(bool selection_aware, std::vector<std::shared_ptr<erhe::Item_base>>& out_items) const -> bool;

    // True when exactly two components of the active mesh-component mode
    // (vertex / edge / face) are selected on two distinct nodes, i.e. when
    // align_selection() has a well-defined pair to act on. Used to gate the
    // toolbar button; deliberately allocation-free (runs per frame).
    [[nodiscard]] auto can_align() const -> bool;

    // Cheap per-frame gate for the Flip Joint button: a single node is selected and
    // it has a nearest rigid body. The authoritative hinge-joint lookup (and the
    // reject-on-no-orientation) happens in flip_joint() on invocation.
    [[nodiscard]] auto can_flip_joint() const -> bool;

    [[nodiscard]] auto mesh_context() -> Mesh_operation_parameters;

    // The active scene root (Selection::get_active_scene_root), which falls
    // back to the last hovered scene view's scene, then to the sole scene
    // when exactly one exists.
    [[nodiscard]] auto get_target_scene_root() -> std::shared_ptr<Scene_root>;

    template<typename T> void async_mesh_operation(bool selection_aware = false);

    // Scene close: drop a pending save-overwrite confirmation targeting the
    // closing scene - the modal's shared_ptr would pin the scene, and
    // confirming would save an already-closed scene.
    void on_close_scene(const std::shared_ptr<Scene_root>& scene_root);

    erhe::message_bus::Subscription<Load_scene_file_message>  m_load_scene_file_subscription;
    erhe::message_bus::Subscription<Close_scene_message>      m_close_scene_subscription;
    App_context& m_context;

    // Maps each draggable operation's registered command to a thunk that runs that
    // operation with an explicit Operation_params snapshot. Built in the constructor
    // alongside the Lambda_commands; its key set is exactly the set of operations
    // that expose a drag source / can be invoked from an inventory slot.
    std::unordered_map<const erhe::commands::Command*, std::function<void(const Operation_params&)>> m_param_invokers;

    erhe::commands::Lambda_command m_merge_command;
    erhe::commands::Lambda_command m_align_command;
    erhe::commands::Lambda_command m_align_with_scale_command;
    erhe::commands::Lambda_command m_add_joint_command;
    erhe::commands::Lambda_command m_flip_joint_command;
    erhe::commands::Lambda_command m_triangulate_command;
    erhe::commands::Lambda_command m_normalize_command;
    erhe::commands::Lambda_command m_bake_transform_command;
    erhe::commands::Lambda_command m_center_transform_command;
    erhe::commands::Lambda_command m_reverse_command;
    erhe::commands::Lambda_command m_repair_command;
    erhe::commands::Lambda_command m_weld_command;

    // Remesh
    erhe::commands::Lambda_command m_remesh_command;
    erhe::commands::Lambda_command m_anisotropic_remesh_command;
    erhe::commands::Lambda_command m_decimate_command;
    erhe::commands::Lambda_command m_smooth_command;

    // Texturing
    erhe::commands::Lambda_command m_make_atlas_command;

    erhe::commands::Lambda_command m_difference_command;
    erhe::commands::Lambda_command m_intersection_command;
    erhe::commands::Lambda_command m_union_command;

    // Subdivision
    erhe::commands::Lambda_command m_catmull_clark_command;
    erhe::commands::Lambda_command m_sqrt3_command;

    // Conway operations
    erhe::commands::Lambda_command m_dual_command;
    erhe::commands::Lambda_command m_join_command;
    erhe::commands::Lambda_command m_kis_command;
    erhe::commands::Lambda_command m_meta_command;
    erhe::commands::Lambda_command m_ortho_command;
    erhe::commands::Lambda_command m_ambo_command;
    erhe::commands::Lambda_command m_truncate_command;
    erhe::commands::Lambda_command m_gyro_command;
    erhe::commands::Lambda_command m_chamfer3_command;

    float m_bevel_ratio{0.5f};
    float m_truncate_ratio{1.0f / 3.0f};
    float m_gyro_ratio{1.0f / 3.0f};
    float m_kis_height{0.0f};

    // What the Add Joint initial-orientation search avoids intersecting.
    Add_joint_avoidance m_add_joint_avoidance{Add_joint_avoidance::joint_pair};

    // Remesh parameters. m_remesh_target_vertex_count is shared by isotropic
    // and anisotropic remesh (maps to Geogram's nb_points); m_decimate_bins is
    // the vertex-clustering grid resolution (higher = more detail kept).
    int   m_remesh_target_vertex_count{2000};
    int   m_decimate_bins{50};
    float m_anisotropy{0.04f};
    int   m_smooth_iterations{5};
    float m_smooth_strength{0.5f};
    // Shared by all four remesh operations: when on, process() regenerates smooth
    // vertex normals and facet texture coordinates; when off they are left as-is
    // (Smooth keeps the original UVs/attributes; Remesh/Decimate produce none).
    bool  m_remesh_regenerate_attributes{true};

    // Make Atlas (Geogram mesh_make_atlas) parameters. m_atlas_texcoord_slot is the
    // target corner texcoord channel (0 or 1) that the generated UVs overwrite;
    // m_atlas_parameterizer / m_atlas_packer index the Atlas_parameterizer /
    // Atlas_packer enums (defaults ABF + XAtlas, matching Geogram's defaults).
    int   m_atlas_texcoord_slot  {0};
    float m_atlas_hard_angles_deg{45.0f};
    int   m_atlas_parameterizer  {3};
    int   m_atlas_packer         {2};

    // Generate Frame Field Tangents: dihedral angle (degrees) above which an edge is
    // treated as a sharp feature the Geogram cross field aligns to.
    float m_frame_field_sharp_angle_deg{45.0f};

    erhe::commands::Lambda_command m_generate_tangents_command;
    erhe::commands::Lambda_command m_generate_frame_field_tangents_command;
    erhe::commands::Lambda_command m_make_geometry_command;
    erhe::commands::Lambda_command m_make_raytrace_command;

    erhe::commands::Lambda_command m_export_gltf_command;
    erhe::commands::Lambda_command m_save_scene_command;
    erhe::commands::Lambda_command m_load_scene_command;

    erhe::commands::Lambda_command m_create_material;
    erhe::commands::Lambda_command m_create_brush;
    erhe::commands::Lambda_command m_create_physics_material;
    erhe::commands::Lambda_command m_create_collision_filter;
    erhe::commands::Lambda_command m_create_joint_settings;

    // Pending File > Save glTF (erhe) overwrite confirmation (imgui_modal_dialogs):
    // set by save_scene() when the target file already exists; the scene_root
    // being non-null is what marks the confirmation as pending. The ImGui
    // context pointer pins the modal to the imgui host that opened it, since
    // viewport_menu (and thus imgui_modal_dialogs) can run for several hosts.
    std::shared_ptr<Scene_root>    m_save_confirm_scene_root{};
    std::filesystem::path          m_save_confirm_path{};
    ImGuiContext*                  m_save_confirm_imgui_context{nullptr};

    Make_mesh_config m_make_mesh_config{};
    Property_editor  m_property_editor{};

    // Operations-window search box. When non-empty, the collapsing section headers
    // are bypassed and a flat list of operations whose label passes the filter is
    // shown instead (see Operations::imgui()).
    ImGuiTextFilter  m_filter{};
};

}
