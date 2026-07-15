#include "windows/settings_window.hpp"

#include "windows/config_ui.hpp"
#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "scene/scene_root.hpp"
#include "windows/inventory_window.hpp"
#include "tools/debug_visualizations.hpp"
#include "editor_settings_store.hpp"
#include "config/generated/developer_config.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/shadow_filter_mode.hpp"
#include "config/generated/shadow_bias_mode.hpp"
#include "config/generated/shadow_cull_mode.hpp"
#include "config/generated/shadow_technique_mode.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "config/generated/renderer_config.hpp"
#include "config/generated/text_renderer_config.hpp"
#include "config/generated/threading_config.hpp"
#include "config/generated/window_config.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/generated/opengl_config.hpp"
#include "erhe_graphics/generated/vulkan_config.hpp"
#include "config/generated/editor_settings_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "config/generated/camera_controls_config_serialization.hpp"
#include "config/generated/render_style_appearance_serialization.hpp"
#include "config/generated/selection_outline_style_serialization.hpp"
#include "config/generated/mesh_component_style_serialization.hpp"
#include "config/generated/content_edge_lines_config_serialization.hpp"
#include "config/generated/preview_edge_lines_config_serialization.hpp"
#include "config/generated/debug_visualizations_settings_serialization.hpp"
#include "config/generated/developer_config_serialization.hpp"
#include "config/generated/grid_config_serialization.hpp"
#include "erhe_xr/generated/headset_config_serialization.hpp"
#include "config/generated/hotbar_config_serialization.hpp"
#include "config/generated/hud_config_serialization.hpp"
#include "config/generated/inventory_slot_serialization.hpp"
#include "config/generated/inventory_config_serialization.hpp"
#include "config/generated/id_renderer_config_serialization.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config_serialization.hpp"
#include "config/generated/network_config_serialization.hpp"
#include "config/generated/physics_config_serialization.hpp"
#include "config/generated/renderer_config_serialization.hpp"
#include "config/generated/scene_config_serialization.hpp"
#include "config/generated/shadow_frustum_fit_config_serialization.hpp"
#include "config/generated/sky_config_serialization.hpp"
#include "config/generated/text_renderer_config_serialization.hpp"
#include "config/generated/threading_config_serialization.hpp"
#include "config/generated/thumbnails_config_serialization.hpp"
#include "config/generated/transform_tool_config_serialization.hpp"
#include "config/generated/viewport_config_data_serialization.hpp"
#include "config/generated/window_config_serialization.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"
#include "erhe_graphics/generated/opengl_config_serialization.hpp"
#include "erhe_graphics/generated/vulkan_config_serialization.hpp"

#include "erhe_codegen/field_info.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor {

Settings_window::Settings_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Settings", "settings"}
    , m_context                {app_context}
{
    m_graphics_settings_subscription = app_message_bus.graphics_settings.subscribe(
        [&](Graphics_settings_message& /*message*/) {
            const std::string& current_preset_name = m_context.app_settings->graphics.current_graphics_preset.name;
            auto& graphics         = m_context.app_settings->graphics;
            auto& graphics_presets = graphics.graphics_presets;
            for (size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
                if (current_preset_name == graphics_presets[i].name) {
                    m_graphics_preset_index = static_cast<int>(i);
                    return;
                }
            }
        }
    );
}

void Settings_window::update_preset_names()
{
}

auto Settings_window::get_graphics_preset() -> Graphics_preset_entry&
{
    Graphics_settings&                  graphics         = m_context.app_settings->graphics;
    std::vector<Graphics_preset_entry>& graphics_presets = graphics.graphics_presets;
    Graphics_preset_entry&              graphics_preset  = graphics_presets.at(m_graphics_preset_index);
    return graphics_preset;
}

void Settings_window::imgui()
{
    reset();

    // Sync developer_mode from developer_config each frame so toggling the
    // checkbox in the Startup Configuration section takes effect immediately.
    if (m_context.developer_config != nullptr) {
        m_context.developer_mode = m_context.developer_config->enable || m_context.renderdoc;
    }

    const float ui_scale = m_context.app_settings->get_ui_scale();
    const ImVec2 button_size{110.0f * ui_scale, 0.0f};

    push_group("User Interface", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen);
    add_entry("UI Font Size", [this](){
        auto& imgui = m_context.app_settings->imgui;
        const bool font_size_changed = ImGui::DragFloat("UI Font Size", &imgui.font_size, 0.1f, 6.0f, 100.0f, "%.1f");
        if (font_size_changed) {
            m_context.imgui_renderer->on_font_config_changed(imgui);
        }
    });

    add_entry("Material Design Font Size", [this](){
        auto& imgui = m_context.app_settings->imgui;
        const bool font_size_changed = ImGui::DragFloat("Material Design Font Size", &imgui.material_design_font_size, 0.1f, 6.0f, 100.0f, "%.1f");
        if (font_size_changed) {
            m_context.imgui_renderer->on_font_config_changed(imgui);
        }
    });

    add_entry("Icon Font Size", [this](){
        auto& imgui = m_context.app_settings->imgui;
        const bool font_size_changed = ImGui::DragFloat("Icon Font Size", &imgui.icon_font_size, 0.1f, 6.0f, 100.0f, "%.1f");
        if (font_size_changed) {
            m_context.imgui_renderer->on_font_config_changed(imgui);
        }
    });

    add_entry("Small Icon Size", [this](){
        ImGui::DragInt("##", &m_context.app_settings->icon_settings.small_icon_size, 0.1f, 6, 512);
    });
    add_entry("Large Icon Size", [this](){
        ImGui::DragInt("##", &m_context.app_settings->icon_settings.large_icon_size, 0.1f, 6, 512);
    });

    add_entry("Hotbar Icon Size", [this](){
        ImGui::DragInt("##", &m_context.app_settings->icon_settings.hotbar_icon_size, 0.1f, 6, 512);
    });

    pop_group();

    push_group("Graphics", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen);

    auto& graphics = m_context.app_settings->graphics;
    add_entry("Currently Used Preset", [&graphics]() {
        ImGui::TextUnformatted(graphics.current_graphics_preset.name.c_str());
    });
    
    auto& graphics_presets = graphics.graphics_presets;
    if (!graphics_presets.empty()) {
        m_graphics_preset_names.resize(graphics_presets.size());
        for (std::size_t i = 0, end = graphics_presets.size(); i < end; ++i) {
            m_graphics_preset_names.at(i) = graphics_presets.at(i).name.c_str();
        }

        add_entry("Edit", [this](){
            ImGui::Combo(
                "##edit_preset",
                &m_graphics_preset_index,
                m_graphics_preset_names.data(),
                static_cast<int>(m_graphics_preset_names.size())
            );
        });
        //// show_settings(graphics_presets.at(m_graphics_preset_index));
    }

    ImGui::NewLine();

    if (!graphics_presets.empty()) {
        // The preset selected in the "Edit" combo above is the active preset:
        // editing any of its fields auto-applies and auto-saves through
        // App_settings::update(), so there is no separate "Use" / "Save
        // Presets" step. Keep the edit index in range (Remove may have shrunk
        // the list) before adopting its name as the active preset.
        if (m_graphics_preset_index < 0) {
            m_graphics_preset_index = 0;
        }
        if (m_graphics_preset_index >= static_cast<int>(graphics_presets.size())) {
            m_graphics_preset_index = static_cast<int>(graphics_presets.size()) - 1;
        }
        graphics.current_graphics_preset.name = graphics_presets.at(m_graphics_preset_index).name;

        add_entry("Preset Name", [this](){
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::InputText("##", &graphics_preset.name);
        });

        add_entry("MSAA Sample Count", [this](){
            Graphics_preset_entry& graphics_preset                = get_graphics_preset();
            Graphics_settings&     graphics                       = m_context.app_settings->graphics;
            std::vector<int>&      msaa_sample_count_entry_values = graphics.msaa_sample_count_entry_values;
            for (
                m_msaa_sample_count_entry_index = 0;
                m_msaa_sample_count_entry_index < msaa_sample_count_entry_values.size();
                ++m_msaa_sample_count_entry_index
            ) {
                if (msaa_sample_count_entry_values[m_msaa_sample_count_entry_index] >= graphics_preset.msaa_sample_count) {
                    break;
                }
            }
            m_msaa_sample_count_entry_index = std::min(
                m_msaa_sample_count_entry_index,
                static_cast<int>(msaa_sample_count_entry_values.size() - 1)
            );

            const bool msaa_changed = ImGui::Combo(
                "##",
                &m_msaa_sample_count_entry_index,
                graphics.msaa_sample_count_entry_s_strings.data(),
                static_cast<int>(graphics.msaa_sample_count_entry_s_strings.size())
            );
            if (msaa_changed) {
                graphics_preset.msaa_sample_count = msaa_sample_count_entry_values[m_msaa_sample_count_entry_index];
            }
        });

        add_entry("Shadows Enabled", [this]() {
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::Checkbox("##", &graphics_preset.shadow_enable);
        });
        add_entry("Shadow Filtering", [this]() {
            Graphics_preset_entry&          graphics_preset = get_graphics_preset();
            const Shadow_filter_mode        current         = graphics_preset.shadow_filter;
            const erhe::codegen::Enum_info& enum_info        = get_enum_info(static_cast<const Shadow_filter_mode*>(nullptr));
            if (ImGui::BeginCombo("##", std::string{::to_string(current)}.c_str())) {
                for (const erhe::codegen::Enum_value_info& value_info : enum_info.values) {
                    const Shadow_filter_mode mode     = static_cast<Shadow_filter_mode>(value_info.value);
                    const bool               selected = (mode == current);
                    if (ImGui::Selectable(value_info.name, selected)) {
                        graphics_preset.shadow_filter = mode;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        });
        add_entry("Shadow Bias", [this]() {
            Graphics_preset_entry&          graphics_preset = get_graphics_preset();
            const Shadow_bias_mode          current         = graphics_preset.shadow_bias;
            const erhe::codegen::Enum_info& enum_info        = get_enum_info(static_cast<const Shadow_bias_mode*>(nullptr));
            if (ImGui::BeginCombo("##", std::string{::to_string(current)}.c_str())) {
                for (const erhe::codegen::Enum_value_info& value_info : enum_info.values) {
                    const Shadow_bias_mode mode     = static_cast<Shadow_bias_mode>(value_info.value);
                    const bool             selected = (mode == current);
                    if (ImGui::Selectable(value_info.name, selected)) {
                        graphics_preset.shadow_bias = mode;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        });
        add_entry("Shadow Cull Mode", [this]() {
            Graphics_preset_entry&          graphics_preset = get_graphics_preset();
            const Shadow_cull_mode          current         = graphics_preset.shadow_cull_mode;
            const erhe::codegen::Enum_info& enum_info        = get_enum_info(static_cast<const Shadow_cull_mode*>(nullptr));
            if (ImGui::BeginCombo("##", std::string{::to_string(current)}.c_str())) {
                for (const erhe::codegen::Enum_value_info& value_info : enum_info.values) {
                    const Shadow_cull_mode mode     = static_cast<Shadow_cull_mode>(value_info.value);
                    const bool             selected = (mode == current);
                    if (ImGui::Selectable(value_info.name, selected)) {
                        graphics_preset.shadow_cull_mode = mode;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        });
        add_entry("Shadow Technique", [this]() {
            Graphics_preset_entry&          graphics_preset = get_graphics_preset();
            const Shadow_technique_mode     current         = graphics_preset.shadow_technique;
            const erhe::codegen::Enum_info& enum_info        = get_enum_info(static_cast<const Shadow_technique_mode*>(nullptr));
            if (ImGui::BeginCombo("##", std::string{::to_string(current)}.c_str())) {
                for (const erhe::codegen::Enum_value_info& value_info : enum_info.values) {
                    const Shadow_technique_mode mode     = static_cast<Shadow_technique_mode>(value_info.value);
                    const bool                  selected = (mode == current);
                    if (ImGui::Selectable(value_info.name, selected)) {
                        graphics_preset.shadow_technique = mode;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        });
        add_entry("Shadow Depth Bias (constant)", [this]() {
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::DragFloat("##", &graphics_preset.shadow_depth_bias_constant, 0.05f, -16.0f, 16.0f, "%.2f");
        });
        add_entry("Shadow Depth Bias (slope)", [this]() {
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::DragFloat("##", &graphics_preset.shadow_depth_bias_slope, 0.05f, -16.0f, 16.0f, "%.2f");
        });
        add_entry("Shadow Resolution", [this](){
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            const int   shadow_resolution_values[] = {  256, 512, 1024, 1024 * 2, 1024 * 3, 1024 * 4, 1024 * 5, 1024 * 6, 1024 * 7, 1024 * 8 };
            const char* shadow_resolution_items [] = { "256", "512", "1024", "2048", "3072", "4096", "5120", "6144", "7168", "8192" };
            m_shadow_resolution_index = 0;
            int min_distance = std::numeric_limits<int>::max();
            for (int i = 0, end = IM_ARRAYSIZE(shadow_resolution_values); i < end; ++i) {
                int entry_distance = std::abs(shadow_resolution_values[i] - graphics_preset.shadow_resolution);
                if (entry_distance < min_distance) {
                    m_shadow_resolution_index = i;
                    min_distance = entry_distance;
                }
            }
            if (ImGui::Combo(
                "##",
                &m_shadow_resolution_index,
                shadow_resolution_items,
                IM_ARRAYSIZE(shadow_resolution_items),
                IM_ARRAYSIZE(shadow_resolution_items)
            )) {
                graphics_preset.shadow_resolution = shadow_resolution_values[m_shadow_resolution_index];
            }
        });
        {
            add_entry(
                "Shadow Depth Bits",
                [this]() {
                    Graphics_preset_entry& graphics_preset = get_graphics_preset();
                    std::vector<erhe::dataformat::Format> formats = m_context.graphics_device->get_supported_depth_stencil_formats();
                    std::set<int> depth_size_set;
                    for (const erhe::dataformat::Format format : formats) {
                        depth_size_set.insert(static_cast<int>(erhe::dataformat::get_depth_size_bits(format)));
                    }
                    if (depth_size_set.empty()) {
                        return;
                    }
                    std::vector<int> depth_sizes(depth_size_set.begin(), depth_size_set.end());
                    std::sort(depth_sizes.begin(), depth_sizes.end());

                    bool found = false;
                    for (size_t i = 0, end = depth_sizes.size(); i < end; ++i) {
                        if (depth_sizes[i] == graphics_preset.shadow_depth_bits) {
                            m_shadow_depth_bits_index = static_cast<int>(i);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        m_shadow_depth_bits_index = 0;
                    }

                    std::string preview_value = fmt::format("{} bits", depth_sizes.at(m_shadow_depth_bits_index));
                    if (ImGui::BeginCombo("##", preview_value.c_str())) {
                        for (size_t i = 0, end = depth_sizes.size(); i < end; ++i) {
                            std::string option_label = fmt::format("{} bits", depth_sizes.at(i));
                            if (ImGui::Selectable(option_label.c_str(), m_shadow_depth_bits_index == static_cast<int>(i))) {
                                m_shadow_depth_bits_index = static_cast<int>(i);
                                graphics_preset.shadow_depth_bits = depth_sizes.at(i);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            );
        }

        //ImGui::SliderInt  ("Shadow Resolution",  &graphics_preset.shadow_resolution,  1, graphics.max_shadow_resolution);
        add_entry("Shadow Light Count", [this](){
            Graphics_preset_entry&   graphics_preset = get_graphics_preset();
            Graphics_settings& graphics        = m_context.app_settings->graphics;
            ImGui::SliderInt("##", &graphics_preset.shadow_light_count, 1, std::min(graphics.max_depth_layers, 32));
        });
        // Point lights use a separate R32F cube-map array for omnidirectional
        // shadows; these size that array independently of the 2D shadow map.
        add_entry("Point Shadow Resolution", [this](){
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            Graphics_settings&     graphics        = m_context.app_settings->graphics;
            ImGui::SliderInt("##", &graphics_preset.point_shadow_resolution, 1, std::min(graphics.max_shadow_resolution, 4096));
        });
        add_entry("Point Shadow Count", [this](){
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::SliderInt("##", &graphics_preset.point_shadow_light_count, 0, 8);
        });
    }

    add_entry("", [this, button_size](){
        std::vector<Graphics_preset_entry>& graphics_presets = m_context.app_settings->graphics.graphics_presets;

        const bool add_pressed = ImGui::Button("Add", button_size);
        if (add_pressed || graphics_presets.empty()) {
            Graphics_preset_entry new_graphics_preset{
                .name = "New preset"
            };
            graphics_presets.push_back(new_graphics_preset);
            m_graphics_preset_index = static_cast<int>(graphics_presets.size() - 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove", button_size)) {
            graphics_presets.erase(graphics_presets.begin() + m_graphics_preset_index);
        }
        // No "Use" button: the edited preset is the active preset and is
        // applied automatically by App_settings::update().
    });
    pop_group();

    // The graphics preset list (graphics_presets.json) auto-applies and
    // auto-saves through App_settings::update(), like editor_settings.json.
    // "Load Presets" remains for re-reading the file from disk on demand.
    add_entry("", [this, button_size](){
        if (ImGui::Button("Load Presets", button_size)) {
            m_context.app_settings->read(m_context.OpenXR);
        }
    });

    const bool show_developer = (m_context.developer_config != nullptr) && m_context.developer_config->enable;

    // Thin forwarder to the shared reflection renderer (windows/config_ui.hpp),
    // so the many call sites below stay terse. label_override gives a distinct
    // group header when the same struct type is shown more than once.
    auto add_config_section = [this, show_developer](auto& section, const char* label_override = nullptr) {
        editor::add_config_section(*this, show_developer, section, label_override);
    };

    push_group("Startup Configuration", ImGuiTreeNodeFlags_Framed);
    if (m_context.graphics_config != nullptr) {
        add_config_section(*m_context.graphics_config);
        add_config_section(m_context.graphics_config->opengl);
        add_config_section(m_context.graphics_config->vulkan);
    }
    if (m_context.mesh_memory_config != nullptr) {
        add_config_section(*m_context.mesh_memory_config);
    }
    if (m_context.renderer_config != nullptr) {
        add_config_section(*m_context.renderer_config);
    }
    if (m_context.text_renderer_config != nullptr) {
        add_config_section(*m_context.text_renderer_config);
    }
    if (m_context.window_config != nullptr) {
        add_config_section(*m_context.window_config);
    }
    add_entry("", [this, button_size](){
        if (ImGui::Button("Save Config", button_size)) {
            if (m_context.graphics_config != nullptr) {
                erhe::codegen::save_config(*m_context.graphics_config, "config/editor/erhe_graphics.json");
            }
            if (m_context.mesh_memory_config != nullptr) {
                erhe::codegen::save_config(*m_context.mesh_memory_config, "config/editor/mesh_memory.json");
            }
            if (m_context.renderer_config != nullptr) {
                erhe::codegen::save_config(*m_context.renderer_config, "config/editor/renderer.json");
            }
            if (m_context.text_renderer_config != nullptr) {
                erhe::codegen::save_config(*m_context.text_renderer_config, "config/editor/text_renderer.json");
            }
            if (m_context.window_config != nullptr) {
                erhe::codegen::save_config(*m_context.window_config, "config/editor/window.json");
            }
        }
    });
    pop_group();

    if (m_context.editor_settings != nullptr) {
        Editor_settings_config& settings = *m_context.editor_settings;

        push_group("Editor Settings", ImGuiTreeNodeFlags_Framed);
        add_entry("Post Processing", [&settings](){
            ImGui::Checkbox("##", &settings.post_processing);
        }, "Enable Post Processing. Takes effect on next viewport creation.");
        add_config_section(settings.camera_controls);
        // Default Visual Style for new viewports - shown near the top since it
        // is one of the more commonly edited sections.
        add_config_section(settings.viewport);
        // Editor-global render-style appearance (colors / widths / point size /
        // color sources), shared by all scene views. The per-view render styles
        // (Visual Style popup) keep only the visibility toggles. Two sets, one
        // per render style; both are the same struct type, so a label override
        // gives each a distinct group header.
        add_config_section(settings.render_style_appearance,          "Default Style Appearance");
        add_config_section(settings.selected_render_style_appearance, "Selection Style Appearance");
        // Editor-global selection outline appearance (moved out of the per-view
        // Visual Style popup).
        add_config_section(settings.selection_outline);
        // Editor-global mesh-component (vertex / edge / face) selection style.
        add_config_section(settings.mesh_component_style);
        // Editor-global gizmo scale + viewport clear color (moved out of the
        // per-view Visual Style popup).
        push_group("Viewport", ImGuiTreeNodeFlags_Framed);
        add_entry("Gizmo Scale", [&settings](){ ImGui::SliderFloat("##", &settings.gizmo_scale, 1.0f, 20.0f, "%.2f"); }, "Scale factor for the transform gizmo handles.");
        add_entry("Clear Color", [&settings](){ ImGui::ColorEdit4("##", &settings.clear_color.x, ImGuiColorEditFlags_Float); }, "Viewport background clear color.");
        pop_group();
        // Editor-global content edge-line (wide-line) method + bias tuning.
        add_config_section(settings.content_edge_lines);
        // Edge-line overlay for the preview thumbnails (same struct shown
        // twice; the label override gives each a distinct group header).
        add_config_section(settings.graph_node_preview_edge_lines, "Graph Node Preview Edge Lines");
        add_config_section(settings.brush_preview_edge_lines,      "Brush Preview Edge Lines");
        // Note: the per-view debug-visualization enable toggles / modes
        // (Debug_visualizations_settings) are intentionally NOT shown here -
        // they live only per scene view (edited in the scene-view Debug
        // Visualization popup). Only the editor-global style is global.
        // Editor-global debug-visualization appearance (colors, line widths,
        // label geometry), shared by all scene views. The per-view enable
        // toggles stay in the scene-view Debug Visualization popup. Rendered
        // with the hand-grouped layout (Shadow Fit / Selection / Annotations)
        // rather than the flat reflection list.
        Debug_visualizations::style_imgui(*this, settings.debug_visualizations_style);
        add_config_section(settings.developer);
        add_config_section(settings.grid);
        add_config_section(settings.headset);
        add_config_section(settings.hotbar);
        add_config_section(settings.hud);
        add_config_section(settings.inventory);
        add_config_section(settings.id_renderer);
        add_config_section(settings.network);
        add_config_section(settings.physics);
        add_config_section(settings.scene);
        add_config_section(settings.shadow_frustum_fit);
        add_config_section(settings.sky);
        add_config_section(settings.thumbnails);
        add_config_section(settings.transform_tool);

        add_entry("", [this, button_size, &settings](){
            if (ImGui::Button("Save Settings", button_size)) {
                if (m_context.app_settings != nullptr) {
                    m_context.app_settings->settings_store().save();
                }
            }
        });
        pop_group();

        // Per-scene setting overrides (issues #239 / #240) moved to the
        // Properties window: they are shown there when a Scene is selected
        // (Properties::scene_properties), keyed on the selected scene rather
        // than only the single registered scene root.
    }

    show_entries("Settings", ImVec2{1.0f, 1.0f});
}

}
