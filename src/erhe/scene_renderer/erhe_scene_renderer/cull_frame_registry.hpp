#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
}

namespace erhe::scene_renderer {

class Draw_list_scene;

// The frame's culling classification of every Draw_list_scene
// (doc/plans/culling.md phase 6a, doc/erhe/draw_list_renderer.md Q6). Every
// Draw_list_scene is created with one (Draw_list_scene_create_info::
// cull_frame_registry) and registers itself when it starts a frame's view
// set (its first add_cull_view() of the frame). classify_frame(), once per
// frame after every view of the frame was added and before the first draw,
// runs Draw_list_scene::classify_frame_cull_views() of every registered
// scene: the CPU lanes always, and the scene's GPU dispatch when its
// published slot asks for the GPU path (which needs the scene's
// Gpu_cull_classifier). The editor drives it from the rendergraph's
// pre-execute callback. Render thread only.
class Cull_frame_registry
{
public:
    Cull_frame_registry();
    ~Cull_frame_registry() noexcept;
    Cull_frame_registry(const Cull_frame_registry&)            = delete;
    Cull_frame_registry& operator=(const Cull_frame_registry&) = delete;

    // Draw_list_scene, at the start of frame_index's view set.
    void add_frame_scene(Draw_list_scene& draw_list_scene, uint64_t frame_index);
    // Render phase, outside any render pass: classifies every scene
    // registered since the previous call, recording the GPU dispatches into
    // command_buffer.
    void classify_frame(erhe::graphics::Command_buffer& command_buffer);

private:
    // Cleared (capacity kept) by classify_frame().
    std::vector<Draw_list_scene*> m_frame_scenes;
    uint64_t                      m_frame_index{std::numeric_limits<uint64_t>::max()};
};

} // namespace erhe::scene_renderer
