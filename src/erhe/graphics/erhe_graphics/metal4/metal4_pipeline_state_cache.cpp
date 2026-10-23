#include "erhe_graphics/metal4/metal4_pipeline_state_cache.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_helpers.hpp"
#include "erhe_graphics/metal4/metal4_pipeline_compiler.hpp"
#include "erhe_graphics/metal4/metal4_shader_stages.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <Metal/Metal.hpp>

#include <algorithm>
#include <functional>

namespace erhe::graphics {

namespace {

void hash_combine(std::size_t& seed, const std::size_t value)
{
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
}

void hash_blend_component(std::size_t& seed, const Blend_state_component& component)
{
    hash_combine(seed, static_cast<std::size_t>(component.equation_mode));
    hash_combine(seed, static_cast<std::size_t>(component.source_factor));
    hash_combine(seed, static_cast<std::size_t>(component.destination_factor));
}

// The blend state without its constant (encoder state, setBlendColor).
auto blend_matches(const Color_blend_state& lhs, const Color_blend_state& rhs) -> bool
{
    return
        (lhs.enabled          == rhs.enabled         ) &&
        (lhs.rgb              == rhs.rgb             ) &&
        (lhs.alpha            == rhs.alpha           ) &&
        (lhs.write_mask.red   == rhs.write_mask.red  ) &&
        (lhs.write_mask.green == rhs.write_mask.green) &&
        (lhs.write_mask.blue  == rhs.write_mask.blue ) &&
        (lhs.write_mask.alpha == rhs.write_mask.alpha);
}

auto attributes_match(const std::vector<Vertex_input_attribute>& lhs, const std::vector<Vertex_input_attribute>& rhs) -> bool
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0, end = lhs.size(); i < end; ++i) {
        if (
            (lhs[i].layout_location != rhs[i].layout_location) ||
            (lhs[i].binding         != rhs[i].binding        ) ||
            (lhs[i].format          != rhs[i].format         ) ||
            (lhs[i].offset          != rhs[i].offset         )
        ) {
            return false;
        }
    }
    return true;
}

auto bindings_match(const std::vector<Vertex_input_binding>& lhs, const std::vector<Vertex_input_binding>& rhs) -> bool
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0, end = lhs.size(); i < end; ++i) {
        if (
            (lhs[i].binding != rhs[i].binding) ||
            (lhs[i].stride  != rhs[i].stride ) ||
            (lhs[i].divisor != rhs[i].divisor)
        ) {
            return false;
        }
    }
    return true;
}

auto make_vertex_descriptor(const Vertex_input_state_data& vis_data) -> MTL::VertexDescriptor*
{
    MTL::VertexDescriptor* vertex_desc = MTL::VertexDescriptor::alloc()->init();
    for (const Vertex_input_attribute& attr : vis_data.attributes) {
        MTL::VertexAttributeDescriptor* attr_desc = vertex_desc->attributes()->object(static_cast<NS::UInteger>(attr.layout_location));
        attr_desc->setFormat(to_mtl_vertex_format(attr.format));
        attr_desc->setOffset(static_cast<NS::UInteger>(attr.offset));
        attr_desc->setBufferIndex(static_cast<NS::UInteger>(attr.binding) + vertex_buffer_index_offset);
    }
    for (const Vertex_input_binding& binding : vis_data.bindings) {
        MTL::VertexBufferLayoutDescriptor* layout_desc = vertex_desc->layouts()->object(static_cast<NS::UInteger>(binding.binding) + vertex_buffer_index_offset);
        layout_desc->setStride(static_cast<NS::UInteger>(binding.stride));
        if (binding.divisor == 0) {
            layout_desc->setStepFunction(MTL::VertexStepFunctionPerVertex);
        } else {
            layout_desc->setStepFunction(MTL::VertexStepFunctionPerInstance);
            layout_desc->setStepRate(static_cast<NS::UInteger>(binding.divisor));
        }
    }
    return vertex_desc;
}

} // anonymous namespace

Pipeline_state_cache::Pipeline_state_cache(Device_impl& device_impl)
    : m_device_impl{device_impl}
{
}

Pipeline_state_cache::~Pipeline_state_cache() noexcept
{
    // ~Device_impl destroys the cache after the GPU is idle.
    release_all_now();
}

void Pipeline_state_cache::release_all_now()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    for (auto& [hash, entry] : m_render_entries) {
        static_cast<void>(hash);
        entry.pipeline->release();
        entry.vertex_function->release();
        if (entry.fragment_function != nullptr) {
            entry.fragment_function->release();
        }
    }
    for (auto& [hash, entry] : m_depth_stencil_entries) {
        static_cast<void>(hash);
        entry.state->release();
    }
    for (auto& [function, entry] : m_compute_entries) {
        static_cast<void>(function);
        entry.pipeline->release();
        entry.function->release();
    }
    m_render_entries.clear();
    m_depth_stencil_entries.clear();
    m_compute_entries.clear();
}

void Pipeline_state_cache::clear()
{
    std::unordered_multimap<std::size_t, Render_entry>                  render_entries;
    std::unordered_multimap<std::size_t, Depth_stencil_entry>           depth_stencil_entries;
    std::unordered_map<MTL4::LibraryFunctionDescriptor*, Compute_entry> compute_entries;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        render_entries.swap(m_render_entries);
        depth_stencil_entries.swap(m_depth_stencil_entries);
        compute_entries.swap(m_compute_entries);
    }
    if (render_entries.empty() && depth_stencil_entries.empty() && compute_entries.empty()) {
        return;
    }
    // Command buffers of frames in flight may still use them.
    m_device_impl.add_completion_handler(
        [
            render_entries        = std::move(render_entries),
            depth_stencil_entries = std::move(depth_stencil_entries),
            compute_entries       = std::move(compute_entries)
        ](Device_impl&) {
            for (const auto& [hash, entry] : render_entries) {
                static_cast<void>(hash);
                entry.pipeline->release();
                entry.vertex_function->release();
                if (entry.fragment_function != nullptr) {
                    entry.fragment_function->release();
                }
            }
            for (const auto& [hash, entry] : depth_stencil_entries) {
                static_cast<void>(hash);
                entry.state->release();
            }
            for (const auto& [function, entry] : compute_entries) {
                static_cast<void>(function);
                entry.pipeline->release();
                entry.function->release();
            }
        }
    );
}

auto Pipeline_state_cache::get_render_pipeline(
    const Shader_stages&                                    shader_stages,
    const Render_pipeline_data&                             data,
    const std::array<unsigned long, max_color_attachments>& color_pixel_formats,
    const unsigned long                                     sample_count
) -> MTL::RenderPipelineState*
{
    const Shader_stages_impl& stages_impl = shader_stages.get_impl();
    MTL4::LibraryFunctionDescriptor* const vertex_function   = stages_impl.get_vertex_function();
    MTL4::LibraryFunctionDescriptor* const fragment_function = stages_impl.get_fragment_function();
    if (vertex_function == nullptr) {
        return nullptr;
    }
    const Vertex_input_state_data* const vertex_input = (data.vertex_input != nullptr) ? &data.vertex_input->get_data() : nullptr;

    std::size_t hash = 0;
    hash_combine(hash, std::hash<const void*>{}(vertex_function));
    hash_combine(hash, std::hash<const void*>{}(fragment_function));
    for (const unsigned long format : color_pixel_formats) {
        hash_combine(hash, static_cast<std::size_t>(format));
    }
    hash_combine(hash, static_cast<std::size_t>(sample_count));
    hash_combine(hash, static_cast<std::size_t>(data.multisample.alpha_to_coverage_enable));
    hash_combine(hash, static_cast<std::size_t>(data.multisample.alpha_to_one_enable));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.enabled));
    hash_blend_component(hash, data.color_blend.rgb);
    hash_blend_component(hash, data.color_blend.alpha);
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.write_mask.red));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.write_mask.green));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.write_mask.blue));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.write_mask.alpha));
    hash_combine(hash, static_cast<std::size_t>(vertex_input != nullptr));
    if (vertex_input != nullptr) {
        for (const Vertex_input_attribute& attr : vertex_input->attributes) {
            hash_combine(hash, static_cast<std::size_t>(attr.layout_location));
            hash_combine(hash, attr.binding);
            hash_combine(hash, static_cast<std::size_t>(attr.format));
            hash_combine(hash, attr.offset);
        }
        for (const Vertex_input_binding& binding : vertex_input->bindings) {
            hash_combine(hash, binding.binding);
            hash_combine(hash, binding.stride);
            hash_combine(hash, static_cast<std::size_t>(binding.divisor));
        }
    }

    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto [begin, end] = m_render_entries.equal_range(hash);
    for (auto i = begin; i != end; ++i) {
        const Render_entry& entry = i->second;
        if (
            (entry.vertex_function     == vertex_function) &&
            (entry.fragment_function   == fragment_function) &&
            (entry.color_pixel_formats == color_pixel_formats) &&
            (entry.sample_count        == sample_count) &&
            (entry.alpha_to_coverage   == data.multisample.alpha_to_coverage_enable) &&
            (entry.alpha_to_one        == data.multisample.alpha_to_one_enable) &&
            blend_matches(entry.color_blend, data.color_blend) &&
            (entry.has_vertex_input    == (vertex_input != nullptr)) &&
            ((vertex_input == nullptr) || (attributes_match(entry.attributes, vertex_input->attributes) && bindings_match(entry.bindings, vertex_input->bindings)))
        ) {
            return entry.pipeline;
        }
    }

    // Miss: build the descriptor and compile.
    Scoped_transient_object_pool object_pool{}; // drains the label string

    // No depth / stencil pixel formats in MTL4::RenderPipelineDescriptor
    // (doc/erhe/metal4_backend.md "Shaders and pipelines").
    MTL4::RenderPipelineDescriptor* desc = MTL4::RenderPipelineDescriptor::alloc()->init();
    desc->setLabel(NS::String::string(shader_stages.name().c_str(), NS::UTF8StringEncoding));
    desc->setVertexFunctionDescriptor(vertex_function);
    desc->setFragmentFunctionDescriptor(fragment_function);

    // Color attachment pixel formats - must match the render pass. Slot 0
    // is always written (may be Invalid for depth-only).
    unsigned int active_color_attachment_count = 0;
    const unsigned long invalid_format = static_cast<unsigned long>(MTL::PixelFormatInvalid);
    for (unsigned int i = 0; i < max_color_attachments; ++i) {
        const unsigned long color_format = color_pixel_formats[i];
        if ((color_format != invalid_format) || (i == 0)) {
            desc->colorAttachments()->object(static_cast<NS::UInteger>(i))->setPixelFormat(static_cast<MTL::PixelFormat>(color_format));
        }
        if (color_format != invalid_format) {
            active_color_attachment_count = i + 1;
        }
    }
    if (sample_count > 1) {
        desc->setRasterSampleCount(static_cast<NS::UInteger>(sample_count));
    }
    desc->setAlphaToCoverageState(data.multisample.alpha_to_coverage_enable ? MTL4::AlphaToCoverageStateEnabled : MTL4::AlphaToCoverageStateDisabled);
    desc->setAlphaToOneState     (data.multisample.alpha_to_one_enable      ? MTL4::AlphaToOneStateEnabled      : MTL4::AlphaToOneStateDisabled);
    if (vertex_input != nullptr) {
        MTL::VertexDescriptor* const vertex_desc = make_vertex_descriptor(*vertex_input);
        desc->setVertexDescriptor(vertex_desc);
        vertex_desc->release();
    }

    // Color blend state - applied to all active color attachments
    {
        MTL::ColorWriteMask mask = MTL::ColorWriteMaskNone;
        if (data.color_blend.write_mask.red)   mask |= MTL::ColorWriteMaskRed;
        if (data.color_blend.write_mask.green) mask |= MTL::ColorWriteMaskGreen;
        if (data.color_blend.write_mask.blue)  mask |= MTL::ColorWriteMaskBlue;
        if (data.color_blend.write_mask.alpha) mask |= MTL::ColorWriteMaskAlpha;
        const unsigned int attachment_loop_count = std::max(1u, active_color_attachment_count);
        for (unsigned int i = 0; i < attachment_loop_count; ++i) {
            MTL4::RenderPipelineColorAttachmentDescriptor* color_att = desc->colorAttachments()->object(static_cast<NS::UInteger>(i));
            if (data.color_blend.enabled) {
                color_att->setBlendingState(MTL4::BlendStateEnabled);
                color_att->setSourceRGBBlendFactor(to_mtl_blend_factor(data.color_blend.rgb.source_factor));
                color_att->setDestinationRGBBlendFactor(to_mtl_blend_factor(data.color_blend.rgb.destination_factor));
                color_att->setRgbBlendOperation(to_mtl_blend_operation(data.color_blend.rgb.equation_mode));
                color_att->setSourceAlphaBlendFactor(to_mtl_blend_factor(data.color_blend.alpha.source_factor));
                color_att->setDestinationAlphaBlendFactor(to_mtl_blend_factor(data.color_blend.alpha.destination_factor));
                color_att->setAlphaBlendOperation(to_mtl_blend_operation(data.color_blend.alpha.equation_mode));
            }
            color_att->setWriteMask(mask);
        }
    }

    std::string error;
    MTL::RenderPipelineState* const pipeline = m_device_impl.get_pipeline_compiler().new_render_pipeline(desc, error);
    desc->release();
    if (pipeline == nullptr) {
        m_device_impl.get_device().shader_error(fmt::format("Pipeline state creation failed for '{}': {}", shader_stages.name(), error), "");
        return nullptr;
    }

    Render_entry entry{
        .vertex_function     = vertex_function,
        .fragment_function   = fragment_function,
        .color_pixel_formats = color_pixel_formats,
        .sample_count        = sample_count,
        .alpha_to_coverage   = data.multisample.alpha_to_coverage_enable,
        .alpha_to_one        = data.multisample.alpha_to_one_enable,
        .color_blend         = data.color_blend,
        .has_vertex_input    = (vertex_input != nullptr),
        .attributes          = (vertex_input != nullptr) ? vertex_input->attributes : std::vector<Vertex_input_attribute>{},
        .bindings            = (vertex_input != nullptr) ? vertex_input->bindings   : std::vector<Vertex_input_binding>{},
        .pipeline            = pipeline
    };
    vertex_function->retain();
    if (fragment_function != nullptr) {
        fragment_function->retain();
    }
    m_render_entries.emplace(hash, std::move(entry));
    return pipeline;
}

auto Pipeline_state_cache::get_depth_stencil_state(
    const Depth_stencil_state& depth_stencil,
    const unsigned long        depth_pixel_format,
    const unsigned long        stencil_pixel_format
) -> MTL::DepthStencilState*
{
    const bool has_depth   = (depth_pixel_format   != 0);
    const bool has_stencil = (stencil_pixel_format != 0);
    std::size_t hash = Depth_stencil_state_hash{}(depth_stencil);
    hash_combine(hash, static_cast<std::size_t>(has_depth));
    hash_combine(hash, static_cast<std::size_t>(has_stencil));

    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto [begin, end] = m_depth_stencil_entries.equal_range(hash);
    for (auto i = begin; i != end; ++i) {
        const Depth_stencil_entry& entry = i->second;
        if ((entry.has_depth == has_depth) && (entry.has_stencil == has_stencil) && (entry.depth_stencil == depth_stencil)) {
            return entry.state;
        }
    }

    MTL::DepthStencilDescriptor* ds_desc = MTL::DepthStencilDescriptor::alloc()->init();
    if (has_depth && depth_stencil.depth_write_enable) {
        ds_desc->setDepthWriteEnabled(true);
    }
    if (has_depth && depth_stencil.depth_test_enable) {
        ds_desc->setDepthCompareFunction(to_mtl_compare_function(depth_stencil.depth_compare_op));
    } else {
        ds_desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
    }
    if (has_stencil && depth_stencil.stencil_test_enable) {
        auto configure_stencil = [](MTL::StencilDescriptor* stencil_desc, const Stencil_op_state& state) {
            stencil_desc->setStencilCompareFunction(to_mtl_compare_function(state.function));
            stencil_desc->setStencilFailureOperation(to_mtl_stencil_operation(state.stencil_fail_op));
            stencil_desc->setDepthFailureOperation(to_mtl_stencil_operation(state.z_fail_op));
            stencil_desc->setDepthStencilPassOperation(to_mtl_stencil_operation(state.z_pass_op));
            stencil_desc->setReadMask(state.test_mask);
            stencil_desc->setWriteMask(state.write_mask);
        };
        configure_stencil(ds_desc->frontFaceStencil(), depth_stencil.stencil_front);
        configure_stencil(ds_desc->backFaceStencil(),  depth_stencil.stencil_back);
    }
    MTL::DepthStencilState* const state = m_device_impl.get_mtl_device()->newDepthStencilState(ds_desc);
    ds_desc->release();
    if (state == nullptr) {
        return nullptr;
    }
    m_depth_stencil_entries.emplace(
        hash,
        Depth_stencil_entry{.depth_stencil = depth_stencil, .has_depth = has_depth, .has_stencil = has_stencil, .state = state}
    );
    return state;
}

auto Pipeline_state_cache::get_compute_pipeline(const Shader_stages& shader_stages) -> MTL::ComputePipelineState*
{
    MTL4::LibraryFunctionDescriptor* const function = shader_stages.get_impl().get_compute_function();
    ERHE_VERIFY(function != nullptr);

    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_compute_entries.find(function);
    if (i != m_compute_entries.end()) {
        return i->second.pipeline;
    }

    Scoped_transient_object_pool object_pool{}; // drains the label string
    MTL4::ComputePipelineDescriptor* const descriptor = MTL4::ComputePipelineDescriptor::alloc()->init();
    descriptor->setLabel(NS::String::string(shader_stages.name().c_str(), NS::UTF8StringEncoding));
    descriptor->setComputeFunctionDescriptor(function);
    std::string error;
    MTL::ComputePipelineState* const pipeline = m_device_impl.get_pipeline_compiler().new_compute_pipeline(descriptor, error);
    descriptor->release();
    if (pipeline == nullptr) {
        log_startup->error("Metal compute pipeline state creation failed for '{}': {}", shader_stages.name(), error);
        return nullptr;
    }
    function->retain();
    m_compute_entries.emplace(function, Compute_entry{.function = function, .pipeline = pipeline});
    return pipeline;
}

} // namespace erhe::graphics
