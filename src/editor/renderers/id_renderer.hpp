#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
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
    class Mesh;
    class Skin;
}
namespace erhe::scene_renderer {
    class Content_wide_line_renderer;
    class Face_id_base_provider;
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
    bool enabled{true};

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
        const erhe::scene::Camera&         camera;
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& content_mesh_spans;
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& tool_mesh_spans;
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
        std::span<const std::shared_ptr<erhe::scene::Skin>>          skins{};
        Skinning_filter                                              skinning_filter{Skinning_filter::all};
    };
    void render            (const Render_parameters& parameters);
    void next_frame        ();

    // ID-buffer edge-line method: render the content edge ribbons -- already
    // queued and compute-expanded by the caller into wide_line_renderer in id
    // mode -- into a viewport-sized rgba8 face-ID buffer with depth test, BEFORE
    // the main viewport pass. The polygon-fill shader then samples
    // get_edge_id_texture() and paints an edge line wherever the stored face id
    // matches its own facet id. This framebuffer is separate from the picking
    // one above: its color is sampled in-frame (not read back) and its depth is
    // cleared then discarded.
    void render_content_edge_id(
        erhe::graphics::Command_buffer&                   command_buffer,
        const erhe::math::Viewport&                       viewport,
        erhe::scene_renderer::Content_wide_line_renderer& wide_line_renderer
    );
    [[nodiscard]] auto get_edge_id_texture() const -> erhe::graphics::Texture*;

    // ID-buffer edge-line method, SEED pass. Renders the visible content fill
    // (the meshes registered for the edge method) outputting each fragment's
    // encoded face id (per-primitive base + facet id, the same registry namespace
    // the edge-id pre-pass and the EDGE_LINES_FROM_ID fill use) into a dedicated
    // face-ID buffer with its own depth buffer, so the buffer holds the FRONTMOST
    // visible face id per pixel. render_content_edge_id() then hands this texture
    // to the wide-line draw, which discards any edge fragment whose own face id
    // does not equal the seed there -- rejecting cap / overspray fragments before
    // they can win the edge-id depth test. Runs BEFORE the edge compute + edge-id
    // pass; its color is sampled in-frame (not read back) and its depth is
    // cleared then discarded.
    class Seed_render_parameters
    {
    public:
        erhe::graphics::Command_buffer&                            command_buffer;
        const erhe::math::Viewport&                                viewport;
        const erhe::scene::Camera&                                 camera;
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes;
        const erhe::scene_renderer::Face_id_base_provider*         face_id_base_provider{nullptr};
        bool                                                       reverse_depth{true};
        erhe::math::Depth_range                                    depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions                         conventions;
        // Joint UBO/SSBO for skinned content (same Joint_buffer that
        // Forward_renderer uses, so both updates allocate disjoint ring ranges).
        // The seed must apply skinning so its visible surface matches the fill.
        erhe::scene_renderer::Joint_buffer*                        joint_buffer{nullptr};
        std::span<const std::shared_ptr<erhe::scene::Skin>>        skins{};
    };
    void render_content_seed(const Seed_render_parameters& parameters);
    [[nodiscard]] auto get_seed_id_texture() const -> erhe::graphics::Texture*;
    [[nodiscard]] auto get_seed_sampler   () const -> const erhe::graphics::Sampler*;

    [[nodiscard]] auto get(int x, int y, uint32_t& out_id, float& out_depth, uint64_t& out_frame_number) -> bool;
    [[nodiscard]] auto get(int x, int y) -> Id_query_result;

    // Side length of the square readback region (and the natural size for a
    // dedicated pick framebuffer). Callers that render the ID pass from a
    // ray-aligned camera size their viewport to this and query the centre
    // texel (get(get_extent()/2, get_extent()/2)).
    [[nodiscard]] static constexpr auto get_extent() -> int { return s_extent; }

    // Region selection scan (box / paint select). A selection gesture requests
    // a scan of a viewport-pixel rectangle (optionally masked to a brush disk)
    // of the ID buffer; render() rasterizes and blits that rectangle to a
    // CPU-read buffer, and take_scan_result() resolves the completed readback
    // into the set of visible (mesh, primitive, triangle) hits a few frames
    // later (async, same latency as the pointer pick). Faces only for now; the
    // caller maps triangle -> facet.
    class Scan_request
    {
    public:
        int       x           {0};      // viewport-pixel bounding rect (clamped to the viewport in render())
        int       y           {0};
        int       width       {0};
        int       height      {0};
        glm::vec2 brush_center {0.0f, 0.0f};
        float     brush_radius {0.0f};  // > 0 with is_brush => disk mask over the rect, else whole rect
        bool      is_brush     {false};
    };
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

    // Request a region scan for the next render(). The gesture re-requests every
    // frame it wants a scan; the request is consumed (and reset) by render().
    void request_scan(const Scan_request& request);
    [[nodiscard]] auto has_pending_scan() const -> bool;
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
    };

    [[nodiscard]] auto get_current_transfer_entry() -> Transfer_entry&;

    void update_framebuffer(erhe::math::Viewport viewport);
    void update_edge_id_framebuffer(erhe::math::Viewport viewport);

    bool                                         m_enabled{true};
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

    erhe::graphics::Base_render_pipeline         m_pipeline;
    erhe::graphics::Base_render_pipeline         m_selective_depth_clear_pipeline;
    std::unique_ptr<erhe::graphics::Texture>     m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    std::unique_ptr<erhe::graphics::Gpu_timer>   m_gpu_timer;
    // Edge-ID pre-pass render target (ID-buffer edge-line method), separate from
    // the picking framebuffer above: color is sampled by the polygon-fill pass
    // the same frame, depth is cleared then discarded, and nothing is read back.
    std::unique_ptr<erhe::graphics::Texture>     m_edge_id_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_edge_id_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_edge_id_render_pass;
    // Seed pre-pass render target (ID-buffer edge-line method): the visible
    // content fill rendered with VARIANT_FACE_ID_SEED into a face-ID color buffer
    // (sampled by the edge-id pass) with its own depth (frontmost face wins, then
    // discarded). The sampler is nearest/clamp -- the edge-id pass texelFetches by
    // integer pixel, so filtering is irrelevant; created once, size-independent.
    std::unique_ptr<erhe::graphics::Texture>     m_seed_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_seed_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_seed_render_pass;
    std::unique_ptr<erhe::graphics::Sampler>     m_seed_sampler;
    erhe::graphics::Ring_buffer_client           m_texture_read_buffer;

    std::array<Transfer_entry, s_transfer_entry_count> m_transfer_entries;
    int                                                m_current_transfer_entry_slot{0};

    // Region selection scan state (box / paint). A small ring of color-only
    // readback entries, sized per-scan (the scan rect, not s_extent), so several
    // scans can be in flight while a gesture drags. Parallel to the pointer-pick
    // Transfer_entry ring above but independent of it.
    static constexpr int s_region_entry_count = 3;
    class Region_entry
    {
    public:
        erhe::graphics::Ring_buffer_range buffer_range;
        std::vector<uint8_t>              data        {};
        int                               x           {0};
        int                               y           {0};
        int                               width       {0};
        int                               height      {0};
        int                               row_stride  {0}; // bytes per row in `data` (256-byte aligned for the GPU copy)
        glm::vec2                         brush_center{0.0f, 0.0f};
        float                             brush_radius{0.0f};
        bool                              is_brush    {false};
        uint64_t                          frame_number{0};
        Transfer_entry::State             state       {Transfer_entry::State::Unused};
        // GPU compute-gather path: when used_compute is true the scan was resolved
        // by the two compute passes, the small {count, ids} result was read back
        // into compact_data (not the per-pixel `data` above), and take_scan_result()
        // resolves scan_count ids from it instead of looping texels. compact_range
        // is the CPU_read ring range the completion handler copied from.
        bool                              used_compute{false};
        erhe::graphics::Ring_buffer_range compact_range;
        std::vector<uint8_t>              compact_data{}; // {uint count; uint ids[max_output];}
        uint32_t                          max_output  {0};
        // Snapshot of the id-range table as it was when this scan was submitted.
        // The live m_primitive_buffers.id_ranges() is rebuilt every frame from the
        // meshes actually drawn that frame, and a scan frame forces
        // Skinning_filter::all (different mesh set => different id_offsets) than the
        // normal skinned_only frame on which the async result is resolved. Resolving
        // against this self-contained snapshot keeps each scan correct regardless of
        // intervening frames. Cleared-and-refilled (capacity kept) to avoid per-scan
        // allocation churn (CLAUDE.md run-time allocation discipline).
        std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> id_ranges{};
    };
    std::optional<Scan_request>                    m_pending_scan;
    std::array<Region_entry, s_region_entry_count> m_region_entries;
    erhe::graphics::Ring_buffer_client             m_region_read_buffer;
    Scan_result                                    m_scan_result;
    std::set<uint32_t>                             m_scan_id_scratch; // reused id-dedup set; cleared per resolve
    // Snapshot of the id-range table from the most recently resolved scan, kept so
    // the MCP id-range-mapping query can report it after a scan. Cleared-and-filled
    // (capacity kept) in take_scan_result().
    std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> m_last_scan_id_ranges;

    // ---- Box/paint GPU compute gather --------------------------------------
    // Two compute passes replace the per-pixel CPU readback + dedup loop when the
    // device supports compute shaders (Vulkan / Metal / GL >= 4.3): pass 1
    // (id_scan_gather.comp) scans the blitted region id buffer and atomicOrs each
    // decoded id into a bitmask (the dedup); pass 2 (id_scan_compact.comp) compacts
    // the set bits into a dense {count, ids} vector that is read back. On devices
    // without compute (e.g. macOS GL 4.1) m_scan_compute_available stays false and
    // the CPU region-scan path is used. Built lazily on first scan.
    void                                               ensure_scan_compute();
    [[nodiscard]] auto submit_scan_compute(
        erhe::graphics::Command_buffer& command_buffer,
        Region_entry&                   region,
        int scan_x, int scan_y, int scan_w, int scan_h
    ) -> bool;
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

    void render_meshes(
        const Render_parameters&                                   parameters,
        erhe::graphics::Render_command_encoder&                    render_encoder,
        erhe::graphics::Base_render_pipeline&                      pipeline,
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
    );

    // Shared bucket-and-draw core for both the picking ID render (render_meshes)
    // and the edge-line seed render (render_content_seed). Buckets the meshes,
    // resolves a shader variant per bucket (force_enable_mask), and draws them
    // into the supplied render pass with the given primitive settings.
    // use_id_ranges records the id_offset->mesh table the picking readback walks;
    // the seed pass passes false (its color is sampled, never read back).
    void render_buckets(
        erhe::graphics::Render_command_encoder&                    render_encoder,
        erhe::graphics::Base_render_pipeline&                      pipeline,
        const erhe::graphics::Render_pass&                         render_pass,
        const erhe::scene_renderer::Primitive_interface_settings&  primitive_settings,
        uint32_t                                                   force_enable_mask,
        bool                                                       use_id_ranges,
        const erhe::Item_filter&                                   filter,
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes
    );

    void update_seed_framebuffer(erhe::math::Viewport viewport);

    std::vector<Range> m_ranges;
    // Scissor optimization: restrict rasterization to the s_extent rect
    // around the pointer (the only region the readback blit copies). The
    // viewport is always set to the full size (see render()), so geometry
    // is transformed identically whether or not the scissor is enabled --
    // the scissor only clips fragments outside the pointer rect.
    bool               m_use_scissor{true};
};

}
