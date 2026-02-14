#include "developer/icon_browser.hpp"

#include "app_context.hpp"

#include "windows/IconsMaterialDesignIcons.h"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

namespace editor {

Icon_browser::Icon_browser(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Icon Browser", "icon_browser", true}
    , m_context                {app_context}
{
}

// ICON_MDI_ARROW_SPLIT_HORIZONTAL
// ICON_MDI_ARROW_UP_DOWN_BOLD
// ICON_MDI_ATOM_VARIANT
// ICON_MDI_AXIS
// ICON_MDI_AXIS_ARROW

// https://fontdrop.info/?darkmode=true

const char* icon_erhe_names[] = {
    "ICON_ANIM",
    "ICON_ARMATURE_DATA",
    "ICON_BONE_DATA",
    "ICON_BRUSH_BIG",
    "ICON_BRUSH_SMALL",
    "ICON_BRUSH_TOOL",
    "ICON_CAMERA",
    "ICON_CURVE_PATH",
    "ICON_DIRECTIONAL_LIGHT",
    "ICON_DRAG",
    "ICON_FILE",
    "ICON_FILEBROWSER",
    "ICON_GRID",
    "ICON_HEADSET",
    "ICON_HUD",
    "ICON_MATERIAL",
    "ICON_MESH",
    "ICON_MESH_CONE",
    "ICON_MESH_CUBE",
    "ICON_MESH_CYLINDER",
    "ICON_MESH_ICOSPHERE",
    "ICON_MESH_TORUS",
    "ICON_MESH_UVSPHERE",
    "ICON_MOUSE_LMB",
    "ICON_MOUSE_LMB_DRAG",
    "ICON_MOUSE_MMB",
    "ICON_MOUSE_MMB_DRAG",
    "ICON_MOUSE_MOVE",
    "ICON_MOUSE_RMB",
    "ICON_MOUSE_RMB_DRAG",
    "ICON_MOVE",
    "ICON_NODE",
    "ICON_PHYSICS",
    "ICON_POINT_LIGHT",
    "ICON_PULL",
    "ICON_PUSH",
    "ICON_ROTATE",
    "ICON_SCALE",
    "ICON_SCENE",
    "ICON_SELECT",
    "ICON_SPACE_MOUSE",
    "ICON_SPACE_MOUSE_LMB",
    "ICON_SPACE_MOUSE_RMB",
    "ICON_SPOT_LIGHT",
    "ICON_TEXTURE",
    "ICON_THREE_DOTS",
    "ICON_VIVE",
    "ICON_VIVE_MENU",
    "ICON_VIVE_TRACKPAD",
    "ICON_VIVE_TRIGGER"
};

const char* icon_erhe_codes[] = {
    "\xee\xa8\x81",  // U+0EA01 anim
    "\xee\xa8\x82",  // U+0EA02 armature_data
    "\xee\xa8\x83",  // U+0EA03 bone_data
    "\xee\xa8\x84",  // U+0EA04 brush_big
    "\xee\xa8\x85",  // U+0EA05 brush_small
    "\xee\xa8\x86",  // U+0EA06 brush_tool
    "\xee\xa8\x87",  // U+0EA07 camera
    "\xee\xa8\x88",  // U+0EA08 curve_path
    "\xee\xa8\x89",  // U+0EA09 directional_light
    "\xee\xa8\x8a",  // U+0EA0A drag
    "\xee\xa8\x8b",  // U+0EA0B file
    "\xee\xa8\x8c",  // U+0EA0C filebrowser
    "\xee\xa8\x8d",  // U+0EA0D grid
    "\xee\xa8\x8e",  // U+0EA0E headset
    "\xee\xa8\x8f",  // U+0EA0F hud
    "\xee\xa8\x90",  // U+0EA10 material
    "\xee\xa8\x91",  // U+0EA11 mesh
    "\xee\xa8\x92",  // U+0EA12 mesh_cone
    "\xee\xa8\x93",  // U+0EA13 mesh_cube
    "\xee\xa8\x94",  // U+0EA14 mesh_cylinder
    "\xee\xa8\x95",  // U+0EA15 mesh_icosphere
    "\xee\xa8\x96",  // U+0EA16 mesh_torus
    "\xee\xa8\x97",  // U+0EA17 mesh_uvsphere
    "\xee\xa8\x98",  // U+0EA18 mouse_lmb
    "\xee\xa8\x99",  // U+0EA19 mouse_lmb_drag
    "\xee\xa8\x9a",  // U+0EA1A mouse_mmb
    "\xee\xa8\x9b",  // U+0EA1B mouse_mmb_drag
    "\xee\xa8\x9c",  // U+0EA1C mouse_move
    "\xee\xa8\x9d",  // U+0EA1D mouse_rmb
    "\xee\xa8\x9e",  // U+0EA1E mouse_rmb_drag
    "\xee\xa8\x9f",  // U+0EA1F move
    "\xee\xa8\xa0",  // U+0EA20 node
    "\xee\xa8\xa1",  // U+0EA21 physics
    "\xee\xa8\xa2",  // U+0EA22 point_light
    "\xee\xa8\xa3",  // U+0EA23 pull
    "\xee\xa8\xa4",  // U+0EA24 push
    "\xee\xa8\xa5",  // U+0EA25 rotate
    "\xee\xa8\xa6",  // U+0EA26 scale
    "\xee\xa8\xa7",  // U+0EA27 scene
    "\xee\xa8\xa8",  // U+0EA28 select
    "\xee\xa8\xa9",  // U+0EA29 space_mouse
    "\xee\xa8\xaa",  // U+0EA2A space_mouse_lmb
    "\xee\xa8\xab",  // U+0EA2B space_mouse_rmb
    "\xee\xa8\xac",  // U+0EA2C spot_light
    "\xee\xa8\xad",  // U+0EA2D texture
    "\xee\xa8\xae",  // U+0EA2E three_dots
    "\xee\xa8\xaf",  // U+0EA2F vive
    "\xee\xa8\xb0",  // U+0EA30 vive_menu
    "\xee\xa8\xb1",  // U+0EA31 vive_trackpad
    "\xee\xa8\xb2"   // U+0EA32 vive_trigger
};


const unsigned int icon_erhe_unicodes[] = {
    0xea02,
    0xea03,
    0xea04,
    0xea05,
    0xea06,
    0xea07,
    0xea08,
    0xea09,
    0xea0a,
    0xea0b,
    0xea0c,
    0xea0d,
    0xea0e,
    0xea0f,
    0xea10,
    0xea11,
    0xea12,
    0xea13,
    0xea14,
    0xea15,
    0xea16,
    0xea17,
    0xea18,
    0xea19,
    0xea1a,
    0xea1b,
    0xea1c,
    0xea1d,
    0xea1e,
    0xea1f,
    0xea20,
    0xea21,
    0xea22,
    0xea23,
    0xea24,
    0xea25,
    0xea26,
    0xea27,
    0xea28,
    0xea29,
    0xea2a,
    0xea2b,
    0xea2c,
    0xea2d,
    0xea2e,
    0xea2f,
    0xea30,
    0xea31,
    0xea32
};

#define ICON_MIN_ERHE 0xea01
#define ICON_MAX_ERHE 0xea32

void Icon_browser::imgui()
{
    ImGui::Combo("Font", &m_selected_font, "Material Design\0Icons\0\0");

    ImFont* icon_font = (m_selected_font == 0)
        ? m_context.imgui_renderer->material_design_font()
        : m_context.imgui_renderer->icon_font();

    const float font_size =
        m_imgui_renderer.get_imgui_settings().scale_factor *
        (
            (m_selected_font == 0)
            ? m_imgui_renderer.get_imgui_settings().material_design_font_size
            : m_imgui_renderer.get_imgui_settings().icon_font_size
        );
    ImGui::PushFont(m_context.imgui_renderer->material_design_font(), m_imgui_renderer.get_imgui_settings().font_size);
    ImGui::TextUnformatted(ICON_MDI_FILTER);
    ImGui::PopFont();
    ImGui::SameLine();
    m_name_filter.Draw("##icon_filter", -FLT_MIN);

    ImGui::PushFont(icon_font, font_size);
    float line_height = ImGui::GetFrameHeight();
    ImGui::PopFont();

    ImVec2 button_size{1.1f * line_height, 1.1f * line_height};
    int x = 0;

    const char**        icon_names    = (m_selected_font == 0) ? icon_MDI_names    : icon_erhe_names;
    const char**        icon_codes    = (m_selected_font == 0) ? icon_MDI_codes    : icon_erhe_codes;
    const unsigned int* icon_unicodes = (m_selected_font == 0) ? icon_MDI_unicodes : icon_erhe_unicodes;
    const unsigned int  icon_min      = (m_selected_font == 0) ? ICON_MIN_MDI      : ICON_MIN_ERHE;
    const unsigned int  icon_max      = (m_selected_font == 0) ? ICON_MAX_MDI      : ICON_MAX_ERHE;

    for (int i = 0; (icon_codes[i] != 0) && (icon_names[i] != nullptr); ++i) {
        ImGui::PushID(i);
        ERHE_DEFER( ImGui::PopID(); );
        const bool show_by_name = m_name_filter.PassFilter(icon_names[i]);
        if (!show_by_name) {
            continue;
        }

        if ((icon_unicodes[i] < icon_min) || (icon_unicodes[i] > icon_max)) {
            continue;
        }
        ImGui::PushFont(icon_font, font_size);
        ImGui::Button(icon_codes[i], button_size);
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %x", icon_names[i], icon_unicodes[i]);
            {
                ImGui::PushFont(icon_font, font_size);
                ImGui::TextUnformatted(icon_codes[i]);
                ImGui::PopFont();
            }
            ImGui::EndTooltip();
        }
        ++x;
        if (x < 20) {
            ImGui::SameLine();
        } else {
            x = 0;
        }
    }
    ImGui::NewLine();
}

}
