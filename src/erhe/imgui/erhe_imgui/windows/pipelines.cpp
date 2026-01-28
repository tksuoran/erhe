#include "erhe_imgui/windows/pipelines.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>

namespace erhe::imgui {

Pipelines::Pipelines(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows)
    : Imgui_window{imgui_renderer, imgui_windows, "Pipelines", "pipelines", true}
{
}

const char* cull_face_mode_strings[] = {
    "Back",
    "Front",
    "Front and Back"
};

const char* front_face_direction_strings[] = {
    "Counter-Clockwise",
    "Clockwise"
};

const char* polygon_mode_strings[] = {
    "Fill",
    "Line",
    "Point",
};

const char* depth_function_strings[] = {
    "Never",
    "Less",
    "Equal",
    "Less or Equal",
    "Greater",
    "Not Equal",
    "Greater or Equal",
    "Always",
};

const char* stencil_op_strings[] = {
    "Zero",
    "Decrement",
    "Decrement with Wrap",
    "Increment",
    "Increment with Wrap",
    "Invert",
    "Keep",
    "Replace"
};

const char* stencil_function_strings[] = {
    "Never",
    "Less",
    "Equal",
    "Less or Equal",
    "Greater",
    "Not Equal",
    "Greater or Equal",
    "Always"
};

const char* equation_mode_strings[] = {
    "Add",              // 0
    "Reverse Subtract", // 1
    "Substract",        // 2
    "Max",              // 3
    "Min"               // 4
};

const char* blending_factor_strings[] = {
    "0",
    "Constant Alpha",
    "Constant Color",
    "Destination Alpha",
    "Destination Color",
    "1",
    "1 - Constant Alpha",
    "1 - Constant Color",
    "1 - Destination Alpha",
    "1 - Destination Color",
    "1 - Source 1 Alpha",
    "1 - Source 1 Color",
    "1 - Source Alpha",
    "1 - Source Color",
    "Source 1 Alpha",
    "Source 1 Color",
    "Source Alpha",
    "Source Alpha Saturate",
    "Source Color"
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
    int int_value = static_cast<int>(value);
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
        make_combo<erhe::graphics::Cull_face_mode>(
            "Cull Mode",
            rasterization.cull_face_mode,
            cull_face_mode_strings,
            IM_ARRAYSIZE(cull_face_mode_strings),
            [](const std::size_t ui_value) { return static_cast<erhe::graphics::Cull_face_mode>(ui_value); }
        );
    }
    ImGui::Checkbox("Depth Clamp", &rasterization.depth_clamp_enable);
    make_combo<erhe::graphics::Front_face_direction>(
        "Front Face Direction",
        rasterization.front_face_direction,
        front_face_direction_strings,
        IM_ARRAYSIZE(front_face_direction_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Front_face_direction>(ui_value); }
    );
    //ImGui::Text(
    //    "Reverse Depth: %s",
    //    g_configuration->graphics.reverse_depth
    //        ? "Yes"
    //        : "No"
    //);
    make_combo<erhe::graphics::Polygon_mode>(
        "Polygon Mode",
        rasterization.polygon_mode,
        polygon_mode_strings,
        IM_ARRAYSIZE(polygon_mode_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Polygon_mode>(ui_value); }
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

    make_combo<erhe::graphics::Stencil_op>(
        "Stencil Fail Operation",
        stencil_op.stencil_fail_op,
        stencil_op_strings,
        IM_ARRAYSIZE(stencil_op_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Stencil_op>(ui_value); }
    );

    make_combo<erhe::graphics::Stencil_op>(
        "Depth Fail Operation",
        stencil_op.z_fail_op,
        stencil_op_strings,
        IM_ARRAYSIZE(stencil_op_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Stencil_op>(ui_value); }
    );

    make_combo<erhe::graphics::Stencil_op>(
        "Depth Pass Operation",
        stencil_op.z_pass_op,
        stencil_op_strings,
        IM_ARRAYSIZE(stencil_op_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Stencil_op>(ui_value); }
    );

    make_combo<erhe::graphics::Compare_operation>(
        "Function",
        stencil_op.function,
        stencil_function_strings,
        IM_ARRAYSIZE(stencil_function_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Compare_operation>(ui_value); }
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
    make_combo<erhe::graphics::Compare_operation>(
        "Depth function",
        depth_stencil.depth_compare_op,
        depth_function_strings,
        IM_ARRAYSIZE(depth_function_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Compare_operation>(ui_value); }
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

    make_combo<erhe::graphics::Blend_equation_mode>(
        "Equation Mode",
        component.equation_mode,
        equation_mode_strings,
        IM_ARRAYSIZE(equation_mode_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Blend_equation_mode>(ui_value); }
    );

    make_combo<erhe::graphics::Blending_factor>(
        "Source Factor",
        component.source_factor,
        blending_factor_strings,
        IM_ARRAYSIZE(blending_factor_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Blending_factor>(ui_value); }
    );

    make_combo<erhe::graphics::Blending_factor>(
        "Destination Factor",
        component.destination_factor,
        blending_factor_strings,
        IM_ARRAYSIZE(blending_factor_strings),
        [](const std::size_t ui_value) { return static_cast<erhe::graphics::Blending_factor>(ui_value); }
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

void Pipelines::imgui()
{
    ERHE_PROFILE_FUNCTION();

    //const auto button_size = ImVec2{ImGui::GetContentRegionAvail().x, 0.0f};
    auto pipelines = erhe::graphics::Render_pipeline_state::get_pipelines();
    for (size_t i = 0, end = pipelines.size(); i < end; ++i) {
        ImGui::PushID(static_cast<int>(i));
        auto* pipeline = pipelines[i];
        if (
            (pipeline == nullptr) ||
            (pipeline->data.name == nullptr)
        ) {
            continue;
        }
        pipeline_imgui(*pipeline);
        ImGui::PopID();
    }
}

void pipeline_imgui(erhe::graphics::Render_pipeline_state& pipeline)
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
                for (const erhe::graphics::Vertex_input_attribute& attribute : vertex_input_data.attributes) {
                    std::string attribute_label = fmt::format("Attribute {}", attribute_index++);
                    ImGui::PushID(attribute_index);
                    if (ImGui::TreeNodeEx(attribute_label.c_str(), ImGuiTreeNodeFlags_Framed)) {
                        ImGui::Text("Location: %u", attribute.layout_location);
                        ImGui::Text("Stride: %zu",   attribute.stride);
                        ImGui::Text("Format: %s",   c_str(attribute.format) ? "yes" : "no");
                        ImGui::Text("Offset: %zu",   attribute.offset);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                int binding_index = 0;
                for (const erhe::graphics::Vertex_input_binding& binding : vertex_input_data.bindings) {
                    std::string binding_label = fmt::format("Binding {}", binding_index++);
                    ImGui::PushID(100 + binding_index);
                    if (ImGui::TreeNodeEx(binding_label.c_str(), ImGuiTreeNodeFlags_Framed)) {
                        ImGui::Text("Binding: %zu", binding.binding);
                        ImGui::Text("Stride: %zu",  binding.stride);
                        ImGui::Text("Divisor: %u", binding.divisor);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
        ImGui::Text("Primitive type: %s", erhe::graphics::c_str(pipeline.data.input_assembly.primitive_topology));
        ImGui::Text("Primitive restart: %s", pipeline.data.input_assembly.primitive_restart ? "yes" : "no");

        Pipelines::rasterization(pipeline.data.rasterization);
        Pipelines::depth_stencil(pipeline.data.depth_stencil);
        Pipelines::color_blend  (pipeline.data.color_blend);
        ImGui::TreePop();
    }
}

} // namespace erhe::imgui
