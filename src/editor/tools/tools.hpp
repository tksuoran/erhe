#pragma once

#include "erhe/components/components.hpp"

#include <gsl/gsl>

namespace erhe::application{
    class Imgui_windows;
}

namespace editor
{

class Render_context;
class Scene_root;
class Scene_view;
class Tool;

class Tools
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Editor_tools"};
    static constexpr std::string_view c_title{"Editor Tools"};
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

    // Public API
    void render_tools            (const Render_context& context);
    void register_tool           (Tool* tool);
    void register_background_tool(Tool* tool);
    void register_hover_tool     (Tool* tool);
    void on_hover                (Scene_view* scene_view);

    [[nodiscard]] auto get_tool_scene_root() -> std::shared_ptr<Scene_root>;

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Imgui_windows> m_imgui_windows;

    std::mutex                        m_mutex;
    std::vector<gsl::not_null<Tool*>> m_tools;
    std::vector<gsl::not_null<Tool*>> m_background_tools;
    std::vector<gsl::not_null<Tool*>> m_hover_tools;
    std::shared_ptr<Scene_root>       m_scene_root;
};

} // namespace editor
