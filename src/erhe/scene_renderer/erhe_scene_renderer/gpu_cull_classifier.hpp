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
    static constexpr uint32_t     workgroup_size      {64};
    // GPU bounds: two vec4 per entry (center + 0, extent + purpose).
    static constexpr std::size_t  bounds_bytes_per_entry{2 * 4 * sizeof(float)};
    // View set: ten vec4 per view (six planes, range sphere, info, the
    // near plane of an occlusion view, occlusion info).
    static constexpr std::size_t  vec4s_per_view      {10};
    // Mask: one uvec2 (64 view bits) per entry per group of 64 views.
    static constexpr std::size_t  mask_bytes_per_entry_group{2 * sizeof(uint32_t)};

    // Command generation (cull_compact.comp documents the contents).
    static constexpr unsigned int compact_templates_binding_point{0};
    static constexpr unsigned int compact_jobs_binding_point     {1};
    static constexpr unsigned int compact_masks_binding_point    {2};
    static constexpr unsigned int compact_commands_binding_point {3};
    static constexpr unsigned int compact_counts_binding_point   {4};
    static constexpr unsigned int compact_bits_binding_point     {5};
    // A multiple of 32 (one pass bit word per 32 entries).
    static constexpr uint32_t     compact_workgroup_size         {64};
    // Command templates: two uvec4 per entry (index_count, first_index,
    // base_vertex, 0 and the entry's flag bits), laid out like the bounds.
    static constexpr std::size_t  template_bytes_per_entry{2 * 4 * sizeof(uint32_t)};
    // Job table: one uvec4 per job (view in bits 0..15 and filter index in
    // bits 16..31, first entry, entry count, command offset) after the
    // header uvec4 and the filter table (two uvec4 per filter, up to
    // max_job_filters).
    static constexpr std::size_t  job_bytes               {4 * sizeof(uint32_t)};
    // The frame's distinct Item_filters of the job table: the shadow
    // selection's and the color selections' (a few per view; verified).
    static constexpr std::size_t  max_job_filters         {32};
    // A job's command offset is a multiple of this: its pass bit words start
    // at command offset / 32.
    static constexpr uint32_t     job_command_alignment   {32};
    // Per job: the uint32 draw count (what the count draw reads, at the
    // job's first word) and the chunk's entries passing the filter.
    static constexpr std::size_t  count_bytes_per_job     {2 * sizeof(uint32_t)};

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
    std::size_t                                               m_view_header_offset{0};
    std::size_t                                               m_view_occlusion_header_offset{0};
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
    std::size_t                                               m_job_header_offset{0};
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

} // namespace erhe::scene_renderer
