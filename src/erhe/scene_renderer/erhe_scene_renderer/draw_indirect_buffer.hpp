#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>
#include <span>
#include <vector>

namespace erhe { class Item_filter; }
namespace erhe::scene {
    class Mesh;
    class Mesh_primitive_ref;
}

namespace erhe::scene_renderer {

class Draw_list;
class Render_bucket;

class Draw_indirect_buffer_range
{
public:
    erhe::graphics::Ring_buffer_range range;
    std::size_t                       draw_indirect_count{0};
};

class Draw_indirect_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Draw_indirect_buffer(erhe::graphics::Device& graphics_device, int max_draw_count = 8000);

    // Can discard return value
    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        erhe::primitive::Primitive_mode                            primitive_mode,
        const erhe::Item_filter&                                   filter
    ) -> Draw_indirect_buffer_range;

    // Primitive-level overload used by callers that bucket at primitive
    // granularity. Each ref produces exactly one draw command (no inner
    // loop over the mesh's primitives).
    auto update(
        const Render_bucket&            bucket,
        erhe::primitive::Primitive_mode primitive_mode
    ) -> Draw_indirect_buffer_range;

    // Prepare-phase half of the bucket path (render_bucket_snapshot.hpp):
    // appends the draw commands update(bucket, primitive_mode) would write,
    // without the debug index count clamp (applied by the span overload).
    // Reads the bucket's Buffer_mesh variants; main thread.
    void append_commands(
        const Render_bucket&                                                  bucket,
        erhe::primitive::Primitive_mode                                       primitive_mode,
        std::vector<erhe::graphics::Draw_indexed_primitives_indirect_command>& out
    ) const;

    // Render-phase half: copies prepared draw commands into a ring buffer
    // range, applying the debug index count clamp. Reads no Mesh.
    auto update(std::span<const erhe::graphics::Draw_indexed_primitives_indirect_command> commands) -> Draw_indirect_buffer_range;

    // Draw-list overload: one draw command per entry in [begin, end) of
    // draw_list whose entry_passes value (indexed like draw_list.entries) is
    // non-zero, in entry order - the exact counterpart of
    // Primitive_buffer::update(Draw_list, ...) so ERHE_DRAW_ID indexes line
    // up. Uses the index_count / first_index / base_vertex baked into the
    // entries at registration; touches no Mesh.
    auto update(
        const Draw_list&               draw_list,
        std::size_t                    begin,
        std::size_t                    end,
        std::span<const std::uint8_t>  entry_passes
    ) -> Draw_indirect_buffer_range;

    //// void debug_properties_window();

private:
    bool m_max_index_count_enable{false};
    int  m_max_index_count       {256};
    int  m_max_draw_count        {8000};
};

} // namespace erhe::scene_renderer
