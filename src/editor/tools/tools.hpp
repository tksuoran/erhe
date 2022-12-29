#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <gsl/gsl>

namespace erhe::application {
    class Commands;
    class Configuration;
    class Imgui_windows;
}

namespace editor
{

class Editor_message_bus;
class Render_context;
class Scene_root;
class Scene_view;
class Tool;

class Tools
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Editor_tools"};
    static constexpr std::string_view c_title    {"Editor Tools"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Tools ();
    ~Tools() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void render_tools     (const Render_context& context);
    void register_tool    (Tool* tool);
    void set_priority_tool(Tool* tool);
    [[nodiscard]] auto get_priority_tool  () const -> Tool*;
    [[nodiscard]] auto get_tools          () const -> const std::vector<Tool*>&;
    [[nodiscard]] auto get_tool_scene_root() -> std::shared_ptr<Scene_root>;

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Commands     > m_commands;
    std::shared_ptr<erhe::application::Configuration> m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows> m_imgui_windows;
    std::shared_ptr<Editor_message_bus>               m_editor_message_bus;

    Tool*                       m_priority_tool{nullptr};

    std::mutex                  m_mutex;
    std::vector<Tool*>          m_tools;
    std::vector<Tool*>          m_background_tools;
    std::shared_ptr<Scene_root> m_scene_root;
};

} // namespace editor
