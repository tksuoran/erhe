#include "windows/properties.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "rendertarget_mesh.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"
#include "scene/frame_controller.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "windows/content_library_window.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/windows/log_window.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui_internal.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

const float indent = 15.0f;

void Properties::Value_edit_state::combine(
    const Value_edit_state& other
)
{
    value_changed = value_changed || other.value_changed;
    edit_ended    = edit_ended    || other.edit_ended;
}

Properties::Properties()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Properties::~Properties() noexcept
{
}

void Properties::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Properties::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Properties::post_initialize()
{
    m_content_library_window = get<Content_library_window>();
    m_editor_scenes          = get<Editor_scenes         >();
    m_operation_stack        = get<Operation_stack       >();
    m_selection_tool         = get<Selection_tool        >();
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Properties::camera_properties(erhe::scene::Camera& camera) const
{
    ERHE_PROFILE_FUNCTION

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    auto* const projection = camera.projection();
    if (
        (projection != nullptr) &&
        ImGui::TreeNodeEx(
            "Projection",
            ImGuiTreeNodeFlags_DefaultOpen
        )
    )
    {
        ImGui::Indent(indent);
        ImGui::SetNextItemWidth(200);
        erhe::application::make_combo(
            "Type",
            projection->projection_type,
            erhe::scene::Projection::c_type_strings,
            IM_ARRAYSIZE(erhe::scene::Projection::c_type_strings)
        );
        switch (projection->projection_type)
        {
            //using enum erhe::scene::Projection::Type;
            case erhe::scene::Projection::Type::perspective:
            {
                ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::perspective_xr:
            {
                ImGui::SliderFloat("Fov Left",  &projection->fov_left,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Right", &projection->fov_right, -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Up",    &projection->fov_up,    -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Down",  &projection->fov_down,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Z Near",    &projection->z_near,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",     &projection->z_far,     0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::perspective_horizontal:
            {
                ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::perspective_vertical:
            {
                ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_horizontal:
            {
                ImGui::SliderFloat("Width",  &projection->ortho_width, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,      0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,       0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_vertical:
            {
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal:
            {
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_rectangle:
            {
                ImGui::SliderFloat("Left",   &projection->ortho_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::generic_frustum:
            {
                ImGui::SliderFloat("Left",   &projection->frustum_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Right",  &projection->frustum_right,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Top",    &projection->frustum_top,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,         0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,          0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::other:
            {
                // TODO(tksuoran@gmail.com): Implement
                break;
            }
        }
        ImGui::Unindent(indent);
        ImGui::TreePop();
    }

    float exposure = camera.get_exposure();
    const auto avail = ImGui::GetContentRegionAvail().x;
    const auto label_size = std::max(
        ImGui::CalcTextSize("Exposure").x,
        ImGui::CalcTextSize("Shadow Range").x
    );
    const auto width = avail - label_size;
    {
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 2000.0f, "%.3f", logarithmic))
        {
            camera.set_exposure(exposure);
        }
    }
    float shadow_range = camera.get_shadow_range();
    {
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("Shadow Range", &shadow_range, 1.00f, 1000.0f, "%.3f", logarithmic))
        {
            camera.set_shadow_range(shadow_range);
        }
    }
}

void Properties::light_properties(erhe::scene::Light& light) const
{
    ERHE_PROFILE_FUNCTION

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    erhe::application::make_combo(
        "Type",
        light.type,
        erhe::scene::Light::c_type_strings,
        IM_ARRAYSIZE(erhe::scene::Light::c_type_strings)
    );
    if (light.type == erhe::scene::Light::Type::spot)
    {
        ImGui::SliderFloat("Inner Spot", &light.inner_spot_angle, 0.0f, glm::pi<float>());
        ImGui::SliderFloat("Outer Spot", &light.outer_spot_angle, 0.0f, glm::pi<float>());
    }
    ImGui::SliderFloat("Range",     &light.range,     1.00f, 20000.0f, "%.3f", logarithmic);
    ImGui::SliderFloat("Intensity", &light.intensity, 0.01f, 20000.0f, "%.3f", logarithmic);
    ImGui::ColorEdit3 ("Color",     &light.color.x,   ImGuiColorEditFlags_Float);

    const auto* node = light.get_node();
    if (node != nullptr)
    {
        auto* scene_root = reinterpret_cast<Scene_root*>(node->get_scene_host());
        if (scene_root != nullptr)
        {
            const auto& layers = scene_root->layers();
            ImGui::ColorEdit3(
                "Ambient",
                &layers.light()->ambient_light.x,
                ImGuiColorEditFlags_Float
            );
        }
    }
}

void Properties::mesh_properties(erhe::scene::Mesh& mesh) const
{
    ERHE_PROFILE_FUNCTION

    //ERHE_VERIFY(mesh.node_data.host != nullptr);

    auto& mesh_data  = mesh.mesh_data;
    const auto* node = mesh.get_node();
    auto* scene_root = reinterpret_cast<Scene_root*>(node->get_scene_host());
    if (scene_root == nullptr)
    {
        ImGui::Text("Mesh host not set");
        return;
    }
    auto& material_library = scene_root->content_library()->materials;

    for (auto& primitive : mesh_data.primitives)
    {
        const auto& geometry = primitive.source_geometry;
        if (geometry)
        {
            const std::string label = fmt::format("Primitive: {}", geometry->name);
            if (
                ImGui::TreeNodeEx(label.c_str())
            )
            {
                ImGui::Indent(indent);
                material_library.combo("Material", primitive.material, false);
                if (primitive.material)
                {
                    ImGui::Text("Material Buffer Index: %u", primitive.material->material_buffer_index);
                }
                else
                {
                    ImGui::Text("Null material");
                }
                if (
                    ImGui::TreeNodeEx("Statistics")
                )
                {
                    ImGui::Indent(indent);
                    int point_count   = geometry->get_point_count();
                    int polygon_count = geometry->get_polygon_count();
                    int edge_count    = geometry->get_edge_count();
                    int corner_count  = geometry->get_corner_count();
                    ImGui::InputInt("Points",      &point_count,   0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputInt("Polygons",    &polygon_count, 0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputInt("Edges",       &edge_count,    0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputInt("Corners",     &corner_count,  0, 0, ImGuiInputTextFlags_ReadOnly);
                    float bbox_volume    = primitive.gl_primitive_geometry.bounding_box.volume();
                    float bsphere_volume = primitive.gl_primitive_geometry.bounding_sphere.volume();
                    ImGui::InputFloat("BBox Volume",    &bbox_volume,    0, 0, "%.4f", ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputFloat("BSphere Volume", &bsphere_volume, 0, 0, "%.4f", ImGuiInputTextFlags_ReadOnly);
                    ImGui::Unindent(indent);
                    ImGui::TreePop();
                }
                ImGui::Unindent(indent);
                ImGui::TreePop();
            }
        }
    }
}

void Properties::rendertarget_properties(Rendertarget_mesh& rendertarget) const
{
    ImGui::Text("Width: %f", rendertarget.width());
    ImGui::Text("Height: %f", rendertarget.height());
    ImGui::Text("Pixels per Meter: %f", static_cast<float>(rendertarget.pixels_per_meter()));
}

namespace {

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2{lhs.x + rhs.x, lhs.y + rhs.y};
}

//static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
//{
//    return ImVec2{lhs.x - rhs.x, lhs.y - rhs.y};
//}

static const float DRAG_MOUSE_THRESHOLD_FACTOR = 0.50f;    // Multiplier for the default value of io.MouseDragThreshold to make DragFloat/DragInt react faster to mouse drags.

class Custom_drag_result
{
public:
    bool text_edited{false};
    bool drag_edited{false};
};

// Fork of DragScalar() so we can differentiate between drag and text edit
auto custom_drag_scalar(
    const char* const label,
    void* const       p_data,
    const float       v_speed = 1.0f,
    const float       min     = 0.0f,
    const float       max     = 0.0f,
    const char*       format  = "%.3f"
) -> Custom_drag_result
{
    ERHE_PROFILE_FUNCTION

    ImGuiDataType      data_type = ImGuiDataType_Float;
    ImGuiSliderFlags   flags = 0;
    const float* const p_min = &min;
    const float* const p_max = &max;

    Custom_drag_result result;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
    {
        return result;
    }

    ImGuiContext&     g     = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID     id    = window->GetID(label);
    const float       w     = ImGui::CalcItemWidth();

    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
    {
        return result;
    }

    // Default format string when passing NULL
    if (format == NULL)
    {
        format = ImGui::DataTypeGetInfo(data_type)->PrintFmt;
    }
    //else if (data_type == ImGuiDataType_S32 && strcmp(format, "%d") != 0) // (FIXME-LEGACY: Patch old "%.0f" format string to use "%d", read function more details.)
    //    format = ImGui::PatchFormatStringFloatToInt(format);

    // Tabbing or CTRL-clicking on Drag turns it into an InputText
    const bool hovered = ImGui::ItemHoverable(frame_bb, id);
    bool temp_input_is_active = temp_input_allowed && ImGui::TempInputIsActive(id);
    if (!temp_input_is_active)
    {
        const bool input_requested_by_tabbing = temp_input_allowed && (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_FocusedByTabbing) != 0;
        const bool clicked = (hovered && g.IO.MouseClicked[0]);
        const bool double_clicked = (hovered && g.IO.MouseClickedCount[0] == 2);
        if (input_requested_by_tabbing || clicked || double_clicked || g.NavActivateId == id || g.NavActivateInputId == id)
        {
            ImGui::SetActiveID(id, window);
            ImGui::SetFocusID(id, window);
            ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
            if (temp_input_allowed)
            {
                if (input_requested_by_tabbing || (clicked && g.IO.KeyCtrl) || double_clicked || g.NavActivateInputId == id)
                {
                    temp_input_is_active = true;
                }
            }
        }

        // Experimental: simple click (without moving) turns Drag into an InputText
        if (g.IO.ConfigDragClickToInputText && temp_input_allowed && !temp_input_is_active)
        {
            if (g.ActiveId == id && hovered && g.IO.MouseReleased[0] && !ImGui::IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * DRAG_MOUSE_THRESHOLD_FACTOR))
            {
                g.NavActivateId = g.NavActivateInputId = id;
                g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                temp_input_is_active = true;
            }
        }
    }

    if (temp_input_is_active)
    {
        // Only clamp CTRL+Click input when ImGuiSliderFlags_AlwaysClamp is set
        const bool is_clamp_input = (flags & ImGuiSliderFlags_AlwaysClamp) != 0 && (p_min == NULL || p_max == NULL || ImGui::DataTypeCompare(data_type, p_min, p_max) < 0);
        result.text_edited = ImGui::TempInputScalar(frame_bb, id, label, data_type, p_data, format, is_clamp_input ? p_min : NULL, is_clamp_input ? p_max : NULL);
        return result;
    }

    // Draw frame
    const ImU32 frame_col = ImGui::GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    ImGui::RenderNavHighlight(frame_bb, id);
    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, style.FrameRounding);

    // Drag behavior
    result.drag_edited = ImGui::DragBehavior(id, data_type, p_data, v_speed, p_min, p_max, format, flags);
    if (result.drag_edited)
    {
        ImGui::MarkItemEdited(id);
    }

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    char value_buf[64];
    const char* value_buf_end = value_buf + ImGui::DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    if (g.LogEnabled)
    {
        ImGui::LogSetNextTextDecoration("{", "}");
    }
    ImGui::RenderTextClipped(frame_bb.Min, frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.5f));

    if (label_size.x > 0.0f)
    {
        ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);
    }

    return result;
}

}

auto Properties::make_scalar_button(
    float* const      value,
    const uint32_t    text_color,
    const uint32_t    background_color,
    const char* const label,
    const char* const imgui_label
) const -> Value_edit_state
{
    ERHE_PROFILE_FUNCTION

    ImGui::PushStyleColor  (ImGuiCol_Text,          text_color);
    ImGui::PushStyleColor  (ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonActive,  background_color);
    ImGui::SetNextItemWidth(12.0f);
    ImGui::Button          (label);
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    constexpr float value_speed = 0.02f;
    const auto value_changed = custom_drag_scalar(imgui_label, value, value_speed);
    const bool edit_ended    = ImGui::IsItemDeactivatedAfterEdit();
    //const bool item_edited   = ImGui::IsItemEdited();
    //const bool item_active   = ImGui::IsItemActive();
    //if (value_changed.drag_edited)
    //{
    //    log_node_properties->trace("drag edited for {}", label);
    //}
    //if (value_changed.text_edited)
    //{
    //    log_node_properties->trace("text edited for {}", label);
    //}
    //if (item_edited)
    //{
    //    log_node_properties->trace("item_edited for {}", label);
    //}
    //if (edit_ended)
    //{
    //    log_node_properties->trace("edit_ended for {}", label);
    //}
    return Value_edit_state{
        .value_changed = value_changed.drag_edited || (value_changed.text_edited && edit_ended),
        .edit_ended    = edit_ended
    };
};

auto Properties::make_angle_button(
    float&            radians_value,
    const uint32_t    text_color,
    const uint32_t    background_color,
    const char* const label,
    const char* const imgui_label
) const -> Value_edit_state
{
    ERHE_PROFILE_FUNCTION

    ImGui::PushStyleColor  (ImGuiCol_Text,          text_color);
    ImGui::PushStyleColor  (ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonActive,  background_color);
    ImGui::SetNextItemWidth(12.0f);
    ImGui::Button          (label);
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    float degrees_value = glm::degrees<float>(radians_value);
    constexpr float value_speed = 1.0f;
    constexpr float value_min   = -360.0f;
    constexpr float value_max   =  360.0f;

    const auto drag_result = custom_drag_scalar(
        imgui_label,
        &degrees_value,
        value_speed,
        value_min,
        value_max,
        "%.f\xc2\xb0" // \xc2\xb0 is degree symbol UTF-8 encoded
    );
    const bool edit_ended    = ImGui::IsItemDeactivatedAfterEdit();
    const bool value_changed = drag_result.drag_edited || (drag_result.text_edited && edit_ended);
    if (value_changed)
    {
        radians_value = glm::radians<float>(degrees_value);
    }
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit()
    };
};
#endif

Properties::Node_state::Node_state(erhe::scene::Node& node)
    : node                              {&node}
    , touched                           {false}
    , initial_parent_from_node_transform{node.parent_from_node_transform()}
    , initial_world_from_node_transform {node.world_from_node_transform()}
{
}

auto Properties::get_node_state(erhe::scene::Node& node) -> Node_state&
{
    for (auto& entry : m_node_states)
    {
        if (entry.node == &node)
        {
            return entry;
        }
    }
    return m_node_states.emplace_back(node);
}

auto Properties::drop_node_state(erhe::scene::Node& node)
{
    m_node_states.erase(
        std::remove_if(
            m_node_states.begin(),
            m_node_states.end(),
            [&node](auto& entry) -> bool
            {
                return entry.node == &node;
            }
        ),
        m_node_states.end()
    );
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Properties::transform_properties(erhe::scene::Node& node)
{
    ERHE_PROFILE_FUNCTION

    if (
        !ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen)
    )
    {
        return;
    }

    ImGui::Indent(indent);

    auto& node_state = get_node_state(node);

    const glm::mat4 transform = node.world_from_node();
    glm::vec3 scale;
    glm::quat orientation;
    glm::vec3 translation;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(transform, scale, orientation, translation, skew, perspective);

    const glm::mat4 orientation_matrix{orientation};
    glm::vec3 euler_angles;
    glm::extractEulerAngleZYX(orientation_matrix, euler_angles.x, euler_angles.y, euler_angles.z);

    Value_edit_state edit_state;
    if (ImGui::TreeNodeEx("Translation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        edit_state.combine(make_scalar_button(&translation.x, 0xff8888ffu, 0xff222266u, "X", "##T.X"));
        edit_state.combine(make_scalar_button(&translation.y, 0xff88ff88u, 0xff226622u, "Y", "##T.Y"));
        edit_state.combine(make_scalar_button(&translation.z, 0xffff8888u, 0xff662222u, "Z", "##T.Z"));
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Rotation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        edit_state.combine(make_angle_button(euler_angles.x, 0xff8888ffu, 0xff222266u, "X", "##R.X"));
        edit_state.combine(make_angle_button(euler_angles.y, 0xff88ff88u, 0xff226622u, "Y", "##R.Y"));
        edit_state.combine(make_angle_button(euler_angles.z, 0xffff8888u, 0xff662222u, "Z", "##R.Z"));
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Scale"))
    {
        edit_state.combine(make_scalar_button(&scale.x, 0xff8888ffu, 0xff222266u, "X", "##S.X"));
        edit_state.combine(make_scalar_button(&scale.y, 0xff88ff88u, 0xff226622u, "Y", "##S.Y"));
        edit_state.combine(make_scalar_button(&scale.z, 0xffff8888u, 0xff662222u, "Z", "##S.Z"));
        ImGui::TreePop();
    }

    erhe::scene::Transform new_parent_from_node;
    if (
        edit_state.value_changed ||
        edit_state.edit_ended
    )
    {
        node_state.touched = true;
        const glm::mat4 new_translation     = erhe::toolkit::create_translation<float>(translation);
        const glm::mat4 new_rotation        = glm::eulerAngleZYX(euler_angles.x, euler_angles.y, euler_angles.z);
        const glm::mat4 new_scale           = erhe::toolkit::create_scale<float>(scale);
        const glm::mat4 new_world_from_node = new_translation * new_rotation * new_scale;
        const auto current_parent = node.parent().lock();
        if (!current_parent)
        {
            new_parent_from_node = erhe::scene::Transform{new_world_from_node};
        }
        else
        {
            new_parent_from_node = current_parent->node_from_world_transform() * erhe::scene::Transform{new_world_from_node};
        }
        if (!edit_state.edit_ended)
        {
            node.set_parent_from_node(new_parent_from_node);
        }
    }

    if (edit_state.edit_ended && node_state.touched)
    {
        m_operation_stack->push(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = std::static_pointer_cast<erhe::scene::Node>(node.shared_from_this()),
                    .parent_from_node_before = node_state.initial_parent_from_node_transform,
                    .parent_from_node_after  = new_parent_from_node
                }
            )
        );
        node_state.touched = false;
    }

    if (!node_state.touched)
    {
        drop_node_state(node);
    }

    ImGui::Unindent(indent);

    ImGui::TreePop();
}
#endif

void Properties::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
#endif
}

void Properties::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar();
#endif
}

namespace {

[[nodiscard]] auto test_bits(uint64_t mask, uint64_t test_bits) -> bool
{
    return (mask & test_bits) == test_bits;
}

}

void Properties::node_physics_properties(Node_physics& node_physics) const
{
    erhe::physics::IRigid_body* rigid_body = node_physics.rigid_body();
    if (rigid_body == nullptr)
    {
        return;
    }

    ImGui::Text("Rigid Body: %s", rigid_body->get_debug_label());
    float           mass    = rigid_body->get_mass();
    const glm::mat4 inertia = rigid_body->get_local_inertia();
    if (ImGui::SliderFloat("Mass", &mass, 0.0f, 1000.0f)) {
        rigid_body->set_mass_properties(mass, inertia);
    }
    float friction = rigid_body->get_friction();
    if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f)) {
        rigid_body->set_friction(friction);
    }
    float restitution = rigid_body->get_restitution();
    if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f)) {
        rigid_body->set_restitution(restitution);
    }

    // Static bodies don't have access to these properties, at least in Jolt
    if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_static)
    {
        float angular_damping = rigid_body->get_angular_damping();
        float linear_damping = rigid_body->get_linear_damping();
        if (
            ImGui::SliderFloat("Angular Dampening", &angular_damping, 0.0f, 1.0f) ||
            ImGui::SliderFloat("Linear Dampening", &linear_damping, 0.0f, 1.0f)
        )
        {
            rigid_body->set_damping(linear_damping, angular_damping);
        }

        {
            const glm::mat4 local_inertia = rigid_body->get_local_inertia();
            float floats[4] = { local_inertia[0][0], local_inertia[1][1], local_inertia[2][2] };
            ImGui::InputFloat3("Local Inertia", floats);
            // TODO floats back to rigid body?
        }
    }

    int motion_mode = static_cast<int>(rigid_body->get_motion_mode());
    if (
        ImGui::Combo(
            "Motion Mode",
            &motion_mode,
            erhe::physics::c_motion_mode_strings,
            IM_ARRAYSIZE(erhe::physics::c_motion_mode_strings)
        )
    )
    {
        rigid_body->set_motion_mode(static_cast<erhe::physics::Motion_mode>(motion_mode));
    }
}

void Properties::item_flags(const std::shared_ptr<erhe::scene::Scene_item>& item)
{
    if (!ImGui::TreeNodeEx("Flags"))
    {
        return;
    }

    ImGui::Indent(indent);

    using namespace erhe::toolkit;
    using Scene_item_flags = erhe::scene::Scene_item_flags;

    const uint64_t flags = item->get_flag_bits();
    bool visible     = test_all_rhs_bits_set(flags, Scene_item_flags::visible                  );
    bool content     = test_all_rhs_bits_set(flags, Scene_item_flags::content                  );
    bool shadow_cast = test_all_rhs_bits_set(flags, Scene_item_flags::shadow_cast              );
    bool id          = test_all_rhs_bits_set(flags, Scene_item_flags::id                       );
    bool tool        = test_all_rhs_bits_set(flags, Scene_item_flags::tool                     );
    bool selected    = test_all_rhs_bits_set(flags, Scene_item_flags::selected                 );
    bool show_debug  = test_all_rhs_bits_set(flags, Scene_item_flags::show_debug_visualizations);
    if (ImGui::Checkbox("Show Debug", &show_debug))
    {
        item->set_flag_bits(Scene_item_flags::show_debug_visualizations, show_debug);
    }
    if (ImGui::Checkbox("Visible", &visible))
    {
        item->set_visible(visible);
    }
    if (ImGui::Checkbox("Content", &content))
    {
        item->set_flag_bits(Scene_item_flags::content, content);
    }
    if (ImGui::Checkbox("ID Render", &id))
    {
        item->set_flag_bits(Scene_item_flags::id, id);
    }
    if (ImGui::Checkbox("Shadow Cast", &shadow_cast))
    {
        item->set_flag_bits(Scene_item_flags::shadow_cast, shadow_cast);
    }
    if (ImGui::Checkbox("Selected", &selected))
    {
        if (selected)
        {
            m_selection_tool->add_to_selection(item);
        }
        else
        {
            m_selection_tool->remove_from_selection(item);
        }
    }
    if (ImGui::Checkbox("Tool", &tool))
    {
        item->set_flag_bits(erhe::scene::Scene_item_flags::tool, tool);
    }


    ImGui::Unindent(indent);

    ImGui::TreePop();
}

[[nodiscard]] auto show_item_details(const erhe::scene::Scene_item* const item)
{
    return
        !is_physics         (item) &&
        !is_frame_controller(item) &&
        !is_raytrace        (item) &&
        !is_rendertarget    (item);
}

void Properties::item_properties(const std::shared_ptr<erhe::scene::Scene_item>& item)
{
    if (is_raytrace(item)) // currently nothing to show, so hidden from user
    {
        return;
    }

    if (
        !ImGui::TreeNodeEx(
            item->type_name(),
            ImGuiTreeNodeFlags_Framed |
            ImGuiTreeNodeFlags_DefaultOpen
        )
    )
    {
        return;
    }

    ImGui::PushID(item->get_label().c_str());

    if (show_item_details(item.get()))
    {
        std::string name = item->get_name();
        if (ImGui::InputText("Name", &name, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (name != item->get_name())
            {
                item->set_name(name);
            }
        }

        item_flags(item);

        if (!is_light(item)) // light uses light color, so hide item color
        {
            glm::vec4 color = item->get_wireframe_color();
            if (
                ImGui::ColorEdit4(
                    "Item Color",
                    &color.x,
                    ImGuiColorEditFlags_Float |
                    ImGuiColorEditFlags_NoInputs
                )
            )
            {
                item->set_wireframe_color(color);
            }
        }
    }

    auto node_physics = as_physics(item);
    if (node_physics)
    {
        node_physics_properties(*node_physics);
    }

    auto camera = as_camera(item);
    if (camera)
    {
        camera_properties(*camera);
    }

    auto light = as_light(item);
    if (light)
    {
        light_properties(*light);
    }

    auto mesh = as_mesh(item);
    if (mesh)
    {
        mesh_properties(*mesh);
    }

    auto rendertarget = as_rendertarget(item);
    if (rendertarget)
    {
        rendertarget_properties(*rendertarget);
    }

    const auto node = as_node(item);
    if (node)
    {
        transform_properties(*node.get());
    }

    ImGui::TreePop();
    ImGui::PopID();
}

void Properties::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto selected_material = m_content_library_window->selected_material();
    if (selected_material)
    {
        if (
            ImGui::TreeNodeEx(
                "Material",
                ImGuiTreeNodeFlags_Framed |
                ImGuiTreeNodeFlags_DefaultOpen
            )
        )
        {
            std::string name = selected_material->get_name();
            if (ImGui::InputText("Name", &name))
            {
                selected_material->set_name(name);
            }
            ImGui::SliderFloat("Metallic",    &selected_material->metallic,     0.0f, 1.0f);
            ImGui::SliderFloat("Roughness X", &selected_material->roughness.x,  0.0f, 1.0f);
            ImGui::SliderFloat("Roughness Y", &selected_material->roughness.y,  0.0f, 1.0f);
            ImGui::ColorEdit4 ("Base Color",  &selected_material->base_color.x, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4 ("Emissive",    &selected_material->emissive.x,   ImGuiColorEditFlags_Float);
            ImGui::TreePop();
        }
    }
#endif

    if (!m_selection_tool)
    {
        return;
    }

    const auto& selection = m_selection_tool->selection();
    for (const auto& item : selection)
    {
        ERHE_VERIFY(item);
        item_properties(item);
    }
#endif
}

} // namespace editor
