#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::scene {
    class Camera;
    class Xformable; using Node = Xformable;
}

namespace editor {

class Scene_root;

// A viewport's overdraw visualization (doc/plans/overdraw_visualization.md):
// not off, the view's content passes count per pixel (Overdraw_view_state)
// and the view shows and reports the counts. shaded counts the fragment
// shader invocations the frame runs (after the early depth test);
// rasterized counts every rasterized fragment (ignoring the depth test).
// The values are erhe::scene_renderer::Overdraw_count's.
enum class Overdraw_mode : unsigned int
{
    off        = 0,
    shaded     = 1,
    rasterized = 2
};
static_assert(static_cast<unsigned int>(Overdraw_mode::shaded)     == static_cast<unsigned int>(erhe::scene_renderer::Overdraw_count::shaded));
static_assert(static_cast<unsigned int>(Overdraw_mode::rasterized) == static_cast<unsigned int>(erhe::scene_renderer::Overdraw_count::rasterized));
inline constexpr const char* c_overdraw_mode_strings[] = { "Off", "Shaded", "Rasterized" };
[[nodiscard]] inline auto to_overdraw_count(const Overdraw_mode mode) -> erhe::scene_renderer::Overdraw_count
{
    return static_cast<erhe::scene_renderer::Overdraw_count>(static_cast<unsigned int>(mode));
}

// The grid composition pass inputs of one view: whether the grid draws and
// its camera UBO parameters (grid.frag). Written by Grid::render() when the
// grid tool records the view.
class Grid_pass_state
{
public:
    bool                                  enabled   {false};
    erhe::scene_renderer::Grid_parameters parameters{};
};

// What the render phase reads of one Viewport_scene_view for one frame, one
// copy per frame in flight (Viewport_scene_view::prepare_render() fills the
// copy of the frame it prepares, the render phase reads the copy of the
// frame it renders; doc/editor/editor.md "Frame phases and the render
// thread").
class Viewport_render_state
{
public:
    // The frame this copy was filled for.
    uint64_t                             frame_index          {std::numeric_limits<uint64_t>::max()};
    bool                                 do_render            {false};
    // The view's rendergraph node was enabled when prepare_render() ran.
    // The frame's composition is prepared, and its render phase renders,
    // only then, even when the node is enabled later in the frame (the
    // Viewport window sets the flag while it draws).
    bool                                 node_enabled         {false};
    // The scene root and camera the frame renders (strong references).
    std::shared_ptr<Scene_root>          scene_root           {};
    std::shared_ptr<erhe::scene::Camera> camera               {};
    // The content render target's size and MSAA sample count for the
    // frame; the render phase applies them to the render target.
    erhe::math::Viewport                 projection_viewport  {0, 0, 0, 0};
    int                                  msaa_sample_count    {0};
    erhe::scene_renderer::Shader_debug   shader_debug         {erhe::scene_renderer::Shader_debug::none};
    Overdraw_mode                        overdraw             {Overdraw_mode::off};
    uint32_t                             overdraw_max_count   {8};
    // The hover flag and pointer position (the ID pass), and whether the
    // pick probe fires this frame.
    bool                                 is_scene_view_hovered{false};
    std::optional<glm::vec2>             position_in_viewport {};
    bool                                 pick_probe           {false};
    // The debug / text renderer frame slot the tools recorded this view's
    // lines and labels into; none when the view recorded nothing.
    std::optional<std::size_t>           tools_frame_slot     {};
    // The view's grid, recorded by the grid tool.
    Grid_pass_state                      grid                 {};
    // Weight_display's joint weight ramp inputs (App_rendering).
    glm::uvec4                           debug_joint_indices  {0xffffffffu, 0, 0, 0};
    std::shared_ptr<erhe::scene::Node>   debug_target_joint   {};
    std::vector<glm::vec4>               debug_joint_colors   {};
};

}
