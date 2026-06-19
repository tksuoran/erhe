#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <memory>
#include <vector>
#include "scene/scene_builder.hpp"
#include "operations/mesh_operation.hpp"
#include "windows/property_editor.hpp"

namespace erhe::imgui { class Imgui_windows; }

struct Scene_config;

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
class Content_library_window;
class Scene_root;
class Scene_view;

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
    void meta();
    void ortho();
    void ambo();
    void truncate();
    void gyro();
    void chamfer3();

    void generate_tangents();
    void generate_frame_field_tangents();
    void make_geometry();
    void make_raytrace();

    void export_gltf();
    void export_callback(const char* const* filelist, int filter);

    void save_scene();
    void load_scene();

    void create_material();
    void create_brush();
    void create_physics_material();
    void create_collision_filter();
    void create_joint_settings();

private:
    void async_for_selected_nodes_with_mesh(std::function<void(Mesh_operation_parameters&&)> op);

    [[nodiscard]] auto count_selected_meshes() const -> size_t;

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

    // Scene root of the last hovered scene view; before any viewport has been
    // hovered, falls back to the sole scene when exactly one exists.
    [[nodiscard]] auto get_target_scene_root() -> std::shared_ptr<Scene_root>;

    template<typename T> void async_mesh_operation();

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Load_scene_file_message>  m_load_scene_file_subscription;
    App_context& m_context;

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

    Scene_view*                    m_hover_scene_view     {nullptr};
    Scene_view*                    m_last_hover_scene_view{nullptr};

    // Each entry pairs the Content_library_window with a weak_ptr to
    // the Scene_root that loaded it. prune_loaded_scene_windows()
    // drops entries whose Scene_root has expired so the vector does
    // not grow monotonically across scene loads / unloads.
    struct Loaded_scene_window
    {
        std::weak_ptr<Scene_root>               scene_root;
        std::shared_ptr<Content_library_window> content_library_window;
    };
    std::vector<Loaded_scene_window> m_loaded_content_library_windows;

    void prune_loaded_scene_windows();

    Make_mesh_config m_make_mesh_config{};
    Property_editor  m_property_editor{};
};

}
