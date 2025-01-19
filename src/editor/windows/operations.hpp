#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_commands/command.hpp"
#include "scene/scene_builder.hpp"
#include "operations/mesh_operation.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

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

class Editor_context;

class Operations : public erhe::imgui::Imgui_window
{
public:
    Operations(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Window
    void imgui() override;

    void merge();
    void triangulate();
    void normalize();
    void bake_transform();
    void reverse();
    void repair();

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

private:
    [[nodiscard]] auto count_selected_meshes() const -> size_t;
    [[nodiscard]] auto mesh_context() -> Mesh_operation_parameters;

    Editor_context& m_context;

    erhe::commands::Lambda_command m_merge_command;
    erhe::commands::Lambda_command m_triangulate_command;
    erhe::commands::Lambda_command m_normalize_command;
    erhe::commands::Lambda_command m_bake_transform_command;
    erhe::commands::Lambda_command m_reverse_command;
    erhe::commands::Lambda_command m_repair_command;

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

    Make_mesh_config m_make_mesh_config{};
};

} // namespace editor
