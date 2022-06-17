#pragma once

#include "tools/tool.hpp"

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/physics/idebug_draw.hpp"

#include <memory>

namespace editor
{

class Selection_tool;
class Scene_root;

class Physics_window
    : public erhe::components::Component
    , public Tool
//    , public Rendertarget_imgui_window // Imgui_window
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Physics_window"};
    static constexpr std::string_view c_title{"Physics"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Physics_window ();
    ~Physics_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Implements Window
    void imgui() override;

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

    [[nodiscard]]
    auto get_debug_draw_parameters() -> Debug_draw_parameters;

private:
    // Component dependencies
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Scene_root>     m_scene_root;

    Debug_draw_parameters m_debug_draw;
};

} // namespace editor
