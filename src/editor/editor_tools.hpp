#pragma once

#include "erhe/components/components.hpp"

#include <gsl/gsl>

namespace editor {

class Editor_imgui_windows;
class Editor_view;
class Editor_time;
class Editor_tools;
class Imgui_window;
class Render_context;
class Tool;

class Editor_tools
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name       {"Editor_tools"};
    static constexpr std::string_view c_description{"Editor Tools"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Editor_tools ();
    ~Editor_tools() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect() override;

    // Public API
    void render_tools            (const Render_context& context);
    void register_tool           (Tool* tool);
    void register_background_tool(Tool* tool);

private:
    // Component dependencies
    std::shared_ptr<Editor_imgui_windows> m_editor_imgui_windows;

    std::mutex                        m_mutex;
    std::vector<gsl::not_null<Tool*>> m_tools;
    std::vector<gsl::not_null<Tool*>> m_background_tools;
};

}
