#pragma once

#include "erhe/components/components.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/optional.hpp"
#include "erhe/toolkit/view.hpp"

#include <glm/glm.hpp>

#include <array>
#include <memory>
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

//enum class Action : unsigned int
//{
//    drag = 0,
//    add,
//    remove,
//    count
//};
//
//static constexpr std::array<std::string_view, 3> c_action_strings =
//{
//    "Drag",
//    "Add",
//    "Remove"
//};

class Editor_rendering;
class Log_window;
class Node_raytrace;
class Scene_root;
class Viewport_window;
class Viewport_windows;

class Pointer_context
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Pointer_context"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Pointer_context ();
    ~Pointer_context() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect() override;

    // Public API
    void update_viewport(Viewport_window* viewport_window);

    void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    );
    void update_mouse(const erhe::toolkit::Mouse_button button, const int count);
    void update_mouse(const double x, const double y);

    void raytrace();

    [[nodiscard]] auto position_in_viewport_window() const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto position_in_world          () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto position_in_world          (const double viewport_depth) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto near_position_in_world     () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto far_position_in_world      () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto raytrace_node              () const -> Node_raytrace*;
    [[nodiscard]] auto raytrace_hit_position      () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto pointer_in_content_area    () const -> bool;
    [[nodiscard]] auto shift_key_down             () const -> bool;
    [[nodiscard]] auto control_key_down           () const -> bool;
    [[nodiscard]] auto alt_key_down               () const -> bool;
    [[nodiscard]] auto mouse_button_pressed       (const erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_button_released      (const erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_x                    () const -> double;
    [[nodiscard]] auto mouse_y                    () const -> double;
    [[nodiscard]] auto hovering_over_tool         () const -> bool;
    [[nodiscard]] auto hovering_over_content      () const -> bool;
    [[nodiscard]] auto hovering_over_gui          () const -> bool;
    [[nodiscard]] auto hover_normal               () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto hover_mesh                 () const -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto hover_primitive            () const -> size_t;
    [[nodiscard]] auto hover_local_index          () const -> size_t;
    [[nodiscard]] auto hover_geometry             () const -> erhe::geometry::Geometry*;
    [[nodiscard]] auto window                     () const -> Viewport_window*;
    [[nodiscard]] auto last_window                () const -> Viewport_window*;
    [[nodiscard]] auto frame_number               () const -> uint64_t;

private:
    class Mouse_button
    {
    public:
        bool pressed {false};
        bool released{false};
    };

    // Component dependencies
    std::shared_ptr<Editor_rendering>  m_editor_rendering;
    std::shared_ptr<Log_window>        m_log_window;
    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Viewport_windows>  m_viewport_windows;

    nonstd::optional<glm::vec3>        m_position_in_window;
    nonstd::optional<glm::vec3>        m_position_in_world;
    nonstd::optional<glm::vec3>        m_near_position_in_world;
    nonstd::optional<glm::vec3>        m_far_position_in_world;
    bool                               m_shift               {false};
    bool                               m_control             {false};
    bool                               m_alt                 {false};

    Mouse_button                       m_mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)];
    //bool                               m_mouse_moved         {false};
    double                             m_mouse_x             {0.0f};
    double                             m_mouse_y             {0.0f};
    Viewport_window*                   m_window              {nullptr};
    Viewport_window*                   m_update_window       {nullptr};
    Viewport_window*                   m_last_window         {nullptr};

    bool                               m_hover_valid         {false};
    std::shared_ptr<erhe::scene::Mesh> m_hover_mesh;
    nonstd::optional<glm::vec3>        m_hover_normal;
    const erhe::scene::Mesh_layer*     m_hover_layer          {nullptr};
    size_t                             m_hover_primitive      {0};
    size_t                             m_hover_local_index    {0};
    bool                               m_hover_tool           {false};
    bool                               m_hover_content        {false};
    bool                               m_hover_gui            {false};
    erhe::geometry::Geometry*          m_hover_geometry       {nullptr};

    uint64_t                           m_frame_number         {0};

    nonstd::optional<glm::vec3>        m_raytrace_hit_position;
    glm::vec3                          m_raytrace_hit_normal  {0.0f};
    Node_raytrace*                     m_raytrace_node        {nullptr};
    //size_t                             m_raytrace_primitive   {0};
    //size_t                             m_raytrace_local_index {0};
};

} // namespace editor
