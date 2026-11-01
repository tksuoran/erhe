#pragma once

#include "renderers/render_frame_results.hpp"

#include "erhe_scene_renderer/draw_list_object.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe::scene {
    class Mesh;
}
namespace erhe::scene_renderer {
    class Draw_list_scene;
    enum class Cull_view : uint16_t;
}

namespace editor {

class Scene_root;
class Scene_view;

// Who asked Lod_drawn_levels for a mesh set (each keeps its own request).
enum class Lod_drawn_levels_requester : unsigned int
{
    lod_window = 0,
    mcp        = 1,
    count      = 2
};

// The level of detail one color draw-list entry drew in one view of a
// rendered frame (Lod_drawn_levels).
class Lod_drawn_level
{
public:
    uint64_t                              view_key            {0}; // get_lod_view_key() of the view
    uint64_t                              mesh_id             {0}; // erhe::Item_base::get_id() of the mesh
    uint32_t                              mesh_primitive_index{0};
    erhe::scene_renderer::Drawn_lod_level drawn               {};
};

// The levels the views of one rendered frame drew for the requested meshes.
class Lod_drawn_levels_frame
{
public:
    uint64_t                     frame_index{0};
    std::vector<Lod_drawn_level> levels;
};

// The key of a scene view in Lod_drawn_level::view_key: its address, used
// only to compare (the view is never reached through it).
[[nodiscard]] auto get_lod_view_key(const Scene_view& scene_view) -> uint64_t;

// The levels of detail the draw lists drew for a requested set of meshes,
// per view, as the render phase read them after the frame's culling
// classification (doc/plans/lod_gpu_selection.md section 5,
// doc/editor/lod.md "Automatic selection"), for the main thread readers:
// the LOD window's "Drawn" column and MCP get_lod_info.
//
// - request() (main thread): the meshes of one requester, one-shot; the
//   next prepare() serves it. The LOD window requests the meshes it shows
//   each time it draws, so a closed window requests nothing.
// - prepare() (main thread, prepare phase, after the draw lists were
//   published): the pending requests' meshes resolved to their color
//   entries in the lists published for the frame
//   (Draw_list_scene::find_published_color_entry()), into the frame's
//   query slot; the requests are consumed.
// - record_view() (render phase, after the frame's classification, per
//   view): the level each query's entry draws in the view's culling view
//   (Draw_list_scene::get_published_entry_lod_level()) into the frame's
//   Render_frame_results slot.
// - find() (main thread): the result of a rendered frame, null when no view
//   recorded in it (doc/editor/render_thread.md "R writes for the main
//   thread per frame in flight").
//
// The query slots and results keep their capacity, so the steady state of
// a constant request allocates nothing.
class Lod_drawn_levels
{
public:
    explicit Lod_drawn_levels(std::size_t frames_in_flight);

    void request(
        Lod_drawn_levels_requester                        requester,
        const std::shared_ptr<Scene_root>&                scene_root,
        std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes
    );
    void prepare(uint64_t frame_index);
    void record_view(
        uint64_t                                     frame_index,
        const Scene_view&                            scene_view,
        const erhe::scene_renderer::Draw_list_scene& draw_list_scene,
        erhe::scene_renderer::Cull_view              cull_view
    );
    [[nodiscard]] auto find(std::optional<uint64_t> frame_index) const -> const Lod_drawn_levels_frame*;
    // The last frame whose prepare() served a request of requester (main
    // thread); empty before the first.
    [[nodiscard]] auto get_served_frame_index(Lod_drawn_levels_requester requester) const -> std::optional<uint64_t>;

private:
    class Request
    {
    public:
        bool                                          pending{false};
        std::weak_ptr<Scene_root>                     scene_root;
        std::vector<std::weak_ptr<erhe::scene::Mesh>> meshes;
        std::optional<uint64_t>                       served_frame_index;
    };
    class Query
    {
    public:
        const erhe::scene_renderer::Draw_list_scene* draw_list_scene{nullptr}; // compared only
        uint64_t                                     mesh_id        {0};
        erhe::scene_renderer::Draw_list_published_entry entry       {};
    };
    class Query_slot
    {
    public:
        uint64_t           frame_index{0};
        bool               prepared   {false};
        std::vector<Query> queries;
    };

    std::array<Request, static_cast<std::size_t>(Lod_drawn_levels_requester::count)> m_requests;
    std::vector<Query_slot>                                                        m_query_slots;
    Render_frame_results<Lod_drawn_levels_frame>                                   m_results;
};

} // namespace editor
