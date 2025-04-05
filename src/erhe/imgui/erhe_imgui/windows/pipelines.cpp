#include "erhe_imgui/windows/pipelines.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"

#include "erhe_gl/enum_base_zero_functions.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace erhe::imgui {

Pipelines::Pipelines(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows)
    : Imgui_window{imgui_renderer, imgui_windows, "Pipelines", "pipelines"}
{
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
const char* gl_cull_face_mode_strings[] = {
    "Front",
    "Back",
    "Front and Back"
};

const char* gl_front_face_direction_strings[] = {
    "Clockwise",
    "Counter-Clockwise"
};

const char* gl_polygon_mode_strings[] = {
    "Point",
    "Line",
    "Fill"
};

const char* gl_depth_function_strings[] = {
    "Never",
    "Less",
    "Equal",
    "Less or Equal",
    "Greater",
    "Not Equal",
    "Greater or Equal",
    "Always",
};

const char* gl_stencil_op_strings[] = {
    "Zero",
    "Invert",
    "Keep",
    "Replace",
    "Increment",
    "Decrement",
    "Increment with Wrap",
    "Decrement with Wrap"
};

const char* gl_stencil_function_strings[] = {
    "Never",
    "Less",
    "Equal",
    "Less or Equal",
    "Greater",
    "Not Equal",
    "Greater or Equal",
    "Always"
};

const char* gl_equation_mode_strings[] = {
    "Add",             // 0
    "Min",             // 1
    "Max",             // 2
    "Substract",       // 3
    "Reverse Subtract" // 4
};

const char* gl_blending_factor_strings[] = {
    "0",
    "1",
    "Source Color",
    "1 - Source Color",
    "Source Alpha",
    "1 - Source Alpha",
    "Destination Alpha",
    "1 - Destiniation Alpha",
    "Destination Color",
    "1 - Destination Color",
    "Source Alpha Saturate",
    "Constant Color",
    "1 - Constant Color",
    "Constant Alpha",
    "1 - Constant Alpha",
    "Source 1 Alpha",
    "Source 1 Color",
    "1 - Source 1 Color",
    "1 - Source 1 Alpha"
};

constexpr float item_width{140.0f};

template <typename T>
void make_combo(
    const char*                    label,
    T&                             value,
    const char* const              items[],
    int                            items_count,
    T(*to_value)(const size_t)
)
{
    int int_value = static_cast<int>(gl::base_zero(value));
    ImGui::SetNextItemWidth(item_width);
    const bool changed = ImGui::Combo(
        label,
        &int_value,
        items,
        items_count
    );
    if (changed) {
        value = to_value(static_cast<size_t>(int_value));
    }
}

void Pipelines::rasterization(erhe::graphics::Rasterization_state& rasterization)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !ImGui::TreeNodeEx(
            "Rasterization",
            ImGuiTreeNodeFlags_Framed      |
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_NoTreePushOnOpen
        )
    ) {
        return;
    }

    ImGui::PushID("Rasterization");
    ImGui::SetNextItemWidth(item_width);
    ImGui::Checkbox("Face Cull", &rasterization.face_cull_enable);
    if (rasterization.face_cull_enable) {
        // TODO Take into account reverse depth
        make_combo<gl::Cull_face_mode>(
            "Cull Mode",
            rasterization.cull_face_mode,
            gl_cull_face_mode_strings,
            IM_ARRAYSIZE(gl_cull_face_mode_strings),
            gl::Cull_face_mode_from_base_zero
        );
    }
    ImGui::Checkbox("Depth Clamp", &rasterization.depth_clamp_enable);
    make_combo<gl::Front_face_direction>(
        "Front Face Direction",
        rasterization.front_face_direction,
        gl_front_face_direction_strings,
        IM_ARRAYSIZE(gl_front_face_direction_strings),
        gl::Front_face_direction_from_base_zero
    );
    //ImGui::Text(
    //    "Reverse Depth: %s",
    //    g_configuration->graphics.reverse_depth
    //        ? "Yes"
    //        : "No"
    //);
    make_combo<gl::Polygon_mode>(
        "Polygon Mode",
        rasterization.polygon_mode,
        gl_polygon_mode_strings,
        IM_ARRAYSIZE(gl_polygon_mode_strings),
        gl::Polygon_mode_from_base_zero
    );

    ImGui::PopID();
}

void make_hex_uint(const char* label, unsigned int& value)
{
    int int_value = static_cast<int>(value);
    ImGui::SetNextItemWidth(item_width);
    const bool changed = ImGui::DragInt(
        label,
        &int_value,
        1.0f,
        0,
        255,
        "%02x"
    );
    if (changed) {
        value = static_cast<unsigned int>(int_value);
    }
}

void Pipelines::stencil_op(const char* label, erhe::graphics::Stencil_op_state& stencil_op)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !ImGui::TreeNodeEx(
            label,
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_NoTreePushOnOpen
        )
    ) {
        return;
    }

    ImGui::PushID(label);

    make_combo<gl::Stencil_op>(
        "Stencil Fail Operation",
        stencil_op.stencil_fail_op,
        gl_stencil_op_strings,
        IM_ARRAYSIZE(gl_stencil_op_strings),
        gl::Stencil_op_from_base_zero
    );

    make_combo<gl::Stencil_op>(
        "Depth Fail Operation",
        stencil_op.z_fail_op,
        gl_stencil_op_strings,
        IM_ARRAYSIZE(gl_stencil_op_strings),
        gl::Stencil_op_from_base_zero
    );

    make_combo<gl::Stencil_op>(
        "Depth Pass Operation",
        stencil_op.z_pass_op,
        gl_stencil_op_strings,
        IM_ARRAYSIZE(gl_stencil_op_strings),
        gl::Stencil_op_from_base_zero
    );

    make_combo<gl::Stencil_function>(
        "Function",
        stencil_op.function,
        gl_stencil_function_strings,
        IM_ARRAYSIZE(gl_stencil_function_strings),
        gl::Stencil_function_from_base_zero
    );

    make_hex_uint("Reference",  stencil_op.reference);
    make_hex_uint("Test Mask",  stencil_op.test_mask);
    make_hex_uint("Write Mask", stencil_op.write_mask);

    ImGui::PopID();
}

void Pipelines::depth_stencil(erhe::graphics::Depth_stencil_state& depth_stencil)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !ImGui::TreeNodeEx(
            "Depth & Stencil",
            ImGuiTreeNodeFlags_Framed      |
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_NoTreePushOnOpen
        )
    ) {
        return;
    }

    ImGui::PushID("Depth & Stencil");
    ImGui::SetNextItemWidth(item_width);
    ImGui::Checkbox("Depth Test",  &depth_stencil.depth_test_enable);
    ImGui::SetNextItemWidth(item_width);
    ImGui::Checkbox("Depth Write", &depth_stencil.depth_write_enable);
    make_combo<gl::Depth_function>(
        "Depth function",
        depth_stencil.depth_compare_op,
        gl_depth_function_strings,
        IM_ARRAYSIZE(gl_depth_function_strings),
        gl::Depth_function_from_base_zero
    );
    ImGui::SetNextItemWidth(item_width);
    ImGui::Checkbox("Stencil Test", &depth_stencil.stencil_test_enable);
    if (depth_stencil.stencil_test_enable) {
        stencil_op("Stencil Front", depth_stencil.stencil_front);
        stencil_op("Stencil Back",  depth_stencil.stencil_back);
    }

    ImGui::PopID();
}

void Pipelines::blend_state_component(const char* label, erhe::graphics::Blend_state_component& component)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !ImGui::TreeNodeEx(
            label,
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_NoTreePushOnOpen
        )
    ) {
        return;
    }

    ImGui::PushID(label);

    make_combo<gl::Blend_equation_mode>(
        "Equation Mode",
        component.equation_mode,
        gl_equation_mode_strings,
        IM_ARRAYSIZE(gl_equation_mode_strings),
        gl::Blend_equation_mode_from_base_zero
    );

    make_combo<gl::Blending_factor>(
        "Source Factor",
        component.source_factor,
        gl_blending_factor_strings,
        IM_ARRAYSIZE(gl_blending_factor_strings),
        gl::Blending_factor_from_base_zero
    );

    make_combo<gl::Blending_factor>(
        "Destination Factor",
        component.destination_factor,
        gl_blending_factor_strings,
        IM_ARRAYSIZE(gl_blending_factor_strings),
        gl::Blending_factor_from_base_zero
    );

    ImGui::PopID();
}

void Pipelines::color_blend(erhe::graphics::Color_blend_state& color_blend)
{
    ERHE_PROFILE_FUNCTION();

    if (
        !ImGui::TreeNodeEx(
            "Color Blend",
            ImGuiTreeNodeFlags_Framed      |
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_NoTreePushOnOpen
        )
    ) {
        return;
    }

    ImGui::PushID("Color Blend");

    ImGui::SetNextItemWidth(item_width);
    ImGui::Checkbox("Blending Enabled", &color_blend.enabled);
    if (color_blend.enabled) {
        blend_state_component("RGB",   color_blend.rgb);
        blend_state_component("Alpha", color_blend.alpha);
        ImGui::SetNextItemWidth(item_width);
        ImGui::ColorEdit4(
            "Color",
            &color_blend.constant[0],
            ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_Float
        );
    }

    if (ImGui::TreeNodeEx("Write Mask", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(item_width);
        ImGui::Checkbox("Red",   &color_blend.write_mask.red);
        ImGui::SetNextItemWidth(item_width);
        ImGui::Checkbox("Green", &color_blend.write_mask.green);
        ImGui::SetNextItemWidth(item_width);
        ImGui::Checkbox("Blue",  &color_blend.write_mask.blue);
        ImGui::SetNextItemWidth(item_width);
        ImGui::Checkbox("Alpha", &color_blend.write_mask.alpha);
        ImGui::TreePop();
    }

    ImGui::PopID();
}
#endif

void Pipelines::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    //const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
    auto pipelines = erhe::graphics::Pipeline::get_pipelines();
    for (size_t i = 0, end = pipelines.size(); i < end; ++i) {
        auto* pipeline = pipelines[i];
        if (
            (pipeline == nullptr) ||
            (pipeline->data.name == nullptr)
        ) {
            continue;
        }
        pipeline_imgui(*pipeline);
    }
#endif
}

void pipeline_imgui(erhe::graphics::Pipeline& pipeline)
{
    const char* name = (pipeline.data.name != nullptr) ? pipeline.data.name : "";
    if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_Framed)) {
        if (pipeline.data.shader_stages != nullptr) {
            ImGui::Text("Shader stages: %s", pipeline.data.shader_stages->name().c_str());
        }
        if (pipeline.data.vertex_input != nullptr) {
            if (ImGui::TreeNodeEx("Vertex input", ImGuiTreeNodeFlags_Framed)) {
                const erhe::graphics::Vertex_input_state_data& vertex_input_data = pipeline.data.vertex_input->get_data();
                int attribute_index = 0;
                for (const erhe::graphics::Gl_vertex_input_attribute& attribute : vertex_input_data.attributes) {
                    std::string attribute_label = fmt::format("Attribute {}", attribute_index++);
                    ImGui::PushID(attribute_index);
                    if (ImGui::TreeNodeEx(attribute_label.c_str(), ImGuiTreeNodeFlags_Framed)) {
                        ImGui::Text("Location: %u",           attribute.layout_location);
                        ImGui::Text("Stride: %d",             attribute.stride);
                        ImGui::Text("Dimension: %d",          attribute.dimension);
                        ImGui::Text("Attribute type: %s",     gl::c_str(attribute.gl_attribute_type));
                        ImGui::Text("Vertex Attrib type: %s", gl::c_str(attribute.gl_vertex_atrib_type));
                        ImGui::Text("Normalized: %s",         attribute.normalized ? "yes" : "no");
                        ImGui::Text("Offset: %u",             attribute.offset);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                int binding_index = 0;
                for (const erhe::graphics::Vertex_input_binding& binding : vertex_input_data.bindings) {
                    std::string binding_label = fmt::format("Binding {}", binding_index++);
                    ImGui::PushID(100 + binding_index);
                    if (ImGui::TreeNodeEx(binding_label.c_str(), ImGuiTreeNodeFlags_Framed)) {
                        ImGui::Text("Binding: %u", binding.binding);
                        ImGui::Text("Stride: %d", binding.stride);
                        ImGui::Text("Divisor: %d", binding.divisor);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
        ImGui::Text("Primitive type: %s", gl::c_str(pipeline.data.input_assembly.primitive_topology));
        ImGui::Text("Primitive restart: %s", pipeline.data.input_assembly.primitive_restart ? "yes" : "no");

        Pipelines::rasterization(pipeline.data.rasterization);
        Pipelines::depth_stencil(pipeline.data.depth_stencil);
        Pipelines::color_blend  (pipeline.data.color_blend);
        ImGui::TreePop();
    }
}

} // namespace erhe::imgui
