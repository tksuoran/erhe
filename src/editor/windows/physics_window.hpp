#pragma once

#include "tools/tool.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/physics/idebug_draw.hpp"

#include <memory>

namespace editor
{

class Physics_window
    : public erhe::components::Component
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Physics_window"};
    static constexpr std::string_view c_title{"Physics"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Physics_window ();
    ~Physics_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

    class Debug_draw_parameters
    {
    public:
        bool enable           {false};
        bool wireframe        {false};
        bool aabb             {true};
        bool contact_points   {true};
        bool no_deactivation  {false}; // forcibly disables deactivation when enabled
        bool constraints      {true};
        bool constraint_limits{true};
        bool normals          {false};
        bool frames           {true};

        erhe::physics::IDebug_draw::Colors colors;
    };

    [[nodiscard]] auto get_debug_draw_parameters() -> Debug_draw_parameters;

private:
    Debug_draw_parameters m_debug_draw;
};

extern Physics_window* g_physics_window;

} // namespace editor
