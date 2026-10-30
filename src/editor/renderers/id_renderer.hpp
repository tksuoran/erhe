#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_scene_renderer/joint_poses.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/id_pass_prepared.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

typedef struct __GLsync *GLsync;

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Bind_group_layout;
    class Command_buffer;
    class Compute_command_encoder;
    class Render_pass;
    class Gpu_timer;
    class Device;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Camera_snapshot;
    class Mesh;
    class Skin;
}
namespace erhe::scene_renderer {
    class Joint_buffer;
    class Mesh_memory;
    class Program_interface;
    class Shader_variant_cache;
}

struct Id_renderer_config;

namespace editor {

class Programs;

class Id_renderer final
{
public:
    // id_renderer.enabled of the config, repurposed under the hybrid picker:
    // true makes the ID pass cover every mesh, false only skinned meshes
    // (App_rendering::prepare_id). Fixed at construction (forced true without
    // a raytrace backend), so the render phase reads it without a frame copy.
    [[nodiscard]] auto covers_all_meshes() const -> bool;

    class Id_query_result
    {
    public:
        uint32_t                           id             {0};
        float                              depth          {0.0f};
        std::shared_ptr<erhe::scene::Mesh> mesh           {};
        std::size_t                        index_of_gltf_primitive_in_mesh{0};
        // The GEO facet index under the queried pixel, decoded as (id - range.offset).
        // The id pass emits the facet id per vertex (a_custom_0), so this is the
        // facet directly -- no triangle_to_mesh_facet indirection on the GPU path.
        std::size_t                        facet_id       {std::numeric_limits<std::size_t>::max()};
        bool                               valid          {false};
        uint64_t                           frame_number   {0};
    };

    Id_renderer(
        const Id_renderer_config&                   id_renderer_config,
        erhe::graphics::Device&                     graphics_device,
        erhe::scene_renderer::Program_interface&    program_interface,
        erhe::scene_renderer::Mesh_memory&          mesh_memory,
        erhe::scene_renderer::Shader_variant_cache& shader_variant_cache,
        Programs&                                   programs
    );
    ~Id_renderer() noexcept;

    // Public API

    // Which meshes the ID pass actually rasterizes. Skinned-only is the
    // hybrid default: a separate raytrace pass covers static meshes (it
    // does so correctly because the rest-pose BVH equals the displayed
    // surface) and the ID pass only carries the meshes that GPU skinning
    // posed away from rest. `all` is the legacy "ID renderer covers
    // everything" mode used by the force-id config knob.
    enum class Skinning_filter
    {
        skinned_only,
        all
    };

    class Render_parameters
    {
    public:
        erhe::graphics::Command_buffer&    command_buffer;
        const erhe::math::Viewport&        viewport;
        const erhe::scene::Camera_snapshot& camera; // the render phase's copy of the view camera
        const int                          x;
        const int                          y;
        bool                               reverse_depth{true};
        erhe::math::Depth_range            depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions conventions;

        // Joint UBO/SSBO that standard.{vert,frag} reads under
        // ERHE_USE_SKINNING; the id pass uses the same shader pair via
        // ERHE_VARIANT_ID_RENDER, so it needs the same joint binding when
        // any of its buckets contain a skinned mesh. Pass the same
        // Joint_buffer that Forward_renderer uses (Forward_renderer::get_joint_buffer())
        // so both updates allocate disjoint ring ranges.
        erhe::scene_renderer::Joint_buffer*                          joint_buffer{nullptr};
        std::span<const erhe::scene_renderer::Joint_pose>            joint_poses{}; // the frame's posed joints (Joint_poses::capture)
    };

    // What the id pass draws: the meshes of these spans that carry
    // visible | active | id, all of them or only the skinned ones.
    class Prepare_parameters
    {
    public:
        std::span<const std::span<const std::shared_ptr<erhe::scene::Mesh>>> mesh_spans;
        Skinning_filter                                                    skinning_filter{Skinning_filter::all};
        std::optional<Id_scan_request>                                     scan{}; // the frame's region scan this pass carries (take_frame_scan())
    };

    // Prepare phase (main thread): buckets the meshes on the original
    // variant (the only one with valid facet ids) and writes their records
    // (id offsets in the color), draw commands, id ranges and the region
    // scan into out. Mutates nothing of this renderer.
    void prepare(const Prepare_parameters& parameters, Id_pass_prepared& out) const;

    // Render phase: renders a prepared id pass around the pointer (and the
    // region scan the pass carries), reading no Mesh, and starts the readback. The
    // transfer entry (and the scan's region entry) keep their own copy of
    // prepared.id_ranges, which get() / take_scan_result() resolve against.
    void render(const Render_parameters& parameters, const Id_pass_prepared& prepared);

    // What render_image() draws with: the caller's render pass, whose color
    // attachment 0 is a format_8_vec4_unorm texture cleared to 0 (no id) and
    // whose depth attachment is format_d32_sfloat cleared to the far value of
    // reverse_depth, and the camera and viewport the pass covers.
    class Image_render_parameters
    {
    public:
        erhe::graphics::Command_buffer&                   command_buffer;
        erhe::graphics::Render_pass&                      render_pass;
        const erhe::math::Viewport&                       viewport;
        const erhe::scene::Camera_snapshot&               camera;
        bool                                              reverse_depth{true};
        erhe::math::Depth_range                           depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions                conventions;
        erhe::scene_renderer::Joint_buffer*               joint_buffer{nullptr}; // as Render_parameters::joint_buffer
        std::span<const erhe::scene_renderer::Joint_pose> joint_poses{};
    };

    // Render phase: draws a prepared id pass over the whole viewport into
    // the caller's render pass. Starts no readback and touches neither the
    // pick framebuffer nor the transfer entries: the caller owns the target
    // and reads it back (the whole-image id capture, Scene_id_capture_node).
    void render_image(const Image_render_parameters& parameters, const Id_pass_prepared& prepared);

    // prepare() and render() back to back on the calling thread, through a
    // prepared pass this renderer keeps: the headset, which renders its pick
    // pass where it prepares it (doc/editor/render_thread.md section 2.2).
    void render_immediate(const Prepare_parameters& prepare_parameters, const Render_parameters& parameters);
    void next_frame        ();


    [[nodiscard]] auto get(int x, int y, uint32_t& out_id, float& out_depth, uint64_t& out_frame_number) -> bool;
    [[nodiscard]] auto get(int x, int y) -> Id_query_result;

    // Side length of the square readback region (and the natural size for a
    // dedicated pick framebuffer). Callers that render the ID pass from a
    // ray-aligned camera size their viewport to this and query the centre
    // texel (get(get_extent()/2, get_extent()/2)).
    [[nodiscard]] static constexpr auto get_extent() -> int { return s_extent; }

    // Region selection scan (box / paint select). A selection gesture requests
    // a scan of a viewport-pixel rectangle (optionally masked to a brush disk)
    // of the ID buffer; render() rasterizes that rectangle and two compute
    // passes compact its distinct ids into a small readback, and
    // take_scan_result() resolves the completed readback into the set of
    // visible (mesh, primitive, triangle) hits a few frames later (async, same
    // latency as the pointer pick). Faces only for now; the
    // caller maps triangle -> facet.
    using Scan_request = Id_scan_request;
    class Scan_hit
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh           {};
        std::size_t                        primitive_index{0};
        // GEO facet index for the scanned pixel (decoded as id - range.offset).
        std::size_t                        facet_id       {0};
    };
    class Scan_result
    {
    public:
        bool                  ready       {false};
        uint64_t              frame_number{0};   // frame the scanned pixels came from; lets callers detect a fresh scan
        std::vector<Scan_hit> hits        {};
    };

    // Request a region scan for the next prepared frame (main thread). The
    // gesture re-requests every frame it wants a scan; begin_frame() (M2,
    // after the frame's tools ran) hands the request to the prepare frame,
    // and the first view that prepares an id pass for that frame claims it
    // with take_frame_scan() into its Id_pass_prepared, the per-frame copy
    // render() reads. Main thread only: the render phase never reads these.
    void request_scan(const Scan_request& request);
    void begin_frame();
    // Prepare phase: the prepare frame has an unclaimed scan request.
    [[nodiscard]] auto has_frame_scan() const -> bool;
    // Prepare phase: claims the prepare frame's scan request (empty when
    // there is none or a view already claimed it).
    [[nodiscard]] auto take_frame_scan() -> std::optional<Scan_request>;
    // Resolve the newest completed region readback (if any newer than the last
    // resolved) into the scan result and return it. The returned reference is
    // stable; check .ready and compare .frame_number against the last value the
    // caller consumed to detect a freshly completed scan.
    [[nodiscard]] auto take_scan_result() -> const Scan_result&;

    // The id-range table captured by the most recently resolved region scan
    // (offset, length, mesh, primitive index). This is the offset->mesh/face
    // mapping the readback inverts: a decoded id in [offset, offset+length)
    // selects this primitive, and (id - offset) is its 0-based facet index.
    // Empty until a scan has been resolved. Used by the MCP id-range-mapping
    // query to expose the encoding for inspection.
    [[nodiscard]] auto get_last_scan_id_ranges() const -> const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>&;


private:
    static constexpr int s_transfer_entry_count = 4;
    static constexpr int s_extent               = 256; // Bigger value handle faster mouse speeds

    class Transfer_entry
    {
    public:
        enum class State : unsigned int {
            Unused = 0,
            Waiting_for_read,
            Read_complete
        };

        erhe::graphics::Ring_buffer_range buffer_range;
        std::vector<uint8_t>              data           {};
        glm::mat4                         clip_from_world{1.0f};
        int                               x_offset       {0};
        int                               y_offset       {0};
        uint64_t                          frame_number   {0};
        int                               slot           {0};
        State                             state          {State::Unused};
        // The id range table of the pass whose pixels this entry reads back,
        // copied from the prepared pass when the entry is recorded
        // (cleared and refilled, capacity kept): get() resolves the entry's
        // ids against it, never against a later frame's table.
        std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> id_ranges{};
    };

    [[nodiscard]] auto get_current_transfer_entry() -> Transfer_entry&;
    // The newest completed readback whose rect contains (x, y): its slot in
    // m_transfer_entries, or -1. The caller holds m_transfer_mutex.
    [[nodiscard]] auto read_pixel(int x, int y, uint32_t& out_id, float& out_depth, uint64_t& out_frame_number) const -> int;
    void draw_prepared(
        erhe::graphics::Render_command_encoder& render_encoder,
        const erhe::graphics::Render_pass&      render_pass,
        const Id_pass_prepared&                 prepared
    );

    void update_framebuffer(erhe::math::Viewport viewport);

    bool                                         m_enabled{true};
    bool                                         m_covers_all_meshes{true}; // constant after construction
    erhe::math::Viewport                         m_viewport{0, 0, 0, 0};

    // TODO Do not store these here?
    erhe::graphics::Device&                      m_graphics_device;
    erhe::scene_renderer::Mesh_memory&           m_mesh_memory;
    erhe::scene_renderer::Shader_variant_cache&  m_shader_variant_cache;
    Programs&                                    m_programs;
    // The bind group layout (Vulkan pipeline layout) shared by all
    // standard.{vert,frag} variants. Must be set on the render command
    // encoder before any buffer bind (Forward_renderer does the same).
    const erhe::graphics::Bind_group_layout*     m_bind_group_layout;
    bool                                         m_y_flip;

    erhe::scene_renderer::Camera_buffer          m_camera_buffers;
    erhe::scene_renderer::Draw_indirect_buffer   m_draw_indirect_buffers;
    erhe::scene_renderer::Primitive_buffer       m_primitive_buffers;

    // The debug label of one bucket, cleared at use so the capacity is kept;
    // the pass render_immediate() prepares and draws.
    std::string                                     m_bucket_label;
    Id_pass_prepared                                m_immediate_prepared;

    erhe::graphics::Base_render_pipeline         m_pipeline;
    std::unique_ptr<erhe::graphics::Texture>     m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    std::unique_ptr<erhe::graphics::Gpu_timer>   m_gpu_timer;
    erhe::graphics::Ring_buffer_client           m_texture_read_buffer;

    // Guards the readback bookkeeping both roles touch: the transfer and
    // region entries (their state, and the fields the main thread reads once
    // an entry is Read_complete) and m_current_transfer_entry_slot. With
    // frame pipelining the render phase records and completes readbacks
    // (render(), next_frame(), the completion handlers) while the prepare
    // phase of the next frame reads them (get(), take_scan_result()). An
    // entry the render phase fills is claimed (Waiting_for_read) under the
    // lock first, so its other fields are written outside it. Never held
    // across a Device call: the completion handlers take it under the
    // device's completion lock.
    mutable std::mutex                                 m_transfer_mutex;
    std::array<Transfer_entry, s_transfer_entry_count> m_transfer_entries;
    int                                                m_current_transfer_entry_slot{0};

    // Region selection scan state (box / paint). A small ring of scan entries
    // so several scans can be in flight while a gesture drags. Parallel to the
    // pointer-pick Transfer_entry ring above but independent of it.
    static constexpr int s_region_entry_count = 3;
    class Region_entry
    {
    public:
        glm::vec2                         brush_center{0.0f, 0.0f};
        float                             brush_radius{0.0f};
        bool                              is_brush    {false};
        uint64_t                          frame_number{0};
        Transfer_entry::State             state       {Transfer_entry::State::Unused};
        // The two compute passes resolve the scan into the small {count, ids}
        // result that is read back into compact_data; take_scan_result()
        // resolves the ids from it. compact_range is the CPU_read ring range
        // the completion handler copied from.
        erhe::graphics::Ring_buffer_range compact_range;
        std::vector<uint8_t>              compact_data{}; // {uint count; uint ids[max_output];}
        uint32_t                          max_output  {0};
        // Copy of the id-range table of the prepared pass this scan was
        // submitted with. Every frame's pass has its own table, built from
        // the meshes drawn that frame, and a scan frame forces
        // Skinning_filter::all (different mesh set => different id_offsets) than the
        // normal skinned_only frame on which the async result is resolved. Resolving
        // against this self-contained snapshot keeps each scan correct regardless of
        // intervening frames. Cleared-and-refilled (capacity kept) to avoid per-scan
        // allocation churn (AGENTS.md run-time allocation discipline).
        std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> id_ranges{};
    };
    std::optional<Scan_request>                    m_pending_scan; // main thread: request_scan -> begin_frame
    std::optional<Scan_request>                    m_frame_scan;   // M2: begin_frame -> take_frame_scan
    std::array<Region_entry, s_region_entry_count> m_region_entries;
    Scan_result                                    m_scan_result;
    std::set<uint32_t>                             m_scan_id_scratch; // reused id-dedup set; cleared per resolve
    // Snapshot of the id-range table from the most recently resolved scan, kept so
    // the MCP id-range-mapping query can report it after a scan. Cleared-and-filled
    // (capacity kept) in take_scan_result().
    std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> m_last_scan_id_ranges;

    // ---- Box/paint GPU compute gather --------------------------------------
    // Two compute passes resolve a region scan: pass 1 (id_scan_gather.comp)
    // scans the blitted region id buffer and atomicOrs each decoded id into a
    // bitmask (the dedup); pass 2 (id_scan_compact.comp) compacts the set bits
    // into a dense {count, ids} vector that is read back. Built lazily on
    // first scan.
    void                                               ensure_scan_compute();
    void submit_scan_compute(
        erhe::graphics::Command_buffer& command_buffer,
        Region_entry&                   region,
        int scan_x, int scan_y, int scan_w, int scan_h
    );
    bool                                               m_scan_compute_attempted{false};
    bool                                               m_scan_compute_available{false};
    std::unique_ptr<erhe::graphics::Shader_resource>   m_scan_input_block;   // binding 0 readonly SSBO (region pixels)
    std::unique_ptr<erhe::graphics::Shader_resource>   m_scan_bitmask_block; // binding 1 rw SSBO (dedup bitmask)
    std::unique_ptr<erhe::graphics::Shader_resource>   m_scan_output_block;  // binding 2 rw SSBO ({count, ids})
    std::unique_ptr<erhe::graphics::Shader_resource>   m_scan_params_block;  // binding 3 UBO (scan params)
    // One bind group layout per pass, each matching exactly the blocks its
    // shader references (gather: input + bitmask + params; compact: bitmask +
    // output + params). Exact-match layouts avoid bound-but-unused-binding
    // descriptor validation friction.
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_scan_gather_bind_group_layout;
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_scan_compact_bind_group_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_scan_gather_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_scan_compact_stages;
    std::optional<erhe::graphics::Compute_pipeline>    m_scan_gather_pipeline;
    std::optional<erhe::graphics::Compute_pipeline>    m_scan_compact_pipeline;
    erhe::graphics::Ring_buffer_client                 m_scan_input_buffer;   // GPU storage: region pixels (texture blit dst)
    erhe::graphics::Ring_buffer_client                 m_scan_bitmask_buffer; // GPU storage: dedup bitmask
    erhe::graphics::Ring_buffer_client                 m_scan_output_buffer;  // GPU storage: {count, ids}
    erhe::graphics::Ring_buffer_client                 m_scan_params_buffer;  // CPU_write UBO: scan params
    erhe::graphics::Ring_buffer_client                 m_scan_result_buffer;  // CPU_read: {count, ids} readback
    // std140 field offsets inside the scan-params UBO, captured when the block is built.
    class Scan_param_offsets
    {
    public:
        std::size_t width{0}, height{0}, pixel_count{0}, row_stride_uints{0};
        std::size_t region_x{0}, region_y{0}, is_brush{0};
        std::size_t brush_center_x{0}, brush_center_y{0}, brush_radius{0};
        std::size_t max_id{0}, bitmask_word_count{0}, max_output{0};
    };
    Scan_param_offsets                                 m_scan_param_offsets;

    class Range
    {
    public:
        uint32_t                           offset   {0};
        uint32_t                           length   {0};
        std::shared_ptr<erhe::scene::Mesh> mesh     {nullptr};
        std::size_t                        mesh_primitive_index{0};
    };

    std::vector<Range> m_ranges;
    // Scissor optimization: restrict rasterization to the s_extent rect
    // around the pointer (the only region the readback blit copies). The
    // viewport is always set to the full size (see render()), so geometry
    // is transformed identically whether or not the scissor is enabled --
    // the scissor only clips fragments outside the pointer rect.
    bool               m_use_scissor{true};
};

}
