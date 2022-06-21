#pragma once

#include "scene/node_raytrace.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/optional.hpp"
#include "erhe/toolkit/timer.hpp"
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

class Editor_rendering;
class Node_raytrace;
class Scene_root;
class Viewport_window;
class Viewport_windows;

class Pointer_context
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Pointer_context"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Pointer_context ();
    ~Pointer_context() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void post_initialize() override;

    // Public API
    void update_viewport(Viewport_window* viewport_window);

    void update_keyboard(
        const bool                   pressed,
        const erhe::toolkit::Keycode code,
        const uint32_t               modifier_mask
    );
    void update_mouse(const erhe::toolkit::Mouse_button button, const int count);
    void update_mouse(const double x, const double y);

#if !defined(ERHE_RAYTRACE_LIBRARY_NONE)
    void raytrace(uint32_t mask);
    void raytrace();
#endif

    class Hover_entry
    {
    public:
        uint32_t                           mask         {0};
        bool                               valid        {false};
        Node_raytrace*                     raytrace_node{nullptr};
        std::shared_ptr<erhe::scene::Mesh> mesh         {};
        erhe::geometry::Geometry*          geometry     {nullptr};
        nonstd::optional<glm::vec3>        position     {};
        nonstd::optional<glm::vec3>        normal       {};
        std::size_t                        primitive    {0};
        std::size_t                        local_index  {0};
    };

    static constexpr std::size_t content_slot = 0;
    static constexpr std::size_t tool_slot    = 1;
    static constexpr std::size_t brush_slot   = 2;
    static constexpr std::size_t gui_slot     = 3;
    static constexpr std::size_t slot_count   = 4;

    static constexpr std::array<uint32_t, slot_count> slot_masks = {
        Raytrace_node_mask::content,
        Raytrace_node_mask::tool,
        Raytrace_node_mask::brush,
        Raytrace_node_mask::gui
    };

    static constexpr std::array<const char*, slot_count> slot_names = {
        "content",
        "tool",
        "brush",
        "gui"
    };

    [[nodiscard]] auto position_in_viewport_window     () const -> nonstd::optional<glm::vec2>;
    [[nodiscard]] auto position_in_world_viewport_depth(const double viewport_depth) const -> nonstd::optional<glm::dvec3>;
    [[nodiscard]] auto near_position_in_world          () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto far_position_in_world           () const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto position_in_world_distance      (const float distance) const -> nonstd::optional<glm::vec3>;
    [[nodiscard]] auto pointer_in_content_area         () const -> bool;
    [[nodiscard]] auto shift_key_down                  () const -> bool;
    [[nodiscard]] auto control_key_down                () const -> bool;
    [[nodiscard]] auto alt_key_down                    () const -> bool;
    [[nodiscard]] auto mouse_button_pressed            (const erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_button_released           (const erhe::toolkit::Mouse_button button) const -> bool;
    [[nodiscard]] auto mouse_x                         () const -> double;
    [[nodiscard]] auto mouse_y                         () const -> double;
    [[nodiscard]] auto get_hover                       (const std::size_t slot) const -> const Hover_entry&;
    [[nodiscard]] auto get_nearest_hover               () const -> const Hover_entry&;
    [[nodiscard]] auto window                          () const -> Viewport_window*;
    [[nodiscard]] auto last_window                     () const -> Viewport_window*;
    [[nodiscard]] auto frame_number                    () const -> uint64_t;

private:
    class Mouse_button
    {
    public:
        bool pressed {false};
        bool released{false};
    };

    // Component dependencies
    std::shared_ptr<Editor_rendering>  m_editor_rendering;
    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Viewport_windows>  m_viewport_windows;

    erhe::toolkit::Timer               m_ray_scene_build_timer;
    erhe::toolkit::Timer               m_ray_traverse_timer;

    nonstd::optional<glm::vec2>        m_position_in_window;
    nonstd::optional<glm::vec3>        m_near_position_in_world;
    nonstd::optional<glm::vec3>        m_far_position_in_world;
    bool                               m_shift               {false};
    bool                               m_control             {false};
    bool                               m_alt                 {false};

    Mouse_button                       m_mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)];
    double                             m_mouse_x             {0.0f};
    double                             m_mouse_y             {0.0f};
    Viewport_window*                   m_window              {nullptr};
    Viewport_window*                   m_update_window       {nullptr};
    Viewport_window*                   m_last_window         {nullptr};
    uint64_t                           m_frame_number         {0};

    std::array<Hover_entry, slot_count> m_hover_entries;
    std::size_t                         m_nearest_slot{0};
};

} // namespace editor
