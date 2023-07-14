#pragma once

#include "erhe/imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

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

class Operations
    : public erhe::imgui::Imgui_window
{
public:
    Operations(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Window
    void imgui() override;

private:
    [[nodiscard]]auto count_selected_meshes() const -> size_t;

    Editor_context& m_context;
};

} // namespace editor
