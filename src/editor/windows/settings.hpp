#pragma once

#include "erhe/imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene_renderer {
    class Shadow_renderer;
}

namespace editor
{

class Editor_context;
class Editor_message_bus;

class Settings
{
public:
    std::string name;
    int         msaa_sample_count {4};
    bool        shadow_enable     {true};
    int         shadow_resolution {1024};
    int         shadow_light_count{4};
    bool        bindless_textures {false};
};

// TODO: Rename to Graphics_settings
class Settings_window
    : public erhe::imgui::Imgui_window
{
public:
    Settings_window(
        erhe::imgui::Imgui_renderer&           imgui_renderer,
        erhe::imgui::Imgui_windows&            imgui_windows,
        erhe::scene_renderer::Shadow_renderer& shadow_renderer,
        Editor_context&                        editor_context,
        Editor_message_bus&                    editor_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

    [[nodiscard]] auto get_msaa_sample_count() -> int;

private:
    void read_ini     ();
    void write_ini    ();
    void apply_limits (Settings& settings);
    void show_settings(Settings& settings);
    void use_settings (
        erhe::scene_renderer::Shadow_renderer& shadow_renderer,
        const Settings&                        settings
    );

    Editor_context& m_context;

    std::vector<const char*> m_msaa_sample_count_entry_s_strings;
    std::vector<int        > m_msaa_sample_count_entry_values;
    std::vector<std::string> m_msaa_sample_count_entry_strings;
    int                      m_max_shadow_resolution{1};
    int                      m_max_depth_layers{1};

    int                      m_msaa_sample_count_entry_index{0};
    std::vector<const char*> m_settings_names;
    std::vector<Settings>    m_settings;
    int                      m_settings_index{0};
    std::string              m_used_settings;

    int                      m_msaa_sample_count{0};
};

} // namespace editor
