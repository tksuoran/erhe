#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace erhe::graphics {
    class Base_render_pipeline;
    class Buffer;
    class Command_buffer;
    class Render_pass;
    class Texture;
}
namespace erhe::scene {
    class Pvs_database;
}

namespace editor {

class App_context;
class Scene_root;

// One face of one sample point of a potentially visible set bake
// (doc/plans/pvs.md "Bake" 3 and 4), as Pvs_baker hands it to its
// Pvs_bake_node.
class Pvs_face_job
{
public:
    std::shared_ptr<Scene_root>      scene_root;
    // The bake's column table, set as the scene's database for the bake:
    // the face is drawn only in a frame whose published draw lists carry
    // its columns (the static set, Draw_list::pvs_columns).
    const erhe::scene::Pvs_database* columns{nullptr};
    // The face camera: a perspective view from the sample point along the
    // face axis, its near plane at z_near and its far plane at z_far.
    erhe::scene::Camera_snapshot     camera;
    float                            z_near {0.0f};
    float                            z_far  {0.0f};
    uint32_t                         cell   {0};
    uint32_t                         face   {0};
    uint32_t                         sample {0};
    // A layer whose depth and id_min words are copied to the host after
    // the passes (pvs_debug_face); none for a bake face.
    std::optional<uint32_t>          readback_layer{};
};

// What the render phase recorded for a face (Pvs_bake_node::take_completed()).
class Pvs_face_record
{
public:
    uint64_t                                frame        {0};
    uint32_t                                cell         {0};
    uint32_t                                face         {0};
    uint32_t                                sample       {0};
    float                                   z_near       {0.0f};
    float                                   z_far        {0.0f};
    // Entries drawn by the depth pass and by the id pass.
    std::size_t                             depth_entries{0};
    std::size_t                             id_entries   {0};
    // readback_layer: the layer's depth words, then its id_min words
    // (face_resolution^2 each, the layered buffer's row order).
    std::optional<uint32_t>                 readback_layer{};
    std::unique_ptr<erhe::graphics::Buffer> readback;
};

// The rendergraph node of a potentially visible set bake (Pvs_baker,
// doc/plans/pvs.md D6): one face per frame. It owns everything its execute
// reads and writes: the layered buffer (Pvs_layered_interface: its header,
// then the dense depth, id_min and id_max words of one face,
// face_resolution^2 * layer_count words each, device-local), the
// depth-only render pass of the face size, the
// pipeline, the record and draw command buffers, and per frame in flight
// the face job the prepare phase handed to it. A node with no links.
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
// disables itself. Render phase: the pre-execute hook registers the face's
// frustum as a culling view; the execute clears the layered buffer, draws
// the depth pass and the id pass (Draw_list_scene::draw_pvs_face()) and,
// for a readback job, copies the layer to the host. The records are shared
// with the main thread under m_record_mutex.
class Pvs_bake_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Pvs_bake_node(
        erhe::rendergraph::Rendergraph& rendergraph,
        App_context&                    context,
        uint32_t                        face_resolution,
        uint32_t                        layer_count
    );
    ~Pvs_bake_node() noexcept override;

    auto get_type_name           () const -> std::string_view override { return "Pvs_bake_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // Main thread (M2), before the rendergraph prepares the frame: the next
    // face. Verifies the previous one was taken by a prepare phase.
    void submit(Pvs_face_job&& job);
    // Main thread: true while a submitted face waits for its frame.
    [[nodiscard]] auto has_pending_job() const -> bool;
    // Main thread: drops the submitted face no prepare phase took (a bake
    // that stopped).
    void cancel_pending_job();
    // Main thread: the recorded faces whose frames completed, oldest first.
    void take_completed(std::vector<Pvs_face_record>& out);
    // Main thread: faces recorded into frames that have not completed (no
    // GPU work of the node may still run when this is false and no job is
    // pending).
    [[nodiscard]] auto has_frames_in_flight() const -> bool;

    [[nodiscard]] auto get_face_resolution() const -> uint32_t { return m_face_resolution; }
    [[nodiscard]] auto get_layer_count    () const -> uint32_t { return m_layer_count; }

protected:
    void prepare_rendergraph_node    (uint64_t frame_index) override;
    void pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

private:
    class Frame_slot
    {
    public:
        uint64_t                              frame_index{0};
        std::optional<Pvs_face_job>           job;
        erhe::scene_renderer::Cull_view       cull_view  {erhe::scene_renderer::Cull_view::none};
    };

    App_context&                                          m_context;
    uint32_t                                              m_face_resolution{0};
    uint32_t                                              m_layer_count    {0};
    std::size_t                                           m_words_per_layer {0};
    std::size_t                                           m_words_per_region{0};
    erhe::math::Viewport                                  m_viewport{0, 0, 0, 0};
    std::unique_ptr<erhe::graphics::Buffer>               m_layered_buffer;
    std::unique_ptr<erhe::graphics::Texture>              m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>          m_render_pass;
    std::unique_ptr<erhe::graphics::Base_render_pipeline> m_pipeline;
    erhe::scene_renderer::Primitive_buffer                m_primitive_buffer;
    erhe::scene_renderer::Draw_indirect_buffer            m_draw_indirect_buffer;
    std::vector<Frame_slot>                               m_frame_slots;
    // Main thread: the job submit() handed over, until a prepare phase
    // takes it.
    std::optional<Pvs_face_job>                           m_pending_job;
    mutable std::mutex                                    m_record_mutex;
    // The faces the render phase recorded, oldest first (m_record_mutex).
    std::vector<Pvs_face_record>                          m_records;
};

} // namespace editor
