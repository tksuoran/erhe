#pragma once

#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/view.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string_view>

namespace erhe::geometry
{
    class Geometry;
}

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

enum class Action : unsigned int
{
    select = 0,
    translate,
    rotate,
    add,
    remove,
    drag,
    count
};

static constexpr std::array<std::string_view, 6> c_action_strings =
{
    "Select",
    "Translate",
    "Rotate",
    "Add",
    "Remove",
    "Drag"
};

class Pointer_context;
class Scene_manager;
class Line_renderer;
class Text_renderer;

class Pointer_context
{
public:
    auto position_in_world      () const -> glm::vec3;
    auto position_in_world      (const float z) const -> glm::vec3;
    auto position_in_world      (const glm::vec3 position_in_window) const -> glm::vec3;
    auto position_in_window     (const glm::vec3 position_in_world) const -> glm::vec3;
    auto pointer_in_content_area() const -> bool
    {
        return
            (pointer_x >= 0) &&
            (pointer_y >= 0) &&
            (pointer_x < viewport.width) &&
            (pointer_y < viewport.height);
    }

    int   pointer_x       {0};
    int   pointer_y       {0};
    float pointer_z       {0.0f};
    bool  shift           {false};
    bool  control         {false};
    bool  alt             {false};
    bool  scene_view_focus{false};

    class Mouse_button
    {
    public:
        bool pressed {false};
        bool released{false};
    };

    Action                             priority_action{Action::select};
    Mouse_button                       mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)];
    bool                               mouse_moved     {false};
    double                             mouse_x         {0.0f};
    double                             mouse_y         {0.0f};
    const erhe::scene::ICamera*        camera          {nullptr};
    erhe::scene::Viewport              viewport        {0, 0, 0, 0, true};
    bool                               hover_valid     {false};
    std::shared_ptr<erhe::scene::Mesh> hover_mesh;
    std::optional<glm::vec3>           hover_normal;
    erhe::scene::Layer*                hover_layer      {nullptr};
    size_t                             hover_primitive  {0};
    size_t                             hover_local_index{0};
    bool                               hover_tool       {false};
    bool                               hover_content    {false};
    erhe::geometry::Geometry*          geometry{nullptr};
    uint64_t                           frame_number{0};
};

} // namespace editor
