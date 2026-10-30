#pragma once

#include "erhe_scene_renderer/pvs_disocclusion.hpp"

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Gpu_timer;
    class Reloadable_shader_stages;
}

namespace erhe::scene_renderer {


// The device-local work buffers of one face's disocclusion passes
// (pvs_disocclusion.hpp; res/shaders/erhe_pvs_disocclusion.glsl), sized for
// one (resolution, tile size, layer count): the closed flag per tile, the
// flat column masks, the tile masks, the lists (per layer the cone counter
// and the cones), the disoccluded map and the columns' runs. Reused by every face; the passes order
// their own accesses (Pvs_disocclusion_pipelines::record_face()).
class Pvs_disocclusion_buffers
{
public:
    Pvs_disocclusion_buffers(erhe::graphics::Device& graphics_device, uint32_t resolution, uint32_t tile_size, uint32_t layer_count);
    ~Pvs_disocclusion_buffers() noexcept;
    Pvs_disocclusion_buffers(const Pvs_disocclusion_buffers&)            = delete;
    Pvs_disocclusion_buffers& operator=(const Pvs_disocclusion_buffers&) = delete;

    [[nodiscard]] auto get_tile_count       () const -> uint32_t { return m_tile_count; }
    [[nodiscard]] auto get_tiles_per_layer  () const -> uint32_t { return m_tiles_per_layer; }
    [[nodiscard]] auto get_closure          () const -> const erhe::graphics::Buffer& { return *m_closure; }
    [[nodiscard]] auto get_column_masks     () const -> const erhe::graphics::Buffer& { return *m_column_masks; }
    [[nodiscard]] auto get_tile_masks       () const -> const erhe::graphics::Buffer& { return *m_tile_masks; }
    [[nodiscard]] auto get_lists            () const -> const erhe::graphics::Buffer& { return *m_lists; }
    [[nodiscard]] auto get_map              () const -> const erhe::graphics::Buffer& { return *m_map; }
    [[nodiscard]] auto get_map_bytes        () const -> std::size_t { return m_map_bytes; }
    [[nodiscard]] auto get_runs      () const -> const erhe::graphics::Buffer& { return *m_runs; }
    [[nodiscard]] auto get_runs_bytes() const -> std::size_t { return m_runs_bytes; }
    // The lists' header: one cone counter per layer.
    static constexpr std::size_t lists_header_words{64};
    // Byte counts: tile count, tiles per layer * 2, tile count * 2, and
    // lists_header_words + tile count * 2 uint words.
    [[nodiscard]] auto get_closure_bytes     () const -> std::size_t;
    [[nodiscard]] auto get_column_masks_bytes() const -> std::size_t;
    [[nodiscard]] auto get_tile_masks_bytes  () const -> std::size_t;
    [[nodiscard]] auto get_lists_bytes       () const -> std::size_t;

private:
    uint32_t                                m_tile_count     {0};
    uint32_t                                m_tiles_per_layer{0};
    std::unique_ptr<erhe::graphics::Buffer> m_closure;
    std::unique_ptr<erhe::graphics::Buffer> m_column_masks;
    std::unique_ptr<erhe::graphics::Buffer> m_tile_masks;
    std::unique_ptr<erhe::graphics::Buffer> m_lists;
    std::unique_ptr<erhe::graphics::Buffer> m_map;
    std::size_t                             m_map_bytes{0};
    std::unique_ptr<erhe::graphics::Buffer> m_runs;
    std::size_t                             m_runs_bytes{0};
};

// Optional GPU timers of the passes of one face (explicit-range
// timers, each begun and ended at most once per frame by the caller's
// choice of the timed face).
class Pvs_disocclusion_timers
{
public:
    erhe::graphics::Gpu_timer* tiles       {nullptr};
    erhe::graphics::Gpu_timer* layer_loop  {nullptr}; // classify + cones, all layers
    erhe::graphics::Gpu_timer* map         {nullptr}; // the disoccluded map, all levels
    erhe::graphics::Gpu_timer* vote        {nullptr}; // the bounds vote
};

// The compute pipelines of the potentially visible set bake's disocclusion
// passes (doc/plans/pvs.md "Bake" 5; precedent Light_cluster_gpu_binner):
// res/shaders/pvs_tiles.comp, pvs_stencil.comp (classify), pvs_cones.comp,
// pvs_map.comp (level 0 and the reductions), pvs_vote.comp (the bounds
// vote) and pvs_trace.comp with their interface blocks, one bind group
// layout shared by all, and the ring buffer client of the per-dispatch
// parameter blocks. Every pass has a workgroup of 64 invocations (the
// largest layer count) and walks its items grid-stride, so no dispatch
// exceeds the device's workgroup count limit.
class Pvs_disocclusion_pipelines
{
public:
    static constexpr unsigned int parameters_binding_point  {0};
    static constexpr unsigned int layered_binding_point     {1};
    static constexpr unsigned int closure_binding_point     {2};
    static constexpr unsigned int column_masks_binding_point{3};
    static constexpr unsigned int tile_masks_binding_point  {4};
    static constexpr unsigned int lists_binding_point       {5};
    static constexpr unsigned int column_words_binding_point{6};
    static constexpr unsigned int trace_binding_point       {7};
    static constexpr unsigned int column_bounds_binding_point{8};
    static constexpr unsigned int map_binding_point         {9};
    static constexpr unsigned int runs_binding_point        {10};
    static constexpr uint32_t     workgroup_size            {64};

    Pvs_disocclusion_pipelines(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path);
    ~Pvs_disocclusion_pipelines() noexcept;
    Pvs_disocclusion_pipelines(const Pvs_disocclusion_pipelines&)            = delete;
    Pvs_disocclusion_pipelines& operator=(const Pvs_disocclusion_pipelines&) = delete;

    // Every pipeline built.
    [[nodiscard]] auto is_supported() const -> bool;

    // Render phase, outside any render pass: the passes of one face over
    // layered_buffer (the Pvs_layered_interface layout, layered_byte_count
    // bytes; the caller orders its raster writes before these compute
    // reads), with parameters (made for buffers' sizes), OR-ing the bit of
    // every column the bounds vote finds visible on the face into
    // column_words (column word count words at offset 0 of a storage
    // buffer; the caller clears it and orders the clear before the first
    // face). column_bounds: two vec4 per column (world min, max), constant
    // for the bake. Orders the work buffers' accesses of earlier faces and
    // frames before this face's, and earlier faces' column word writes
    // before this face's; the caller orders this face's column word writes
    // before its own reads. timers: optional, see Pvs_disocclusion_timers.
    void record_face(
        erhe::graphics::Command_buffer&    command_buffer,
        Pvs_disocclusion_buffers&          buffers,
        const erhe::graphics::Buffer&      layered_buffer,
        std::size_t                        layered_byte_count,
        const Pvs_disocclusion_parameters& parameters,
        const erhe::graphics::Buffer&      column_bounds,
        std::size_t                        column_bounds_byte_count,
        const erhe::graphics::Buffer&      column_words,
        const Pvs_disocclusion_timers*     timers
    );

    // The bake trace (doc/plans/pvs.md "Bake" 7), after record_face() of
    // the same face: res/shaders/pvs_trace.comp counts, per traced entry,
    // the face's samples naming it, those inside the core and those of them
    // in a disoccluded (tile, layer), into trace_buffer (pvs_trace.comp's
    // layout, trace_byte_count bytes; the caller writes its table and zeroes
    // its counters, and orders its own reads after this).
    void record_trace(
        erhe::graphics::Command_buffer&    command_buffer,
        Pvs_disocclusion_buffers&          buffers,
        const erhe::graphics::Buffer&      layered_buffer,
        std::size_t                        layered_byte_count,
        const Pvs_disocclusion_parameters& parameters,
        const erhe::graphics::Buffer&      trace_buffer,
        std::size_t                        trace_byte_count
    );

private:
    enum class Pass : unsigned int {
        tiles = 0,
        stencil,
        cones,
        map_level_0,
        map_reduce,
        vote,
        trace,
        count
    };

    void dispatch(
        erhe::graphics::Command_buffer&           command_buffer,
        Pass                                      pass,
        uint32_t                                  workgroups,
        const erhe::graphics::Ring_buffer_range&  parameter_range,
        Pvs_disocclusion_buffers&                 buffers,
        const erhe::graphics::Buffer&             layered_buffer,
        std::size_t                               layered_byte_count,
        const erhe::graphics::Buffer&             column_bounds,
        std::size_t                               column_bounds_byte_count,
        const erhe::graphics::Buffer&             column_words,
        std::size_t                               column_words_byte_count,
        const erhe::graphics::Buffer&             trace_buffer,
        std::size_t                               trace_byte_count,
        erhe::graphics::Gpu_timer*                timer
    );

    erhe::graphics::Device&                                   m_graphics_device;
    erhe::graphics::Shader_resource                           m_parameters_block;
    erhe::graphics::Shader_resource                           m_layered_block;
    erhe::graphics::Shader_resource                           m_closure_block;
    erhe::graphics::Shader_resource                           m_column_masks_block;
    erhe::graphics::Shader_resource                           m_tile_masks_block;
    erhe::graphics::Shader_resource                           m_lists_block;
    erhe::graphics::Shader_resource                           m_column_words_block;
    erhe::graphics::Shader_resource                           m_trace_block;
    erhe::graphics::Shader_resource                           m_column_bounds_block;
    erhe::graphics::Shader_resource                           m_map_block;
    erhe::graphics::Shader_resource                           m_runs_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_bind_group_layout;
    std::array<std::unique_ptr<erhe::graphics::Reloadable_shader_stages>, static_cast<std::size_t>(Pass::count)> m_shader_stages;
    std::array<std::unique_ptr<erhe::graphics::Compute_pipeline>,         static_cast<std::size_t>(Pass::count)> m_pipelines;
    erhe::graphics::Ring_buffer_client                        m_parameter_buffer;
    // record_face() scratch (cleared, capacity kept).
    std::vector<erhe::graphics::Ring_buffer_range>            m_parameter_ranges;
    Pvs_disocclusion_parameters                               m_layer_parameters;
};

} // namespace erhe::scene_renderer
