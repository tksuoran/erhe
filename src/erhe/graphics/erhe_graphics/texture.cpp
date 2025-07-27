// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {
    
auto Texture::get_mipmap_dimensions(const Texture_type type) -> int
{
    switch (type) {
        //using enum gl::Texture_target;
        case Texture_type::texture_1d:       return 1;
        case Texture_type::texture_cube_map: return 2;
        case Texture_type::texture_2d:       return 2;
        case Texture_type::texture_3d:       return 3;
        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

auto Texture::get_static_type() -> uint64_t
{
    return erhe::Item_type::texture;
}

auto Texture::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Texture::get_type_name() const -> std::string_view
{
    return static_type_name;
}

Texture::Texture(Texture&&) noexcept = default;

Texture::~Texture() noexcept
{
    SPDLOG_LOGGER_TRACE(log_texture, "Deleting texture {} {}", gl_name(), m_debug_label);
}

auto Texture::get_referenced_texture() const -> const Texture*
{
    return this;
}

auto Texture::get_size_level_count(int size) -> int
{
    int level_count = size > 0 ? 1 : 0;

    while (size > 1) {
        size = size / 2;
        ++level_count;
    }
    return level_count;
}

auto get_texture_level_count(const int width, const int height, const int depth) -> int
{
    const auto x_level_count = Texture::get_size_level_count(width);
    const auto y_level_count = Texture::get_size_level_count(height);
    const auto z_level_count = Texture::get_size_level_count(depth);
    return std::max(std::max(x_level_count, y_level_count), z_level_count);
}

auto Texture_create_info::get_texture_level_count() const -> int
{
    const auto dimensions = Texture::get_mipmap_dimensions(type);

    if (dimensions >= 1) {
        if (width == 0) {
            ERHE_FATAL("zero texture width");
        }
    }

    if (dimensions >= 2) {
        if (height == 0) {
            ERHE_FATAL("zero texture height");
        }
    }

    if (dimensions == 3) {
        if (depth == 0) {
            ERHE_FATAL("zero texture depth");
        }
    }

    return use_mipmaps
        ? erhe::graphics::get_texture_level_count(
            width,
            (dimensions >= 2) ? height : 0,
            (dimensions >= 3) ? depth : 0
        )
        : 1;
}

auto Texture_create_info::make_view(Device& device, const std::shared_ptr<Texture>& view_source) -> Texture_create_info
{
    Texture_create_info create_info{device};
    create_info.type                   = view_source->get_texture_type();
    create_info.pixelformat            = view_source->get_pixelformat();
    create_info.use_mipmaps            = view_source->get_level_count() > 1;
    create_info.fixed_sample_locations = view_source->get_fixed_sample_locations();
    create_info.sparse                 = view_source->is_sparse();
    create_info.sample_count           = view_source->get_sample_count();
    create_info.width                  = view_source->get_width(); // TODO view_min_level
    create_info.height                 = view_source->get_height();
    create_info.depth                  = view_source->get_depth();
    create_info.array_layer_count      = view_source->get_array_layer_count();
    create_info.level_count            = view_source->get_level_count();
    create_info.debug_label            = fmt::format("View of {}", view_source->get_debug_label());
    create_info.view_source            = view_source;
    return create_info;
}


Texture::Texture(Device& device, const Create_info& create_info)
    : Item{create_info.debug_label}
    , m_impl{std::make_unique<Texture_impl>(device, create_info)}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}
auto Texture::get_debug_label() const -> const std::string&
{
    return m_impl->get_debug_label();
}
auto Texture::get_texture_type() const -> Texture_type
{
    return m_impl->get_texture_type();
}
auto Texture::is_layered() const -> bool
{
    return m_impl->is_layered();
}
auto Texture::get_width(unsigned int level) const -> int
{
    return m_impl->get_width(level);
}
auto Texture::get_height(unsigned int level) const -> int
{
    return m_impl->get_height(level);
}
auto Texture::get_depth(unsigned int level) const -> int
{
    return m_impl->get_depth(level);
}
auto Texture::get_array_layer_count() const -> int
{
    return m_impl->get_array_layer_count();
}
auto Texture::get_level_count() const -> int
{
    return m_impl->get_level_count();
}
auto Texture::get_fixed_sample_locations() const -> bool
{
    return m_impl->get_fixed_sample_locations();
}
auto Texture::get_pixelformat() const -> erhe::dataformat::Format
{
    return m_impl->get_pixelformat();
}
auto Texture::get_sample_count() const -> int
{
    return m_impl->get_sample_count();
}
auto Texture::is_sparse() const -> bool
{
    return m_impl->is_sparse();
}
auto Texture::get_impl() -> Texture_impl&
{
    return *m_impl.get();
}
auto Texture::get_impl() const -> const Texture_impl&
{
    return *m_impl.get();
}

auto format_texture_handle(uint64_t handle) -> std::string
{
    uint32_t low  = static_cast<uint32_t>((handle & 0xffffffffu));
    uint32_t high = static_cast<uint32_t>( handle >> 32u);
    return fmt::format("{:08x}.{:08x} {}.{}", high, low, high, low);
}

} // namespace erhe::graphics
