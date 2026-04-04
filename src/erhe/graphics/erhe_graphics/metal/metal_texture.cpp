#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_helpers.hpp"
#include "erhe_graphics/device.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>

namespace erhe::graphics {

Texture_impl::Texture_impl(Texture_impl&& other) noexcept
    : m_type                  {other.m_type}
    , m_pixelformat           {other.m_pixelformat}
    , m_fixed_sample_locations{other.m_fixed_sample_locations}
    , m_is_sparse             {other.m_is_sparse}
    , m_sample_count          {other.m_sample_count}
    , m_width                 {other.m_width}
    , m_height                {other.m_height}
    , m_depth                 {other.m_depth}
    , m_array_layer_count     {other.m_array_layer_count}
    , m_level_count           {other.m_level_count}
    , m_debug_label           {other.m_debug_label}
    , m_mtl_texture           {other.m_mtl_texture}
{
    other.m_mtl_texture = nullptr;
}

Texture_impl::~Texture_impl() noexcept
{
    if (m_mtl_texture != nullptr) {
        m_mtl_texture->release();
        m_mtl_texture = nullptr;
    }
}

Texture_impl::Texture_impl(Device& device, const Texture_create_info& create_info)
    : m_type                  {create_info.type}
    , m_pixelformat           {create_info.pixelformat}
    , m_fixed_sample_locations{create_info.fixed_sample_locations}
    , m_is_sparse             {create_info.sparse}
    , m_sample_count          {create_info.sample_count}
    , m_width                 {create_info.width}
    , m_height                {create_info.height}
    , m_depth                 {create_info.depth}
    , m_array_layer_count     {create_info.array_layer_count}
    , m_level_count           {
        (create_info.level_count != 0)
            ? create_info.level_count
            : create_info.get_texture_level_count()
    }
    , m_debug_label           {create_info.debug_label}
{
    Device_impl& device_impl = device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    if (mtl_device == nullptr) {
        return;
    }

    MTL::PixelFormat pixel_format = to_mtl_pixel_format(m_pixelformat);
    if (pixel_format == MTL::PixelFormatInvalid) {
        return;
    }

    // Texture view: create from existing texture's mip level / array layer range
    if (create_info.view_source) {
        MTL::Texture* source_mtl_texture = create_info.view_source->get_impl().get_mtl_texture();
        if (source_mtl_texture != nullptr) {
            NS::Range level_range = NS::Range::Make(
                static_cast<NS::UInteger>(create_info.view_base_level),
                static_cast<NS::UInteger>(std::max(1, create_info.level_count))
            );
            NS::Range slice_range = NS::Range::Make(
                static_cast<NS::UInteger>(create_info.view_base_array_layer),
                1
            );
            // When viewing a single layer of a 2D array texture, create
            // the view as texture2d so shaders can sample it as a regular 2D texture.
            MTL::TextureType view_type = source_mtl_texture->textureType();
            if ((view_type == MTL::TextureType2DArray) && (create_info.array_layer_count == 0)) {
                view_type = MTL::TextureType2D;
            }
            m_mtl_texture = source_mtl_texture->newTextureView(
                pixel_format,
                view_type,
                level_range,
                slice_range
            );
            if ((m_mtl_texture != nullptr) && !m_debug_label.empty()) {
                NS::String* label = NS::String::alloc()->init(m_debug_label.data(), NS::UTF8StringEncoding);
                m_mtl_texture->setLabel(label);
                label->release();
            }
        }
        return;
    }

    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setTextureType(to_mtl_texture_type(m_type, m_sample_count, m_array_layer_count));
    desc->setPixelFormat(pixel_format);
    desc->setWidth(static_cast<NS::UInteger>(std::max(1, m_width)));
    desc->setHeight(static_cast<NS::UInteger>(std::max(1, m_height)));
    desc->setDepth(static_cast<NS::UInteger>(std::max(1, m_depth)));
    desc->setMipmapLevelCount(static_cast<NS::UInteger>(std::max(1, m_level_count)));
    if (m_array_layer_count > 0) {
        desc->setArrayLength(static_cast<NS::UInteger>(m_array_layer_count));
    }
    if (m_sample_count > 1) {
        desc->setSampleCount(static_cast<NS::UInteger>(m_sample_count));
    }
    desc->setStorageMode(MTL::StorageModePrivate);

    MTL::TextureUsage usage = MTL::TextureUsageShaderRead;
    if (create_info.usage_mask & Image_usage_flag_bit_mask::color_attachment) {
        usage |= MTL::TextureUsageRenderTarget;
    }
    if (create_info.usage_mask & Image_usage_flag_bit_mask::depth_stencil_attachment) {
        usage |= MTL::TextureUsageRenderTarget;
    }
    desc->setUsage(usage);

    m_mtl_texture = mtl_device->newTexture(desc);
    desc->release();

    if ((m_mtl_texture != nullptr) && !m_debug_label.empty()) {
        NS::String* label = NS::String::alloc()->init(m_debug_label.data(), NS::UTF8StringEncoding);
        m_mtl_texture->setLabel(label);
        label->release();
    }
}

auto Texture_impl::get_mipmap_dimensions(const Texture_type type) -> int
{
    switch (type) {
        case Texture_type::texture_1d:             return 1;
        case Texture_type::texture_2d:             return 2;
        case Texture_type::texture_2d_array:       return 2;
        case Texture_type::texture_3d:             return 3;
        case Texture_type::texture_cube_map:       return 2;
        case Texture_type::texture_cube_map_array: return 2;
        default:                                   return 0;
    }
}

auto Texture_impl::get_debug_label() const -> erhe::utility::Debug_label { return m_debug_label; }
auto Texture_impl::get_pixelformat() const -> erhe::dataformat::Format { return m_pixelformat; }

auto Texture_impl::get_width(unsigned int level) const -> int
{
    int size = m_width;
    for (unsigned int i = 0; i < level; i++) { size = std::max(1, size / 2); }
    return size;
}

auto Texture_impl::get_height(unsigned int level) const -> int
{
    int size = m_height;
    for (unsigned int i = 0; i < level; i++) { size = std::max(1, size / 2); }
    return size;
}

auto Texture_impl::get_depth(unsigned int level) const -> int
{
    int size = m_depth;
    for (unsigned int i = 0; i < level; i++) { size = std::max(1, size / 2); }
    return size;
}

auto Texture_impl::get_array_layer_count() const -> int { return m_array_layer_count; }
auto Texture_impl::get_level_count() const -> int { return m_level_count; }
auto Texture_impl::get_fixed_sample_locations() const -> bool { return m_fixed_sample_locations; }
auto Texture_impl::get_sample_count() const -> int { return m_sample_count; }
auto Texture_impl::get_texture_type() const -> Texture_type { return m_type; }
auto Texture_impl::is_layered() const -> bool { return m_array_layer_count > 0; }
auto Texture_impl::gl_name() const -> unsigned int { return 0; }
auto Texture_impl::get_handle() const -> uint64_t { return 0; }
auto Texture_impl::is_sparse() const -> bool { return m_is_sparse; }
auto Texture_impl::get_mtl_texture() const -> MTL::Texture* { return m_mtl_texture; }
void Texture_impl::clear() const {}

void Texture_impl::set_buffer(Buffer& buffer)
{
    static_cast<void>(buffer);
}

auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool { return &lhs == &rhs; }
auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool { return !(lhs == rhs); }

} // namespace erhe::graphics
