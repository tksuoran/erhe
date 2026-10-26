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
// pre-execute callback. The registry also ends the frame's shared draw
// records (doc/erhe/draw_list_renderer.md "Record upload"): a scene whose
// draw_shadow() / draw_color() acquired shared records in a frame registers
// itself (add_record_scene()), and release_frame_records(), once per frame
// after the frame's last draw-list pass, releases them in that frame.
// Render thread only.
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
    // Draw_list_scene, when its first shared record range of frame_index is
    // acquired.
    void add_record_scene(Draw_list_scene& draw_list_scene, uint64_t frame_index);
    // Render phase, after the frame's last draw-list pass (the editor: after
    // the rendergraph and the headset rendered, before the frame's submit):
    // releases the shared records of every scene registered since the
    // previous call (Draw_list_scene::release_frame_records()).
    void release_frame_records();
    // Draw_list_scene, when its classification of frame_index wrote
    // occlusion words (doc/plans/occlusion_culling.md) that the host reads
    // once the frame retired.
    void add_frame_end_scene(Draw_list_scene& draw_list_scene, uint64_t frame_index);
    // Render phase, after the frame's last pass and before its submit,
    // outside any render pass (the editor: with release_frame_records()):
    // records Draw_list_scene::record_frame_end() of every scene registered
    // since the previous call into command_buffer.
    void record_frame_end(erhe::graphics::Command_buffer& command_buffer);
    // Scenes registered since the last record_frame_end() call.
    [[nodiscard]] auto has_frame_end_scenes() const -> bool;

private:
    // Cleared (capacity kept) by classify_frame().
    std::vector<Draw_list_scene*> m_frame_scenes;
    uint64_t                      m_frame_index{std::numeric_limits<uint64_t>::max()};
    // Cleared (capacity kept) by release_frame_records().
    std::vector<Draw_list_scene*> m_record_scenes;
    uint64_t                      m_record_frame_index{std::numeric_limits<uint64_t>::max()};
    // Cleared (capacity kept) by record_frame_end().
    std::vector<Draw_list_scene*> m_frame_end_scenes;
    uint64_t                      m_frame_end_frame_index{std::numeric_limits<uint64_t>::max()};
};

} // namespace erhe::scene_renderer
