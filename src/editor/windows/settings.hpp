#pragma once

#include "editor_message.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <string_view>

namespace erhe::application
{
    class Configuration;
}

namespace editor
{

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

class Settings_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
    , public erhe::message_bus::Message_bus_node<Editor_message>
{
public:
    static constexpr std::string_view c_type_name{"Settings_window"};
    static constexpr std::string_view c_title{"Settings"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Settings_window();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

private:
    void read_ini     ();
    void write_ini    ();
    void apply_limits (Settings& settings);
    void show_settings(Settings& settings);
    void use_settings (const Settings& settings);

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
};

} // namespace editor
