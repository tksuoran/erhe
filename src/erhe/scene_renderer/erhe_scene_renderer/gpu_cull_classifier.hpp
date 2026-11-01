#pragma once

#include "erhe_scene_renderer/cull_classification.hpp"

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Base_render_pipeline;
    class Bind_group_layout;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Reloadable_shader_stages;
    class Shader_stages;
}

namespace erhe::scene_renderer {

class Draw_list_scene;

// The device-wide half of the GPU culling classification
// (doc/erhe/draw_list_renderer.md Q6 "GPU classification"): the compute
// pipeline of res/shaders/cull_classify.comp, its interface blocks and the
// ring buffer clients its per-dispatch view set, mask regions and
// diagnostic readbacks are acquired from. One per device, shared by every
// Draw_list_scene (Draw_list_scene_create_info::gpu_cull_classifier); a
// Draw_list_scene owns its GPU bounds and records its own dispatches.
//
// The GPU command generation of the shadow passes (doc/plans/culling.md
// phase 6, Q6 "GPU command generation") is its second pair of pipelines, the
// two passes of res/shaders/cull_compact.comp (pass bits, compaction), with
// the ring buffer clients of its per-frame job table, pass bit, command and
// count regions.
//
// The raster occlusion test (doc/plans/occlusion_culling.md, Q6 "Occlusion
// test") is its render pipeline: res/shaders/occlusion_test.vert/.frag, a
// depth-tested, color-write-free draw of every entry's box inside a view's
// render pass, whose fragments set the entry's visibility bit; with the
// uniform ring buffer client of its per-draw view block. Supported where the
// classifier is and fragment shaders take storage writes and atomics
// (Device_info::fragment_stores_and_atomics).
//
// Device capability: native multi-draw indirect with a GPU-read draw count
// (Device_info::use_draw_indirect_count; the shadow passes draw the commands
// as erhe::graphics prepared count draws, an indirect command buffer on
// Metal) and Record_indexing::first_instance (the generated commands name
// their records through first_instance), plus compute storage blocks.
// Devices on the emulated multi-draw path, devices on
// Record_indexing::draw_position (the null device), devices without the
// count draw (MoltenVK) and devices whose pipelines failed to build run the
// CPU path only.
class Gpu_cull_classifier
{
public:
    static constexpr unsigned int bounds_binding_point{0};
    static constexpr unsigned int views_binding_point {1};
    static constexpr unsigned int masks_binding_point {2};
    // The occlusion words of the frame's slot (test, result and culled
    // regions, written by the classification of a frame with occlusion
    // views) and those of the previous occlusion frame, which the
    // classification consumes (cull_classify.comp documents them).
    static constexpr unsigned int occlusion_words_binding_point         {3};
    static constexpr unsigned int occlusion_previous_words_binding_point{4};
    // The potentially visible set rule (doc/plans/pvs.md "Runtime
    // consumption", "Residency and streaming"): the resident page slots
    // (Pvs_gpu_database), the frame's PVS culled words (mask layout), the
    // entries' columns (one uint32 per entry, laid out like the bounds) and
    // the frame's page table (per page its slot, the frame slot's buffer).
    static constexpr unsigned int pvs_database_binding_point{5};
    static constexpr unsigned int pvs_culled_binding_point  {6};
    static constexpr unsigned int pvs_columns_binding_point {7};
    static constexpr unsigned int pvs_pages_binding_point   {8};
    // Level of detail selection (doc/plans/lod_gpu_selection.md sections 2
    // and 3): the command templates (lod_word, lod_table_index; the
    // compaction's templates buffer), the level table (one uvec4 row per
    // level) and the frame's level region (persistent per frame slot), one
    // uint32 per entry per group of lod_columns_per_word LOD columns,
    // written by the classification; the previous frame's level region
    // (the frame slot before, read for hysteresis: section 6).
    static constexpr unsigned int lod_templates_binding_point      {9};
    static constexpr unsigned int lod_table_binding_point          {10};
    static constexpr unsigned int lod_levels_binding_point         {11};
    static constexpr unsigned int lod_previous_levels_binding_point{12};
    static constexpr uint32_t     workgroup_size      {64};
    // GPU bounds: two vec4 per entry (center + max_axis_scale, extent +
    // purpose).
    static constexpr std::size_t  bounds_bytes_per_entry{2 * 4 * sizeof(float)};
    // GPU columns: one uint32 per entry.
    static constexpr std::size_t  column_bytes_per_entry{sizeof(uint32_t)};
    // View set: fourteen vec4 per view (six planes, range sphere, info, the
    // near plane of an occlusion view, occlusion info, the PVS query box
    // min and max, the level of detail origin and projection scale, the
    // level of detail column info; doc/plans/lod_gpu_selection.md section
    // 3).
    static constexpr std::size_t  vec4s_per_view      {14};
    // Level table (Draw_list_scene::get_lod_table()): one uvec4 per row.
    static constexpr std::size_t  lod_table_bytes_per_row{4 * sizeof(uint32_t)};
    // Level region: one uint32 per entry per group of 8 LOD columns, the
    // level of column c in the nibble c % 8 (the CPU level words' layout).
    static constexpr std::size_t  lod_level_bytes_per_entry_group{sizeof(uint32_t)};
    static constexpr uint32_t     lod_columns_per_word           {8};
    // Mask: one uvec2 (64 view bits) per entry per group of 64 views.
    static constexpr std::size_t  mask_bytes_per_entry_group{2 * sizeof(uint32_t)};

    // Command generation (cull_compact.comp documents the contents).
    static constexpr unsigned int compact_templates_binding_point{0};
    static constexpr unsigned int compact_jobs_binding_point     {1};
    static constexpr unsigned int compact_masks_binding_point    {2};
    static constexpr unsigned int compact_commands_binding_point {3};
    static constexpr unsigned int compact_counts_binding_point   {4};
    static constexpr unsigned int compact_bits_binding_point     {5};
    // The frame's level region and the level table (pass 1 reads the
    // index range of an entry's selected level).
    static constexpr unsigned int compact_lod_levels_binding_point{6};
    static constexpr unsigned int compact_lod_table_binding_point {7};

    // The storage blocks each compute bind group layout lists (all of its
    // bindings); is_device_capable() requires the larger count of compute
    // storage blocks.
    static constexpr std::array<unsigned int, 13> classify_storage_binding_points{
        bounds_binding_point, views_binding_point, masks_binding_point,
        occlusion_words_binding_point, occlusion_previous_words_binding_point,
        pvs_database_binding_point, pvs_culled_binding_point, pvs_columns_binding_point, pvs_pages_binding_point,
        lod_templates_binding_point, lod_table_binding_point, lod_levels_binding_point, lod_previous_levels_binding_point
    };
    static constexpr std::array<unsigned int, 8> compact_storage_binding_points{
        compact_templates_binding_point, compact_jobs_binding_point, compact_masks_binding_point,
        compact_commands_binding_point, compact_counts_binding_point, compact_bits_binding_point,
        compact_lod_levels_binding_point, compact_lod_table_binding_point
    };
    // Each list is its layout's binding points 0 .. count - 1 in order, so
    // the counts are the pipelines' storage block counts.
    template <std::size_t N>
    [[nodiscard]] static constexpr auto is_dense_binding_list(const std::array<unsigned int, N>& binding_points) -> bool
    {
        for (std::size_t i = 0; i < N; ++i) {
            if (binding_points[i] != i) {
                return false;
            }
        }
        return true;
    }
    static constexpr std::size_t  classify_storage_block_count{classify_storage_binding_points.size()};
    static constexpr std::size_t  compact_storage_block_count {compact_storage_binding_points.size()};
    static constexpr std::size_t  required_compute_storage_blocks{
        (classify_storage_block_count > compact_storage_block_count) ? classify_storage_block_count : compact_storage_block_count
    };
    // A multiple of 32 (one pass bit word per 32 entries).
    static constexpr uint32_t     compact_workgroup_size         {64};
    // Command templates: two uvec4 per entry (index_count, first_index,
    // base_vertex, lod_word and the entry's flag bits, lod_table_index, 0),
    // laid out like the bounds.
    static constexpr std::size_t  template_bytes_per_entry{2 * 4 * sizeof(uint32_t)};
    // Job table: one uvec4 per job (view in bits 0..15, filter index in
    // bits 16..20 and LOD column in bits 21..31, job_lod_column_none for a
    // job without one; first entry, entry count, command offset) after the
    // header uvec4, the LOD header uvec4 and the filter table (two uvec4
    // per filter, up to max_job_filters).
    static constexpr std::size_t  job_bytes               {4 * sizeof(uint32_t)};
    // The frame's distinct Item_filters of the job table: the shadow
    // selection's and the color selections' (a few per view; verified).
    static constexpr std::size_t  max_job_filters         {32};
    static constexpr uint32_t     job_filter_shift        {16};
    static constexpr uint32_t     job_lod_column_shift    {21};
    static constexpr uint32_t     job_lod_column_none     {0x7ffu};
    static_assert(max_job_filters <= (1u << (job_lod_column_shift - job_filter_shift)));
    // A job's command offset is a multiple of this: its pass bit words start
    // at command offset / 32.
    static constexpr uint32_t     job_command_alignment   {32};
    // Per job: the uint32 draw count (what the count draw reads, at the
    // job's first word) and the chunk's entries passing the filter.
    static constexpr std::size_t  count_bytes_per_job     {2 * sizeof(uint32_t)};
    // After the count slots of all jobs, per job its level of detail counts
    // (doc/plans/lod_gpu_selection.md section 5): the commands the job wrote
    // per level, then the triangles (index_count / 3) they draw per level
    // (cull_compact.comp; Lod_level_counts).
    static constexpr std::size_t  lod_count_levels        {8};
    static constexpr std::size_t  lod_count_bytes_per_job {2 * lod_count_levels * sizeof(uint32_t)};

    // The occlusion test's render pipeline (occlusion_test.vert / .frag).
    static constexpr unsigned int occlusion_view_binding_point       {0}; // uniform, vertex
    static constexpr unsigned int occlusion_bounds_binding_point     {1}; // storage, vertex
    static constexpr unsigned int occlusion_draw_test_binding_point  {2}; // storage, vertex
    static constexpr unsigned int occlusion_draw_result_binding_point{3}; // storage, fragment
    // Per entry the three faces of its box toward the camera: seven unique
    // corners, two triangles per face (occlusion_test.vert), drawn indexed.
    static constexpr uint32_t     occlusion_vertices_per_entry       {7};
    static constexpr uint32_t     occlusion_indices_per_entry        {18};
    // Views one occlusion draw rasterizes: one, or the views of a multiview
    // render pass (the headset's eyes; occlusion_test.vert indexes the
    // view block by gl_ViewIndex).
    static constexpr std::size_t  occlusion_max_view_count           {2};

    // The 32-bit halves (low, high) of a 64-bit value as the shaders read
    // them (flag bits, Item_filter masks).
    [[nodiscard]] static constexpr auto split_uint64(const uint64_t value) -> std::array<uint32_t, 2>
    {
        return std::array<uint32_t, 2>{
            static_cast<uint32_t>(value & 0xffffffffu),
            static_cast<uint32_t>(value >> 32u)
        };
    }

    Gpu_cull_classifier(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path);
    ~Gpu_cull_classifier() noexcept;
    Gpu_cull_classifier(const Gpu_cull_classifier&)            = delete;
    Gpu_cull_classifier& operator=(const Gpu_cull_classifier&) = delete;

    [[nodiscard]] static auto is_device_capable(const erhe::graphics::Device& graphics_device) -> bool;
    // The device is capable and the pipeline built.
    [[nodiscard]] auto is_supported() const -> bool;
    // is_supported(), the device takes fragment storage writes and atomics
    // and the occlusion test's pipeline built.
    [[nodiscard]] auto is_occlusion_supported() const -> bool;

    // For Draw_list_scene's dispatch recording.
    [[nodiscard]] auto get_device           () -> erhe::graphics::Device&;
    [[nodiscard]] auto get_bind_group_layout() const -> const erhe::graphics::Bind_group_layout*;
    [[nodiscard]] auto get_pipeline         () const -> const erhe::graphics::Compute_pipeline&;
    [[nodiscard]] auto get_view_buffer      () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_mask_buffer      () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_readback_buffer  () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_view_header_offset() const -> std::size_t { return m_view_header_offset; }
    [[nodiscard]] auto get_view_occlusion_header_offset() const -> std::size_t { return m_view_occlusion_header_offset; }
    // The PVS header (cull_classify.comp documents it): pvs_header,
    // pvs_cells, pvs_pages (uvec4) and pvs_grid (vec4).
    [[nodiscard]] auto get_view_pvs_header_offset() const -> std::size_t { return m_view_pvs_header_offset; }
    [[nodiscard]] auto get_view_pvs_cells_offset () const -> std::size_t { return m_view_pvs_cells_offset; }
    [[nodiscard]] auto get_view_pvs_pages_offset () const -> std::size_t { return m_view_pvs_pages_offset; }
    [[nodiscard]] auto get_view_pvs_grid_offset  () const -> std::size_t { return m_view_pvs_grid_offset; }
    // The level of detail header and column counts (cull_classify.comp
    // documents them).
    [[nodiscard]] auto get_view_lod_header_offset () const -> std::size_t { return m_view_lod_header_offset; }
    [[nodiscard]] auto get_view_lod_columns_offset() const -> std::size_t { return m_view_lod_columns_offset; }
    // The frame's LOD column table (doc/plans/lod_gpu_selection.md sections
    // 3 and 4): max_lod_columns uvec4 (std140 array stride 16), per column
    // its view, purpose (0 color, 1 shadow) and the color columns of a
    // shadow column's sampling views (bit c: color column c), 0.
    [[nodiscard]] auto get_view_lod_column_views_offset() const -> std::size_t { return m_view_lod_column_views_offset; }
    [[nodiscard]] auto get_view_data_offset  () const -> std::size_t { return m_view_data_offset; }
    // For Draw_list_scene's command generation.
    [[nodiscard]] auto get_compact_bind_group_layout() const -> const erhe::graphics::Bind_group_layout*;
    [[nodiscard]] auto get_compact_bits_pipeline    () const -> const erhe::graphics::Compute_pipeline&;
    [[nodiscard]] auto get_compact_pipeline         () const -> const erhe::graphics::Compute_pipeline&;
    [[nodiscard]] auto get_job_buffer               () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_bits_buffer              () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_command_buffer           () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_count_buffer             () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_job_header_offset() const -> std::size_t { return m_job_header_offset; }
    [[nodiscard]] auto get_job_lod_header_offset() const -> std::size_t { return m_job_lod_header_offset; }
    [[nodiscard]] auto get_job_filter_offset() const -> std::size_t { return m_job_filter_offset; }
    [[nodiscard]] auto get_job_data_offset  () const -> std::size_t { return m_job_data_offset; }
    // For Draw_list_scene's occlusion test. The view block holds
    // occlusion_max_view_count clip_from_world matrices and eyes (std140
    // array strides: a mat4, a vec4).
    [[nodiscard]] auto get_occlusion_bind_group_layout  () const -> const erhe::graphics::Bind_group_layout*;
    [[nodiscard]] auto get_occlusion_pipeline           () -> erhe::graphics::Base_render_pipeline&;
    // view_count 1: the single-view stages; 2 .. occlusion_max_view_count:
    // the multiview stages (null when the device has no multiview or they
    // failed to build).
    [[nodiscard]] auto get_occlusion_shader_stages      (std::size_t view_count) const -> const erhe::graphics::Shader_stages*;
    [[nodiscard]] auto get_occlusion_view_buffer        () -> erhe::graphics::Ring_buffer_client&;
    [[nodiscard]] auto get_occlusion_view_block_bytes   () const -> std::size_t { return m_occlusion_view_block_bytes; }
    [[nodiscard]] auto get_occlusion_clip_from_world_offset() const -> std::size_t { return m_occlusion_clip_from_world_offset; }
    [[nodiscard]] auto get_occlusion_eye_offset         () const -> std::size_t { return m_occlusion_eye_offset; }
    [[nodiscard]] auto get_occlusion_info_offset        () const -> std::size_t { return m_occlusion_info_offset; }

private:
    erhe::graphics::Device&                                   m_graphics_device;
    bool                                                      m_device_capable{false};
    erhe::graphics::Shader_resource                           m_bounds_block;
    erhe::graphics::Shader_resource                           m_views_block;
    erhe::graphics::Shader_resource                           m_masks_block;
    erhe::graphics::Shader_resource                           m_occlusion_words_block;
    erhe::graphics::Shader_resource                           m_occlusion_previous_words_block;
    erhe::graphics::Shader_resource                           m_pvs_database_block;
    erhe::graphics::Shader_resource                           m_pvs_culled_block;
    erhe::graphics::Shader_resource                           m_pvs_columns_block;
    erhe::graphics::Shader_resource                           m_pvs_pages_block;
    erhe::graphics::Shader_resource                           m_lod_templates_block;
    erhe::graphics::Shader_resource                           m_lod_table_block;
    erhe::graphics::Shader_resource                           m_lod_levels_block;
    erhe::graphics::Shader_resource                           m_lod_previous_levels_block;
    std::size_t                                               m_view_header_offset{0};
    std::size_t                                               m_view_occlusion_header_offset{0};
    std::size_t                                               m_view_pvs_header_offset{0};
    std::size_t                                               m_view_pvs_cells_offset {0};
    std::size_t                                               m_view_pvs_pages_offset {0};
    std::size_t                                               m_view_pvs_grid_offset  {0};
    std::size_t                                               m_view_lod_header_offset{0};
    std::size_t                                               m_view_lod_columns_offset{0};
    std::size_t                                               m_view_lod_column_views_offset{0};
    std::size_t                                               m_view_data_offset  {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_pipeline;
    erhe::graphics::Ring_buffer_client                        m_view_buffer;
    erhe::graphics::Ring_buffer_client                        m_mask_buffer;
    erhe::graphics::Ring_buffer_client                        m_readback_buffer;

    erhe::graphics::Shader_resource                           m_compact_templates_block;
    erhe::graphics::Shader_resource                           m_compact_jobs_block;
    erhe::graphics::Shader_resource                           m_compact_masks_block;
    erhe::graphics::Shader_resource                           m_compact_commands_block;
    erhe::graphics::Shader_resource                           m_compact_counts_block;
    erhe::graphics::Shader_resource                           m_compact_bits_block;
    erhe::graphics::Shader_resource                           m_compact_lod_levels_block;
    erhe::graphics::Shader_resource                           m_compact_lod_table_block;
    std::size_t                                               m_job_header_offset{0};
    std::size_t                                               m_job_lod_header_offset{0};
    std::size_t                                               m_job_filter_offset{0};
    std::size_t                                               m_job_data_offset  {0};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_compact_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_compact_bits_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_compact_bits_pipeline;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_compact_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_compact_pipeline;
    erhe::graphics::Ring_buffer_client                        m_job_buffer;
    erhe::graphics::Ring_buffer_client                        m_bits_buffer;
    erhe::graphics::Ring_buffer_client                        m_command_buffer;
    erhe::graphics::Ring_buffer_client                        m_count_buffer;

    erhe::graphics::Shader_resource                           m_occlusion_view_block;
    erhe::graphics::Shader_resource                           m_occlusion_bounds_block;
    erhe::graphics::Shader_resource                           m_occlusion_draw_test_block;
    erhe::graphics::Shader_resource                           m_occlusion_draw_result_block;
    std::size_t                                               m_occlusion_view_block_bytes{0};
    std::size_t                                               m_occlusion_clip_from_world_offset{0};
    std::size_t                                               m_occlusion_eye_offset{0};
    std::size_t                                               m_occlusion_info_offset{0};
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_occlusion_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_occlusion_shader_stages;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_occlusion_multiview_shader_stages;
    std::unique_ptr<erhe::graphics::Base_render_pipeline>     m_occlusion_pipeline;
    erhe::graphics::Ring_buffer_client                        m_occlusion_view_buffer;
};

static_assert(Gpu_cull_classifier::is_dense_binding_list(Gpu_cull_classifier::classify_storage_binding_points));
static_assert(Gpu_cull_classifier::is_dense_binding_list(Gpu_cull_classifier::compact_storage_binding_points));

} // namespace erhe::scene_renderer
