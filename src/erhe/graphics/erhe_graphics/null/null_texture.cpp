#include "erhe_graphics/null/null_texture.hpp"
#include "erhe_graphics/device.hpp"

#include <algorithm>

namespace erhe::graphics {

Texture_impl::Texture_impl(Texture_impl&&) noexcept = default;

Texture_impl::~Texture_impl() noexcept = default;

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
    static_cast<void>(device);
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

auto Texture_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

auto Texture_impl::get_pixelformat() const -> erhe::dataformat::Format
{
    return m_pixelformat;
}

auto Texture_impl::get_width(unsigned int level) const -> int
{
    int size = m_width;
    for (unsigned int i = 0; i < level; i++) {
        size = std::max(1, size / 2);
    }
    return size;
}

auto Texture_impl::get_height(unsigned int level) const -> int
{
    int size = m_height;
    for (unsigned int i = 0; i < level; i++) {
        size = std::max(1, size / 2);
    }
    return size;
}

auto Texture_impl::get_depth(unsigned int level) const -> int
{
    int size = m_depth;
    for (unsigned int i = 0; i < level; i++) {
        size = std::max(1, size / 2);
    }
    return size;
}

auto Texture_impl::get_array_layer_count() const -> int
{
    return m_array_layer_count;
}

auto Texture_impl::get_level_count() const -> int
{
    return m_level_count;
}

auto Texture_impl::get_fixed_sample_locations() const -> bool
{
    return m_fixed_sample_locations;
}

auto Texture_impl::get_sample_count() const -> int
{
    return m_sample_count;
}

auto Texture_impl::get_texture_type() const -> Texture_type
{
    return m_type;
}

auto Texture_impl::is_layered() const -> bool
{
    return m_array_layer_count > 0;
}

auto Texture_impl::gl_name() const -> unsigned int
{
    return 0;
}

auto Texture_impl::get_handle() const -> uint64_t
{
    return 0;
}

auto Texture_impl::is_sparse() const -> bool
{
    return m_is_sparse;
}

void Texture_impl::clear() const
{
}

auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return &lhs == &rhs;
}

auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
