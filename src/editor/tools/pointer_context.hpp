#pragma once

#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/view.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::scene
{
    class ICamera;
    class Camera;
    class Layer;
    class Mesh;
}

namespace editor
{

class Tool;

struct Pointer_context
{
    auto position_in_world() const -> glm::vec3;

    auto position_in_world(float z) const-> glm::vec3;

    auto position_in_world(glm::vec3 position_in_window) const-> glm::vec3;

    auto position_in_window(glm::vec3 position_in_world) const -> glm::vec3;

    auto pointer_in_content_area() const -> bool
    {
        return (pointer_x >= 0) &&
               (pointer_y >= 0) &&
               (pointer_x < viewport.width) &&
               (pointer_y < viewport.height);
    }

    int                   pointer_x       {0};
    int                   pointer_y       {0};
    float                 pointer_z       {0.0f};
    bool                  shift           {false};
    bool                  control         {false};
    bool                  alt             {false};
    bool                  scene_view_focus{false};
    struct Mouse_button
    {
        bool pressed {false};
        bool released{false};
    };
    Mouse_button          mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)];
    bool                  mouse_moved     {false};
    double                mouse_x         {0.0f};
    double                mouse_y         {0.0f};
    erhe::scene::ICamera* camera          {nullptr};
    erhe::scene::Viewport viewport        {0, 0, 0, 0};
    bool                               hover_valid     {false};
    std::shared_ptr<erhe::scene::Mesh> hover_mesh;
    std::optional<glm::vec3>           hover_normal;
    erhe::scene::Layer*                hover_layer      {nullptr};
    size_t                             hover_primitive  {0};
    size_t                             hover_local_index{0};
    bool                               hover_tool       {false};
    bool                               hover_content    {false};
};

} // namespace editor
