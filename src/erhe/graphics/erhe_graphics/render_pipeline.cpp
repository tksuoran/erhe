#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
#   include "erhe_graphics/gl/gl_render_pipeline.hpp"
#elif defined(ERHE_GRAPHICS_API_VULKAN)
#   include "erhe_graphics/vulkan/vulkan_render_pipeline.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
#   include "erhe_graphics/metal/metal_render_pipeline.hpp"
#elif defined(ERHE_GRAPHICS_API_NONE)
#   include "erhe_graphics/null/null_render_pipeline.hpp"
#endif

namespace erhe::graphics {

void Render_pipeline_create_info::set_format_from_render_pass(const Render_pass_descriptor& desc)
{
    color_attachment_count = 0;
    color_attachment_formats.fill(erhe::dataformat::Format::format_undefined);
    depth_attachment_format   = erhe::dataformat::Format::format_undefined;
    stencil_attachment_format = erhe::dataformat::Format::format_undefined;
    sample_count = 1;

    // Swapchain render passes: color/depth formats come from the swapchain, not textures
    if (desc.swapchain != nullptr) {
        color_attachment_formats[0] = desc.swapchain->get_color_format();
        color_attachment_count = 1;
        color_usage_before[0] = desc.color_attachments[0].usage_before;
        color_usage_after [0] = desc.color_attachments[0].usage_after;
        erhe::dataformat::Format depth_fmt = desc.swapchain->get_depth_format();
        if (depth_fmt != erhe::dataformat::Format::format_undefined) {
            depth_attachment_format = depth_fmt;
            depth_usage_before     = desc.depth_attachment.usage_before;
            depth_usage_after      = desc.depth_attachment.usage_after;
        }
        // Swapchain depth may also come from an explicit depth attachment texture
        if (desc.depth_attachment.is_defined() && (desc.depth_attachment.texture != nullptr)) {
            depth_attachment_format = desc.depth_attachment.texture->get_pixelformat();
            depth_usage_before     = desc.depth_attachment.usage_before;
            depth_usage_after      = desc.depth_attachment.usage_after;
        }
        // Stencil format must be picked up too -- Metal validates that the
        // pipeline's stencil pixel format matches the framebuffer's, and a
        // swapchain pass that uses a depth+stencil format texture (e.g.
        // d32_sfloat_s8_uint) will fail validation if the pipeline has
        // stencil_attachment_format == undefined.
        if (desc.stencil_attachment.is_defined() && (desc.stencil_attachment.texture != nullptr)) {
            stencil_attachment_format = desc.stencil_attachment.texture->get_pixelformat();
        }
        return;
    }

    // Off-screen render passes: formats come from attachment textures
    for (std::size_t i = 0; i < desc.color_attachments.size(); ++i) {
        const Render_pass_attachment_descriptor& att = desc.color_attachments[i];
        if (att.is_defined() && (att.texture != nullptr)) {
            color_usage_before[color_attachment_count] = att.usage_before;
            color_usage_after [color_attachment_count] = att.usage_after;
            color_attachment_formats[color_attachment_count] = att.texture->get_pixelformat();
            ++color_attachment_count;
        }
    }

    if (desc.depth_attachment.is_defined() && (desc.depth_attachment.texture != nullptr)) {
        depth_usage_before     = desc.depth_attachment.usage_before;
        depth_usage_after      = desc.depth_attachment.usage_after;
        depth_attachment_format = desc.depth_attachment.texture->get_pixelformat();
        const int tex_sample_count = desc.depth_attachment.texture->get_sample_count();
        if (tex_sample_count > 0) {
            sample_count = static_cast<unsigned int>(tex_sample_count);
        }
    }
    if (desc.stencil_attachment.is_defined() && (desc.stencil_attachment.texture != nullptr)) {
        stencil_attachment_format = desc.stencil_attachment.texture->get_pixelformat();
    }

    // Get sample count from color attachments if not set from depth
    if (sample_count == 1) {
        for (std::size_t i = 0; i < desc.color_attachments.size(); ++i) {
            const Render_pass_attachment_descriptor& att = desc.color_attachments[i];
            if (att.is_defined() && (att.texture != nullptr)) {
                const int tex_sample_count = att.texture->get_sample_count();
                if (tex_sample_count > 1) {
                    sample_count = static_cast<unsigned int>(tex_sample_count);
                    break;
                }
            }
        }
    }
}

// --- Render_pipeline ---

Render_pipeline::Render_pipeline(Device& device, const Render_pipeline_create_info& create_info)
    : m_create_info{create_info}
    , m_impl       {std::make_unique<Render_pipeline_impl>(device, create_info)}
{
}

Render_pipeline::~Render_pipeline() noexcept = default;

Render_pipeline::Render_pipeline(Render_pipeline&& other) noexcept = default;

auto Render_pipeline::operator=(Render_pipeline&& other) noexcept -> Render_pipeline& = default;

auto Render_pipeline::get_impl() -> Render_pipeline_impl&
{
    return *m_impl;
}

auto Render_pipeline::get_impl() const -> const Render_pipeline_impl&
{
    return *m_impl;
}

auto Render_pipeline::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_create_info.base.debug_label;
}

auto Render_pipeline::is_valid() const -> bool
{
    return (m_impl != nullptr) && m_impl->is_valid();
}

auto Render_pipeline::get_create_info() const -> const Render_pipeline_create_info&
{
    return m_create_info;
}

// --- Lazy_render_pipeline ---

namespace {

auto compute_format_hash(const Render_pipeline_create_info& ci) -> std::size_t
{
    auto hash_combine = [](std::size_t& seed, std::size_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };
    std::size_t hash = 0;
    hash_combine(hash, static_cast<std::size_t>(ci.color_attachment_count));
    for (unsigned int i = 0; i < ci.color_attachment_count; ++i) {
        hash_combine(hash, static_cast<std::size_t>(ci.color_attachment_formats[i]));
        hash_combine(hash, static_cast<std::size_t>(ci.color_usage_before[i]));
        hash_combine(hash, static_cast<std::size_t>(ci.color_usage_after[i]));
    }
    hash_combine(hash, static_cast<std::size_t>(ci.depth_attachment_format));
    hash_combine(hash, static_cast<std::size_t>(ci.stencil_attachment_format));
    hash_combine(hash, static_cast<std::size_t>(ci.sample_count));
    hash_combine(hash, static_cast<std::size_t>(ci.depth_usage_before));
    hash_combine(hash, static_cast<std::size_t>(ci.depth_usage_after));
    return hash;
}

} // anonymous namespace

Lazy_render_pipeline::Lazy_render_pipeline() = default;

Lazy_render_pipeline::Lazy_render_pipeline(Device& device, const Base_render_pipeline_create_info& create_info)
    : data    {std::move(create_info)}
    , m_device{&device}
{
    // These are handled later by get_pipeline_for()
    //ERHE_VERIFY(data.shader_stages = nullptr);
    //ERHE_VERIFY(data.vertex_input  == nullptr);
    //ERHE_VERIFY(data.vertex_format == nullptr);
}

Lazy_render_pipeline::~Lazy_render_pipeline() noexcept = default;

Lazy_render_pipeline::Lazy_render_pipeline(Lazy_render_pipeline&& other) noexcept
    : data      {std::move(other.data)}
    , m_device  {other.m_device}
    , m_variants{std::move(other.m_variants)}
{
    other.m_device = nullptr;
}

auto Lazy_render_pipeline::operator=(Lazy_render_pipeline&& other) noexcept -> Lazy_render_pipeline&
{
    if (this != &other) {
        data       = std::move(other.data);
        m_device   = other.m_device;
        m_variants = std::move(other.m_variants);
        other.m_device = nullptr;
    }
    return *this;
}

auto Lazy_render_pipeline::get_pipeline_for(
    const Render_pass_descriptor&          render_pass_desc,
    const Shader_stages*                   shader_stages,
    const Vertex_input_state*              vertex_input,
    const erhe::dataformat::Vertex_format* vertex_format
) -> Render_pipeline*
{
    if (m_device == nullptr) {
        return nullptr;
    }

    // Build a create info with format info from the render pass
    Render_pipeline_create_info ci{
        .base          = data,
        .shader_stages = shader_stages,
        .vertex_input  = vertex_input,
        .vertex_format = vertex_format
    };
    ci.set_format_from_render_pass(render_pass_desc);

    const std::size_t hash = compute_format_hash(ci);

    {
        std::lock_guard<std::mutex> lock{m_mutex};
        auto it = m_variants.find(hash);
        if (it != m_variants.end()) {
            return it->second.get();
        }
    }

    // Create new variant
    auto pipeline = std::make_unique<Render_pipeline>(*m_device, ci);
    if (!pipeline->is_valid()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock{m_mutex};
    auto [it, inserted] = m_variants.emplace(hash, std::move(pipeline));
    return it->second.get();
}

auto Lazy_render_pipeline::get_create_info() const -> const Base_render_pipeline_create_info&
{
    return data;
}

} // namespace erhe::graphics
