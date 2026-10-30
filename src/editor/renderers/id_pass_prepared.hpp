#pragma once

#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/render_bucket_snapshot.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace erhe::scene_renderer {
    class Material_set;
}

namespace editor {

// A region selection scan (box / paint select) of a viewport-pixel
// rectangle of the id buffer, optionally masked to a brush disk
// (Id_renderer::Scan_request).
class Id_scan_request
{
public:
    int       x           {0};      // viewport-pixel bounding rect (clamped to the viewport in Id_renderer::render())
    int       y           {0};
    int       width       {0};
    int       height      {0};
    glm::vec2 brush_center {0.0f, 0.0f};
    float     brush_radius {0.0f};  // > 0 with is_brush => disk mask over the rect, else whole rect
    bool      is_brush     {false};
};

// The id pass of one scene view and one frame, prepared in the prepare phase
// (Id_renderer::prepare, the last M2 step) and drawn by Id_renderer::render
// in the render phase without reading any Mesh: the bucketed records (the id
// offset in each record's color) and draw commands, and the id range table
// that turns a read-back id into (mesh, primitive, facet). Kept per frame in
// flight in the view's Composer_frame_slots slot (Composer_prepared::id_pass).
class Id_pass_prepared
{
public:
    // Whether the prepare phase prepared the pass for this frame (the view
    // renders the id pass: hovered, a region scan pending or a pick probe).
    bool                                                          prepared{false};
    erhe::scene_renderer::Render_bucket_snapshot                  buckets;
    // Deterministic per gather: the offsets start at
    // Primitive_buffer::first_id_offset and follow the record order.
    std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> id_ranges;
    // The scene's material set the records' material slots index; the
    // render phase binds it for the alpha-tested buckets' discard (its copy
    // of this frame, Material_set::bind()).
    erhe::scene_renderer::Material_set*                           material_set{nullptr};
    // The frame's region scan, when this view claimed it in the prepare
    // phase (Id_renderer::take_frame_scan): the render phase rasterizes and
    // reads back its rectangle along with the pointer rect.
    std::optional<Id_scan_request>                                scan;
};

} // namespace editor
