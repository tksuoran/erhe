#include "windows/settings_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "windows/inventory_window.hpp"
#include "config/generated/erhe_config.hpp"
#include "config/generated/erhe_config_serialization.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/editor_settings_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "config/generated/camera_controls_config_serialization.hpp"
#include "config/generated/developer_config_serialization.hpp"
#include "config/generated/grid_config_serialization.hpp"
#include "config/generated/headset_config_serialization.hpp"
#include "config/generated/hotbar_config_serialization.hpp"
#include "config/generated/hud_config_serialization.hpp"
#include "config/generated/inventory_slot_serialization.hpp"
#include "config/generated/inventory_config_serialization.hpp"
#include "config/generated/id_renderer_config_serialization.hpp"
#include "config/generated/mesh_memory_config_serialization.hpp"
#include "config/generated/network_config_serialization.hpp"
#include "config/generated/physics_config_serialization.hpp"
#include "config/generated/renderer_config_serialization.hpp"
#include "config/generated/scene_config_serialization.hpp"
#include "config/generated/shader_monitor_config_serialization.hpp"
#include "config/generated/text_renderer_config_serialization.hpp"
#include "config/generated/threading_config_serialization.hpp"
#include "config/generated/thumbnails_config_serialization.hpp"
#include "config/generated/transform_tool_config_serialization.hpp"
#include "config/generated/viewport_config_data_serialization.hpp"
#include "config/generated/window_config_serialization.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"

#include "erhe_codegen/field_info.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace {

void imgui_field(void* base, const erhe::codegen::Field_info& field)
{
    using erhe::codegen::Field_type;
    void* ptr = static_cast<char*>(base) + field.offset;

    switch (field.field_type) {
        case Field_type::bool_:
            ImGui::Checkbox("##", static_cast<bool*>(ptr));
            break;
        case Field_type::int_:
        case Field_type::int8:
        case Field_type::int16:
        case Field_type::int32:
        case Field_type::int64:
            if (field.numeric_limits.has_ui_min && field.numeric_limits.has_ui_max) {
                ImGui::SliderInt("##", static_cast<int*>(ptr),
                    static_cast<int>(field.numeric_limits.ui_min),
                    static_cast<int>(field.numeric_limits.ui_max));
            } else {
                ImGui::DragInt("##", static_cast<int*>(ptr));
            }
            break;
        case Field_type::unsigned_int:
        case Field_type::uint8:
        case Field_type::uint16:
        case Field_type::uint32:
        case Field_type::uint64:
            ImGui::DragInt("##", static_cast<int*>(ptr));
            break;
        case Field_type::float_:
            if (field.numeric_limits.has_ui_min && field.numeric_limits.has_ui_max) {
                ImGui::SliderFloat("##", static_cast<float*>(ptr),
                    static_cast<float>(field.numeric_limits.ui_min),
                    static_cast<float>(field.numeric_limits.ui_max));
            } else {
                ImGui::DragFloat("##", static_cast<float*>(ptr), 0.01f);
            }
            break;
        case Field_type::double_:
            {
                float v = static_cast<float>(*static_cast<double*>(ptr));
                if (ImGui::DragFloat("##", &v, 0.01f)) {
                    *static_cast<double*>(ptr) = static_cast<double>(v);
                }
            }
            break;
        case Field_type::string:
            ImGui::InputText("##", static_cast<std::string*>(ptr));
            break;
        case Field_type::vec2:
            ImGui::DragFloat2("##", static_cast<float*>(ptr), 0.01f);
            break;
        case Field_type::vec3:
            ImGui::ColorEdit3("##", static_cast<float*>(ptr));
            break;
        case Field_type::vec4:
            ImGui::ColorEdit4("##", static_cast<float*>(ptr));
            break;
        case Field_type::ivec2:
            ImGui::DragInt2("##", static_cast<int*>(ptr));
            break;
        case Field_type::mat4:
        case Field_type::vector:
        case Field_type::array:
        case Field_type::optional:
        case Field_type::struct_ref:
        case Field_type::enum_ref:
            ImGui::TextUnformatted(field.type_name);
            break;
    }
}

} // anonymous namespace

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

    // Sync developer_mode from erhe_config each frame so toggling the
    // checkbox in the Startup Configuration section takes effect immediately.
    if (m_context.erhe_config != nullptr) {
        m_context.developer_mode = m_context.erhe_config->developer.enable || m_context.renderdoc;
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

        add_entry("Reverse Depth", [this]() {
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::Checkbox("##", &graphics_preset.reverse_depth);
        });

        add_entry("Shadows Enabled", [this]() {
            Graphics_preset_entry& graphics_preset = get_graphics_preset();
            ImGui::Checkbox("##", &graphics_preset.shadow_enable);
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

        ImGui::SameLine();
        if (ImGui::Button("Use", button_size)) {
            m_context.app_settings->graphics.current_graphics_preset = graphics_presets.at(m_graphics_preset_index);
            m_context.app_message_bus->graphics_settings.queue_message(
                Graphics_settings_message{
                    .graphics_preset = &m_context.app_settings->graphics.current_graphics_preset
                }
            );
        }
    });
    pop_group();

    add_entry("", [this, button_size](){
        if (ImGui::Button("Load", button_size)) {
            if (m_context.editor_settings != nullptr) {
                m_context.app_settings->read(*m_context.editor_settings);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save", button_size)) {
            if (m_context.editor_settings != nullptr) {
                m_context.app_settings->write(*m_context.editor_settings);
            }
        }
    });

    const bool show_developer = (m_context.erhe_config != nullptr) && m_context.erhe_config->developer.enable;

    auto add_config_section = [this, show_developer](auto& section) {
        const auto& struct_info = get_struct_info(static_cast<const std::remove_reference_t<decltype(section)>*>(nullptr));
        if (struct_info.developer && !show_developer) {
            return;
        }
        const char* label = (struct_info.short_desc != nullptr && struct_info.short_desc[0] != '\0')
            ? struct_info.short_desc
            : struct_info.name;
        push_group(label, ImGuiTreeNodeFlags_Framed);
        const auto fields = get_fields(static_cast<const std::remove_reference_t<decltype(section)>*>(nullptr));
        for (const auto& field : fields) {
            if (field.removed_in != 0) {
                continue;
            }
            if (field.developer && !show_developer) {
                continue;
            }
            const char* entry_label = (field.short_desc != nullptr && field.short_desc[0] != '\0')
                ? field.short_desc
                : field.name;
            std::string tooltip = (field.long_desc != nullptr && field.long_desc[0] != '\0')
                ? std::string{field.long_desc}
                : std::string{};
            add_entry(std::string{entry_label}, [&section, &field]() {
                imgui_field(&section, field);
            }, std::move(tooltip));
        }
        pop_group();
    };

    if (m_context.erhe_config != nullptr) {
        Erhe_config& erhe_config = *m_context.erhe_config;
        push_group("Startup Configuration (erhe.json)", ImGuiTreeNodeFlags_Framed);
        add_config_section(erhe_config.developer);
        add_config_section(erhe_config.graphics);
        add_config_section(erhe_config.mesh_memory);
        add_config_section(erhe_config.renderer);
        add_config_section(erhe_config.shader_monitor);
        add_config_section(erhe_config.text_renderer);
        add_config_section(erhe_config.threading);
        add_config_section(erhe_config.window);
        add_entry("", [this, button_size](){
            if (ImGui::Button("Save erhe.json", button_size)) {
                erhe::codegen::save_config(*m_context.erhe_config, "erhe.json");
            }
        });
        pop_group();
    }

    if (m_context.editor_settings != nullptr) {
        Editor_settings_config& settings = *m_context.editor_settings;

        push_group("Editor Settings", ImGuiTreeNodeFlags_Framed);
        add_config_section(settings.camera_controls);
        add_config_section(settings.grid);
        add_config_section(settings.headset);
        add_config_section(settings.hotbar);
        add_config_section(settings.hud);
        add_config_section(settings.inventory);
        add_config_section(settings.id_renderer);
        add_config_section(settings.network);
        add_config_section(settings.physics);
        add_config_section(settings.scene);
        add_config_section(settings.thumbnails);
        add_config_section(settings.transform_tool);
        add_config_section(settings.viewport);

        add_entry("", [this, button_size, &settings](){
            if (ImGui::Button("Save Settings", button_size)) {
                if (m_context.inventory_window != nullptr) {
                    m_context.inventory_window->write_config(settings.inventory);
                }
                erhe::codegen::save_config(settings, "editor_settings.json");
            }
        });
        pop_group();
    }

    show_entries("Settings", ImVec2{1.0f, 1.0f});
}

}
