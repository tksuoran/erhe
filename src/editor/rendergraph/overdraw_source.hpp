#pragma once

#include <cstdint>

namespace erhe::graphics {
    class Buffer;
}

namespace editor {

enum class Overdraw_mode : unsigned int; // scene/viewport_render_state.hpp

// How one frame of a view shows its counts.
class Overdraw_frame
{
public:
    Overdraw_mode mode{}; // off
    uint32_t      max_count{8};
};

// A view whose counts an Overdraw_node shows (Viewport_scene_view,
// Scene_image_view). Render phase, after the view executed in the frame.
class IOverdraw_source
{
public:
    virtual ~IOverdraw_source() noexcept = default;

    // Null when the view did not count in frame_index.
    [[nodiscard]] virtual auto get_overdraw_count_buffer(uint64_t frame_index) const -> const erhe::graphics::Buffer* = 0;
    [[nodiscard]] virtual auto get_overdraw_frame       (uint64_t frame_index) const -> Overdraw_frame = 0;
};

} // namespace editor
