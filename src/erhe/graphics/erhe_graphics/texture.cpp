// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/texture.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_texture.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_texture.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_texture.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_texture.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_texture.hpp"
#endif

#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace erhe::graphics {

const erhe::property::Property<int> Texture::width_property = erhe::property::Property<int>::register_computed(
    "width", Texture::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        return static_cast<const Texture&>(object).get_width();
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "Texture", .tooltip = "Level 0 width in texels (computed)", .label = "Width"}
    }
);
const erhe::property::Property<int> Texture::height_property = erhe::property::Property<int>::register_computed(
    "height", Texture::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        return static_cast<const Texture&>(object).get_height();
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "Texture", .tooltip = "Level 0 height in texels (computed)", .label = "Height"}
    }
);
const erhe::property::Property<std::string> Texture::pixelformat_property = erhe::property::Property<std::string>::register_computed(
    "pixelformat", Texture::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        return std::string{erhe::dataformat::c_str(static_cast<const Texture&>(object).get_pixelformat())};
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "Texture", .tooltip = "Pixel format (computed)", .label = "Format"}
    }
);

namespace {

auto texture_property_ui(const std::string_view label, const std::string_view tooltip) -> erhe::property::Property_ui
{
    return erhe::property::Property_ui{.group = "Texture", .tooltip = tooltip, .label = label};
}

} // anonymous namespace

const erhe::property::Property<Texture_transfer_function> Texture::transfer_function_property = erhe::property::Property<Texture_transfer_function>::register_property(
    "transfer_function", Texture::property_owner_type(), c_texture_transfer_function_enum_info,
    erhe::property::Property_metadata{
        .default_value = erhe::property::make_value(Texture_transfer_function::srgb),
        .ui            = texture_property_ui("Transfer Function", "sRGB for color data, linear for data textures; read at load and by conversion")
    }
);
const erhe::property::Property<Texture_normal_encoding> Texture::normal_encoding_property = erhe::property::Property<Texture_normal_encoding>::register_property(
    "normal_encoding", Texture::property_owner_type(), c_texture_normal_encoding_enum_info,
    erhe::property::Property_metadata{
        .default_value = erhe::property::make_value(Texture_normal_encoding::none),
        .ui            = texture_property_ui("Normal Encoding", "Not a normal map, or a normal map with three or two stored components; read by conversion")
    }
);
const erhe::property::Property<bool> Texture::generate_mipmaps_property = erhe::property::Property<bool>::register_property(
    "generate_mipmaps", Texture::property_owner_type(),
    erhe::property::Property_metadata{
        .default_value = true,
        .ui            = texture_property_ui("Generate Mipmaps", "Make the mip levels an image does not carry; read at load")
    }
);
const erhe::property::Property<Sampler_address_mode> Texture::wrap_u_property = erhe::property::Property<Sampler_address_mode>::register_property(
    "wrap_u", Texture::property_owner_type(), c_texture_wrap_enum_info,
    erhe::property::Property_metadata{
        .default_value = erhe::property::make_value(Sampler_address_mode::repeat),
        .ui            = texture_property_ui("Wrap U", "The wrap mode mipmap generation filters with at the image's edges in u")
    }
);
const erhe::property::Property<Sampler_address_mode> Texture::wrap_v_property = erhe::property::Property<Sampler_address_mode>::register_property(
    "wrap_v", Texture::property_owner_type(), c_texture_wrap_enum_info,
    erhe::property::Property_metadata{
        .default_value = erhe::property::make_value(Sampler_address_mode::repeat),
        .ui            = texture_property_ui("Wrap V", "The wrap mode mipmap generation filters with at the image's edges in v")
    }
);

auto Texture::get_texture_properties() const -> Texture_properties
{
    return Texture_properties{
        .transfer_function = get_value(transfer_function_property),
        .normal_encoding   = get_value(normal_encoding_property),
        .generate_mipmaps  = get_value(generate_mipmaps_property),
        .wrap_u            = get_value(wrap_u_property),
        .wrap_v            = get_value(wrap_v_property)
    };
}

void Texture::set_texture_properties(const Texture_properties& properties)
{
    set_value(transfer_function_property, properties.transfer_function);
    set_value(normal_encoding_property,   properties.normal_encoding);
    set_value(generate_mipmaps_property,  properties.generate_mipmaps);
    set_value(wrap_u_property,            properties.wrap_u);
    set_value(wrap_v_property,            properties.wrap_v);
}

Texture_reference_user::~Texture_reference_user() noexcept = default;

Texture_reference::~Texture_reference() noexcept = default;

namespace {

// Process-wide image memory accounting. Atomics because textures are created
// and destroyed from worker threads (glTF image residency) as well as the
// main thread.
std::atomic<std::size_t> g_texture_count{0};
std::atomic<std::size_t> g_texture_byte_count{0};

std::mutex  g_content_texture_mutex;
std::size_t g_content_texture_count     {0};
std::size_t g_content_texture_byte_count{0};

}

// Estimate from the create info. A view or a wrapped external image owns no
// allocation of its own and is not counted.
auto estimate_texture_byte_count(const Texture_create_info& create_info) -> std::size_t
{
    if ((create_info.wrap_texture_name != 0) || (create_info.buffer != nullptr)) {
        return 0;
    }
    const int width  = std::max(create_info.width,  1);
    const int height = std::max(create_info.height, 1);
    const int depth  = std::max(create_info.depth,  1);
    const int layers = std::max(create_info.array_layer_count, 1);
    const int levels = std::max(create_info.get_texture_level_count(), 1);
    const std::size_t slice_bytes = erhe::dataformat::get_mip_chain_byte_count(
        create_info.pixelformat,
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        static_cast<std::size_t>(levels)
    );
    const std::size_t samples = static_cast<std::size_t>(std::max(create_info.sample_count, 1));
    return slice_bytes * static_cast<std::size_t>(depth) * static_cast<std::size_t>(layers) * samples;
}


auto Texture::get_memory_statistics() -> Texture::Memory_statistics
{
    const std::lock_guard<std::mutex> lock{g_content_texture_mutex};
    return Memory_statistics{
        .texture_count         = g_texture_count.load(std::memory_order_relaxed),
        .byte_count            = g_texture_byte_count.load(std::memory_order_relaxed),
        .content_texture_count = g_content_texture_count,
        .content_byte_count    = g_content_texture_byte_count
    };
}

auto Texture::get_mipmap_dimensions(const Texture_type type) -> int
{
    switch (type) {
        case Texture_type::texture_buffer:         return 0;
        case Texture_type::texture_1d:             return 1;
        case Texture_type::texture_2d:             return 2;
        case Texture_type::texture_2d_array:       return 2;
        case Texture_type::texture_3d:             return 3;
        case Texture_type::texture_cube_map:       return 2;
        case Texture_type::texture_cube_map_array: return 2;
        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

Texture::Texture(Texture&&) noexcept = default;

Texture::~Texture() noexcept
{
    SPDLOG_LOGGER_TRACE(log_texture, "Deleting texture {} {}", gl_name(), m_debug_label);
    if (m_estimated_byte_count > 0) {
        g_texture_count     .fetch_sub(1,                      std::memory_order_relaxed);
        g_texture_byte_count.fetch_sub(m_estimated_byte_count, std::memory_order_relaxed);
        if (m_memory_class == Texture_memory_class::content) {
            const std::lock_guard<std::mutex> lock{g_content_texture_mutex};
            --g_content_texture_count;
            g_content_texture_byte_count -= m_estimated_byte_count;
        }
    }
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

auto Texture_create_info::make_view(
    Device&                         device,
    const std::shared_ptr<Texture>& view_source,
    const int                       view_base_level,
    const int                       view_base_array_layer
) -> Texture_create_info
{
    const int source_level_count = view_source->get_level_count();
    const int source_layer_count = view_source->get_array_layer_count();
    ERHE_VERIFY((view_base_level >= 0) && (view_base_level < source_level_count));
    ERHE_VERIFY((view_base_array_layer >= 0) && (view_base_array_layer < std::max(1, source_layer_count)));
    const unsigned int base_level = static_cast<unsigned int>(view_base_level);

    Texture_create_info create_info{device};
    create_info.type                   = view_source->get_texture_type();
    create_info.pixelformat            = view_source->get_pixelformat();
    create_info.fixed_sample_locations = view_source->get_fixed_sample_locations();
    create_info.sparse                 = view_source->is_sparse();
    create_info.sample_count           = view_source->get_sample_count();
    create_info.width                  = view_source->get_width (base_level);
    create_info.height                 = view_source->get_height(base_level);
    create_info.depth                  = view_source->get_depth (base_level);
    create_info.array_layer_count      = (source_layer_count > 0) ? (source_layer_count - view_base_array_layer) : 0;
    create_info.level_count            = source_level_count - view_base_level;
    create_info.use_mipmaps            = create_info.level_count > 1;
    create_info.debug_label            = erhe::utility::Debug_label{ fmt::format("View of {}", view_source->get_debug_label().string_view()) };
    create_info.view_source            = view_source;
    create_info.view_base_level        = view_base_level;
    create_info.view_base_array_layer  = view_base_array_layer;
    return create_info;
}


Texture::Texture(Device& device, const Texture_create_info& create_info)
    : Item{create_info.debug_label.string_view()}
    , m_impl{std::make_unique<Texture_impl>(device, create_info)}
    , m_estimated_byte_count{estimate_texture_byte_count(create_info)}
    , m_memory_class        {create_info.memory_class}
{
    ERHE_VERIFY(create_info.usage_mask != 0);
    enable_flag_bits(erhe::Item_flags::show_in_ui);
    if (m_estimated_byte_count > 0) {
        g_texture_count     .fetch_add(1,                     std::memory_order_relaxed);
        g_texture_byte_count.fetch_add(m_estimated_byte_count, std::memory_order_relaxed);
        if (m_memory_class == Texture_memory_class::content) {
            const std::lock_guard<std::mutex> lock{g_content_texture_mutex};
            ++g_content_texture_count;
            g_content_texture_byte_count += m_estimated_byte_count;
        }
    }
}
auto Texture::get_debug_label() const -> erhe::utility::Debug_label
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
void Texture::clear() const
{
    m_impl->clear();
}
void Texture::set_buffer(Buffer& buffer)
{
    m_impl->set_buffer(buffer);
}

auto format_texture_handle(uint64_t handle) -> std::string
{
    uint32_t low  = static_cast<uint32_t>((handle & 0xffffffffu));
    uint32_t high = static_cast<uint32_t>( handle >> 32u);
    return fmt::format("{:08x}.{:08x} {}.{}", high, low, high, low);
}

} // namespace erhe::graphics
