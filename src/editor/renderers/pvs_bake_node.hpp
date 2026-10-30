#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/pvs_disocclusion.hpp"
#include "renderers/pvs_baker.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Base_render_pipeline;
    class Buffer;
    class Command_buffer;
    class Gpu_timer;
    class Render_pass;
    class Texture;
}
namespace erhe::scene {
    class Pvs_database;
}
namespace erhe::scene_renderer {
    class Pvs_disocclusion_buffers;
    class Pvs_disocclusion_pipelines;
}

namespace editor {

class App_context;
class Scene_root;

// Whether a bake counts the depth pass's fragments per entry for its trace
// (doc/plans/pvs.md "Bake" 7): constant for the bake (the layered buffer's
// header); only the traced cell reads the counters.
enum class Pvs_trace_mode : unsigned int {
    off = 0,
    on
};

// One face pass of a cell of a potentially visible set bake
// (doc/plans/pvs.md "Bake" 3 to 5): the face camera (a perspective view
// from the sample point along the face axis, its near plane at z_near and
// its far plane at z_far) and the geometry of the disocclusion passes
// (Pvs_face_geometry; the render phase completes it with the frame's entry
// word count).
class Pvs_face_view
{
public:
    erhe::scene::Camera_snapshot                camera;
    float                                       z_near {0.0f};
    float                                       z_far  {0.0f};
    uint32_t                                    face   {0};
    uint32_t                                    sample {0};
    erhe::scene_renderer::Pvs_face_geometry     geometry{};
};

// One cell of a potentially visible set bake, as Pvs_baker hands it to its
// Pvs_bake_node: every face pass of the cell (six faces per sample per
// face) drawn and run through the disocclusion passes in one frame.
class Pvs_cell_job
{
public:
    std::shared_ptr<Scene_root>      scene_root;
    // The bake's column table, set as the scene's database for the bake:
    // the cell is drawn only in a frame whose published draw lists carry
    // its columns (the static set, Draw_list::pvs_columns).
    const erhe::scene::Pvs_database* columns{nullptr};
    uint32_t                         cell   {0};
    std::vector<Pvs_face_view>       faces;
    // pvs_debug_face: one face whose layer's depth and id_min words are
    // copied to the host after its raster passes, and no disocclusion
    // passes; none for a bake cell.
    std::optional<uint32_t>          readback_layer{};
    // The bake trace (doc/plans/pvs.md "Bake" 7): the columns to trace;
    // empty for an untraced cell.
    std::vector<uint32_t>            trace_columns;
    // The bake trace's depth dump: the face (0 .. 5, sample 0) whose whole
    // depth region is copied to the host for the CPU diagnostics.
    std::optional<uint32_t>          trace_depth_face{};
};

// One face of a traced cell (Pvs_cell_record::traces): a host buffer of,
// back to back, the trace counters (4 per traced column: samples, in the
// core, of those disoccluded, 0), the depth pass's fragment counters per
// frame entry (entry_count), the cell's column words after the face
// (column_word_count), the closed flags (tile_count), the tile masks (2 *
// tile_count) and the column masks (2 * tiles^2).
class Pvs_face_trace
{
public:
    uint32_t                                face            {0};
    uint32_t                                sample          {0};
    uint32_t                                entry_count     {0};
    uint32_t                                column_word_count{0};
    uint32_t                                tile_count      {0};
    uint32_t                                tiles           {0};
    std::size_t                             word_count      {0};
    std::unique_ptr<erhe::graphics::Buffer> readback;
    std::unique_ptr<erhe::graphics::Buffer> trace_buffer;
};

// What the render phase recorded for a cell (Pvs_bake_node::take_completed()).
class Pvs_cell_record
{
public:
    uint64_t                                frame        {0};
    uint32_t                                cell         {0};
    // The first face's pass (pvs_debug_face reports it).
    uint32_t                                face         {0};
    float                                   z_near       {0.0f};
    float                                   z_far        {0.0f};
    // Entries drawn by the depth passes and by the id passes (all faces).
    std::size_t                             depth_entries{0};
    std::size_t                             id_entries   {0};
    // readback_layer: the layer's depth words, then its id_min words
    // (face_resolution^2 each, the layered buffer's row order).
    std::optional<uint32_t>                 readback_layer{};
    std::unique_ptr<erhe::graphics::Buffer> readback;
    // A bake cell: the cell's visible columns (bit c = column c,
    // column_word_count words), the bounds vote over its faces; a traced
    // cell also the column of every frame-local entry
    // (Draw_list_scene::get_published_pvs_entry_columns()).
    std::unique_ptr<erhe::graphics::Buffer> column_words;
    uint32_t                                column_word_count{0};
    std::vector<uint32_t>                   entry_columns;
    // A traced cell: per face pass.
    std::vector<Pvs_face_trace>             traces;
    // The trace's depth dump (Pvs_cell_job::trace_depth_face): the face's
    // depth region, face_resolution^2 * layer_count words.
    std::optional<uint32_t>                 trace_depth_face{};
    std::unique_ptr<erhe::graphics::Buffer> trace_depth;
};

// The rendergraph node of a potentially visible set bake (Pvs_baker,
// doc/plans/pvs.md D6): one cell per frame. It owns everything its execute
// reads and writes: the layered buffer (Pvs_layered_interface: its
// constant header, then the dense depth, id_min and id_max words of one
// face, face_resolution^2 * layer_count words each, device-local; reused by
// every face of the cell), the disocclusion passes' pipelines and work
// buffers, the columns' world bounds and the cell's column words, the
// depth-only render pass of the face size, the pipeline, the record and
// draw command buffers, the GPU timers, and per frame in flight the cell
// job the prepare phase handed to it. A node with no links.
//
// Every frame in flight shares the layered buffer and the work buffers, so
// nothing per face is written into them from the CPU at record time
// (upload_to_buffer() is a host copy on Metal, ordered with no GPU work):
// the header and the column bounds are constant for the bake and written
// once in the node's first frame; the face's depth range is its camera
// block's (per pass, from the ring buffer) and the disocclusion parameters
// come from ring buffer ranges; the clears are GPU fills.
//
// The passes have no color attachment and test no depth. Metal (both
// backends) begins no render pass without an attachment (its render pass
// descriptor would need renderTargetWidth / Height, which erhe::graphics
// does not set), so the pass carries a depth attachment of the face size
// the pipeline neither tests nor writes, on every backend.
//
// Prepare phase (prepare_rendergraph_node): the job submit() handed over
// becomes the frame's slot once the scene's Draw_list_scene holds the job's
// column table (the frame publishes the columns); with no job the node
// disables itself. Render phase: the pre-execute hook registers every
// face's frustum as a culling view; the execute clears the cell's column
// words, and per face clears the layered buffer, draws the depth pass (and
// the id pass for a trace or pvs_debug_face; Draw_list_scene::
// draw_pvs_face()) and runs the disocclusion passes
// (Pvs_disocclusion_pipelines::record_face()), then copies the column
// words to the host; a readback job instead copies its layer to the host
// after the raster passes. The records are shared with the main thread
// under m_record_mutex.
class Pvs_bake_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Pvs_bake_node(
        erhe::rendergraph::Rendergraph& rendergraph,
        App_context&                    context,
        uint32_t                        face_resolution,
        uint32_t                        tile_size,
        uint32_t                        layer_count,
        Pvs_trace_mode                  trace_mode,
        std::span<const glm::vec4>      column_bounds
    );
    ~Pvs_bake_node() noexcept override;

    auto get_type_name           () const -> std::string_view override { return "Pvs_bake_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // The disocclusion passes' pipelines built.
    [[nodiscard]] auto is_disocclusion_supported() const -> bool;
    // Main thread (M2), before the rendergraph prepares the frame: the next
    // cell. Verifies the previous one was taken by a prepare phase.
    void submit(Pvs_cell_job&& job);
    // Main thread: true while a submitted cell waits for its frame.
    [[nodiscard]] auto has_pending_job() const -> bool;
    // Main thread: drops the submitted cell no prepare phase took (a bake
    // that stopped).
    void cancel_pending_job();
    // Main thread: the recorded cells whose frames completed, oldest first.
    void take_completed(std::vector<Pvs_cell_record>& out);
    // Main thread: cells recorded into frames that have not completed (no
    // GPU work of the node may still run when this is false and no job is
    // pending).
    [[nodiscard]] auto has_frames_in_flight() const -> bool;
    // Main thread: the GPU timers' latest results.
    [[nodiscard]] auto get_gpu_times() const -> Pvs_bake_gpu_times;

    [[nodiscard]] auto get_face_resolution() const -> uint32_t { return m_face_resolution; }
    [[nodiscard]] auto get_layer_count    () const -> uint32_t { return m_layer_count; }

protected:
    void prepare_rendergraph_node    (uint64_t frame_index) override;
    void pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

private:
    // A traced cell, after a face's disocclusion passes: the trace pass and
    // the face's readback (Pvs_face_trace). A diagnostic path: it allocates.
    void record_trace(
        erhe::graphics::Command_buffer&                          command_buffer,
        const Pvs_cell_job&                                      job,
        const Pvs_face_view&                                     face,
        const erhe::scene_renderer::Pvs_disocclusion_parameters& parameters,
        Pvs_cell_record&                                         record,
        std::size_t                                              layered_byte_count
    );

    class Frame_slot
    {
    public:
        uint64_t                                     frame_index{0};
        std::optional<Pvs_cell_job>                  job;
        std::vector<erhe::scene_renderer::Cull_view> cull_views;
    };

    App_context&                                          m_context;
    uint32_t                                              m_face_resolution{0};
    uint32_t                                              m_tile_size      {0};
    uint32_t                                              m_layer_count    {0};
    Pvs_trace_mode                                        m_trace_mode     {Pvs_trace_mode::off};
    // The layered buffer's constant header was written (the first frame).
    bool                                                  m_header_written {false};
    std::size_t                                           m_words_per_layer {0};
    std::size_t                                           m_words_per_region{0};
    erhe::math::Viewport                                  m_viewport{0, 0, 0, 0};
    std::unique_ptr<erhe::graphics::Buffer>               m_layered_buffer;
    // The bake's columns: count, world bounds (two vec4 each), the cell's
    // column words.
    uint32_t                                              m_column_count     {0};
    uint32_t                                              m_column_word_count{0};
    std::vector<glm::vec4>                                m_column_bounds_data;
    std::unique_ptr<erhe::graphics::Buffer>               m_column_bounds;
    std::unique_ptr<erhe::graphics::Buffer>               m_column_words;
    // The face's disocclusion parameters (execute scratch, capacity kept).
    erhe::scene_renderer::Pvs_disocclusion_parameters     m_face_parameters;
    std::unique_ptr<erhe::graphics::Texture>              m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>          m_render_pass;
    std::unique_ptr<erhe::graphics::Base_render_pipeline> m_pipeline;
    std::unique_ptr<erhe::scene_renderer::Pvs_disocclusion_pipelines> m_disocclusion_pipelines;
    std::unique_ptr<erhe::scene_renderer::Pvs_disocclusion_buffers>   m_disocclusion_buffers;
    erhe::scene_renderer::Primitive_buffer                m_primitive_buffer;
    erhe::scene_renderer::Draw_indirect_buffer            m_draw_indirect_buffer;
    // The GPU timers: the cell, and the first face's passes.
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_cell_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_depth_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_id_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_tiles_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_layer_loop_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_map_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer>            m_vote_timer;
    std::vector<Frame_slot>                               m_frame_slots;
    // Main thread: the job submit() handed over, until a prepare phase
    // takes it.
    std::optional<Pvs_cell_job>                           m_pending_job;
    mutable std::mutex                                    m_record_mutex;
    // The cells the render phase recorded, oldest first (m_record_mutex).
    std::vector<Pvs_cell_record>                          m_records;
};

} // namespace editor
