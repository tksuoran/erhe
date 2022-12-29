#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <memory>
#include <mutex>

namespace editor
{

class Mesh_memory;
class Operation_stack;
class Selection_tool;
class Tool;

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

class Operations
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Operations"};
    static constexpr std::string_view c_title{"Operations"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Operations ();
    ~Operations() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Window
    void imgui() override;

private:
    [[nodiscard]]auto count_selected_meshes() const -> size_t;

    // Component dependencies
    std::shared_ptr<Mesh_memory>     m_mesh_memory;
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Selection_tool>  m_selection_tool;
};

} // namespace editor
