#pragma once

#include "tools/tool.hpp"
#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Selection_tool;
class Scene_root;

class Physics_window
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Physics_window";

    Physics_window ();
    ~Physics_window() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    void render     (const Render_context& render_context) override;
    auto state      () const -> State                      override;
    auto description() -> const char*                      override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    struct Debug_draw_parameters
    {
        bool enable           {false};
        bool wireframe        {false};
        bool aabb             {true};
        bool contact_points   {true};
        bool no_deactivation  {false}; // forcibly disables deactivation when enabled
        bool constraints      {true};
        bool constraint_limits{true};
        bool normals          {false};
        bool frames           {true};

        struct Default_colors
        {
            glm::vec3 active_object               {1.0f, 1.0f, 1.0f};
            glm::vec3 deactivated_object          {0.0f, 1.0f, 0.0f};
            glm::vec3 wants_deactivation_object   {0.0f, 1.0f, 1.0f};
            glm::vec3 disabled_deactivation_object{1.0f, 0.0f, 0.0f};
            glm::vec3 disabled_simulation_object  {1.0f, 1.0f, 0.0f};
            glm::vec3 aabb                        {1.0f, 0.0f, 0.0f};
            glm::vec3 contact_point               {1.0f, 1.0f, 0.0f};
        };
        Default_colors default_colors;
    };

    auto get_debug_draw_parameters() -> Debug_draw_parameters;

private:
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Scene_root>     m_scene_root;
    Debug_draw_parameters           m_debug_draw;
};

} // namespace editor
