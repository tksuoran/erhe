#pragma once

#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MTL  { class ComputePipelineState; }
namespace MTL  { class DepthStencilState; }
namespace MTL  { class RenderPipelineState; }
namespace MTL4 { class LibraryFunctionDescriptor; }

namespace erhe::graphics {

class Device_impl;
class Render_pipeline_data;
class Shader_stages;

// The Metal 4 counterpart of the Vulkan backend's encoder pipeline map: the
// pipeline states (and depth stencil states) that
// Render_command_encoder::set_render_pipeline_state and
// Compute_command_encoder::set_compute_pipeline_state bind, created once per
// distinct key and kept for the device's lifetime, so a bind every frame
// creates nothing (no allocation, no compile, no archive capture).
//
// A render key is everything the MTL4 render pipeline descriptor is built
// from: the stage function descriptors (retained by the entry, so their
// addresses cannot be reused while it is cached), the pass's color
// attachment formats and sample count, alpha-to-coverage / alpha-to-one,
// the blend state except its constant (encoder state) and the vertex input
// layout. A depth stencil key is the Depth_stencil_state plus whether the
// pass has depth / stencil. A compute key is the compute function
// descriptor.
//
// Called from the main thread (M2) and the render thread (R), under one
// mutex; a miss compiles under it (misses are rare after startup).
// clear() (Device::clear_render_pipeline_cache, after a shader reload)
// releases the entries through the device's frame-deferred completion
// handlers, as in-flight command buffers may still use them.
class Pipeline_state_cache final
{
public:
    static constexpr std::size_t max_color_attachments = 4;

    explicit Pipeline_state_cache(Device_impl& device_impl);
    ~Pipeline_state_cache() noexcept;
    Pipeline_state_cache(const Pipeline_state_cache&) = delete;
    void operator=      (const Pipeline_state_cache&) = delete;

    // Null when the shader stages have no vertex function or the pipeline
    // fails (reported through Device::shader_error).
    [[nodiscard]] auto get_render_pipeline(
        const Shader_stages&                                   shader_stages,
        const Render_pipeline_data&                            data,
        const std::array<unsigned long, max_color_attachments>& color_pixel_formats,
        unsigned long                                          sample_count
    ) -> MTL::RenderPipelineState*;

    // depth / stencil pixel formats of the pass, 0 when it has none.
    [[nodiscard]] auto get_depth_stencil_state(
        const Depth_stencil_state& depth_stencil,
        unsigned long              depth_pixel_format,
        unsigned long              stencil_pixel_format
    ) -> MTL::DepthStencilState*;

    // Null when the pipeline fails (logged).
    [[nodiscard]] auto get_compute_pipeline(const Shader_stages& shader_stages) -> MTL::ComputePipelineState*;

    void clear();

private:
    class Render_entry
    {
    public:
        MTL4::LibraryFunctionDescriptor*                 vertex_function  {nullptr};
        MTL4::LibraryFunctionDescriptor*                 fragment_function{nullptr};
        std::array<unsigned long, max_color_attachments> color_pixel_formats{};
        unsigned long                                    sample_count     {1};
        bool                                             alpha_to_coverage{false};
        bool                                             alpha_to_one     {false};
        Color_blend_state                                color_blend      {};
        bool                                             has_vertex_input {false};
        std::vector<Vertex_input_attribute>              attributes;
        std::vector<Vertex_input_binding>                bindings;
        MTL::RenderPipelineState*                        pipeline         {nullptr};
    };
    class Depth_stencil_entry
    {
    public:
        Depth_stencil_state      depth_stencil{};
        bool                     has_depth    {false};
        bool                     has_stencil  {false};
        MTL::DepthStencilState*  state        {nullptr};
    };
    class Compute_entry
    {
    public:
        MTL4::LibraryFunctionDescriptor* function{nullptr};
        MTL::ComputePipelineState*       pipeline{nullptr};
    };

    void release_all_now();

    Device_impl&                                         m_device_impl;
    std::mutex                                           m_mutex;
    std::unordered_multimap<std::size_t, Render_entry>        m_render_entries;
    std::unordered_multimap<std::size_t, Depth_stencil_entry> m_depth_stencil_entries;
    std::unordered_map<MTL4::LibraryFunctionDescriptor*, Compute_entry> m_compute_entries;
};

} // namespace erhe::graphics
