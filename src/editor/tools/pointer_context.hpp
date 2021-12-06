#pragma once

#include "erhe/components/component.hpp"
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
    class Mesh;
    class Mesh_layer;
}

namespace editor
{

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

class Editor_rendering;
class Frame_log_window;
class Viewport_window;
class Viewport_windows;

class Pointer_context
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Pointer_context"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Pointer_context();
    ~Pointer_context() override;

    // Implements Component
    auto get_type_hash() const -> uint32_t override { return hash; }
    void connect      () override;

    void begin_frame();

    void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    );
    void update_mouse(
        const erhe::toolkit::Mouse_button button,
        const int                         count
    );
    void update_mouse(
        const double x,
        const double y
    );

    void update                     (Viewport_window* viewport_window);

    auto position_in_viewport_window() const -> std::optional<glm::vec3>;
    auto position_in_world          () const -> std::optional<glm::vec3>;
    auto position_in_world          (const float viewport_depth) const -> std::optional<glm::vec3>;
    auto near_position_in_world     () const -> std::optional<glm::vec3>;
    auto far_position_in_world      () const -> std::optional<glm::vec3>;
    auto pointer_in_content_area    () const -> bool;
    auto shift_key_down             () const -> bool;
    auto control_key_down           () const -> bool;
    auto alt_key_down               () const -> bool;
    auto mouse_button_pressed       (const erhe::toolkit::Mouse_button button) const -> bool;
    auto mouse_button_released      (const erhe::toolkit::Mouse_button button) const -> bool;
    auto mouse_moved                () const -> bool;
    auto mouse_x                    () const -> double;
    auto mouse_y                    () const -> double;
    auto hovering_over_tool         () const -> bool;
    auto hovering_over_content      () const -> bool;
    auto hover_normal               () const -> std::optional<glm::vec3>;
    auto hover_mesh                 () const -> std::shared_ptr<erhe::scene::Mesh>;
    auto hover_primitive            () const -> size_t;
    auto hover_local_index          () const -> size_t;
    auto hover_geometry             () const -> erhe::geometry::Geometry*;

    auto window             () const -> Viewport_window*;

    void set_priority_action(const Action action);
    auto priority_action    () const -> Action;
    auto frame_number       () const -> uint64_t;

private:
    class Mouse_button
    {
    public:
        bool pressed {false};
        bool released{false};
    };

    Editor_rendering*                  m_editor_rendering{nullptr};
    Frame_log_window*                  m_frame_log_window{nullptr};
    Viewport_windows*                  m_viewport_windows{nullptr};

    std::optional<glm::vec3>           m_position_in_window;
    std::optional<glm::vec3>           m_position_in_world;
    std::optional<glm::vec3>           m_near_position_in_world;
    std::optional<glm::vec3>           m_far_position_in_world;
    bool                               m_shift               {false};
    bool                               m_control             {false};
    bool                               m_alt                 {false};

    Mouse_button                       m_mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)];
    bool                               m_mouse_moved         {false};
    double                             m_mouse_x             {0.0f};
    double                             m_mouse_y             {0.0f};
    Viewport_window*                   m_window              {nullptr};

    bool                               m_hover_valid         {false};
    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh;
    std::optional<glm::vec3>           m_hover_normal;
    const erhe::scene::Mesh_layer*     m_hover_layer          {nullptr};
    size_t                             m_hover_primitive      {0};
    size_t                             m_hover_local_index    {0};
    bool                               m_hover_tool           {false};
    bool                               m_hover_content        {false};
    erhe::geometry::Geometry*          m_hover_geometry       {nullptr};

    Action                             m_priority_action      {Action::select};
    uint64_t                           m_frame_number         {0};

    glm::vec3                          m_raytrace_hit_position{0.0f};
    glm::vec3                          m_raytrace_hit_normal  {0.0f};
    size_t                             m_raytrace_primitive   {0};
    size_t                             m_raytrace_local_index {0};
};

} // namespace editor
