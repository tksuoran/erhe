// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

class InternalFormatFormatType
{
public:
    gl::Internal_format internal_format;
    gl::Pixel_format    format;
    gl::Pixel_type      type;
};

auto component_count(const gl::Pixel_format pixel_format) -> size_t
{
    switch (pixel_format) {
        //using enum gl::Pixel_format;
        case gl::Pixel_format::red:
        case gl::Pixel_format::red_integer: {
            return 1;
        }

        case gl::Pixel_format::rg:
        case gl::Pixel_format::rg_integer: {
            return 2;
        }

        case gl::Pixel_format::rgb:
        case gl::Pixel_format::rgb_integer: {
            return 3;
        }

        case gl::Pixel_format::rgba:
        case gl::Pixel_format::rgba_integer: {
            return 4;
        }

        default: {
            ERHE_FATAL("Bad pixel format");
        }
    }
}

auto byte_count(const gl::Pixel_type pixel_type) -> size_t
{
    switch (pixel_type) {
        //using enum gl::Pixel_type;
        case gl::Pixel_type::unsigned_byte:
        case gl::Pixel_type::byte: {
            return 1;
        }

        case gl::Pixel_type::unsigned_short:
        case gl::Pixel_type::short_: {
            return 2;
        }

        case gl::Pixel_type::unsigned_int:
        case gl::Pixel_type::int_:
        case gl::Pixel_type::float_: {
            return 4;
        }

        default: {
            ERHE_FATAL("Bad pixel type");
        }
    }
};

constexpr std::array INTERNAL_FORMAT_INFO =
{
    InternalFormatFormatType{ gl::Internal_format::r8               , gl::Pixel_format::red            , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::r8_snorm         , gl::Pixel_format::red            , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::r16f             , gl::Pixel_format::red            , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::r32f             , gl::Pixel_format::red            , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::rg8              , gl::Pixel_format::rg             , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rg8_snorm        , gl::Pixel_format::rg             , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::rg16f            , gl::Pixel_format::rg             , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::rg32f            , gl::Pixel_format::rg             , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::rgb8             , gl::Pixel_format::rgb            , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rgb8_snorm       , gl::Pixel_format::rgb            , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::srgb8            , gl::Pixel_format::rgb            , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rgb16f           , gl::Pixel_format::rgb            , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::r11f_g11f_b10f   , gl::Pixel_format::rgb            , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::rgba8            , gl::Pixel_format::rgba           , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::srgb8_alpha8     , gl::Pixel_format::rgba           , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rgba8_snorm      , gl::Pixel_format::rgba           , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::rgba32f          , gl::Pixel_format::rgba           , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::rgba16f          , gl::Pixel_format::rgba           , gl::Pixel_type::float_         },
    InternalFormatFormatType{ gl::Internal_format::r8ui             , gl::Pixel_format::red_integer    , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::r16ui            , gl::Pixel_format::red_integer    , gl::Pixel_type::unsigned_short },
    InternalFormatFormatType{ gl::Internal_format::r32ui            , gl::Pixel_format::red_integer    , gl::Pixel_type::unsigned_int   },
    InternalFormatFormatType{ gl::Internal_format::r8i              , gl::Pixel_format::red_integer    , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::r16i             , gl::Pixel_format::red_integer    , gl::Pixel_type::short_         },
    InternalFormatFormatType{ gl::Internal_format::r32i             , gl::Pixel_format::red_integer    , gl::Pixel_type::int_           },
    InternalFormatFormatType{ gl::Internal_format::rg8ui            , gl::Pixel_format::rg_integer     , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rg16ui           , gl::Pixel_format::rg_integer     , gl::Pixel_type::unsigned_short },
    InternalFormatFormatType{ gl::Internal_format::rg32ui           , gl::Pixel_format::rg_integer     , gl::Pixel_type::unsigned_int   },
    InternalFormatFormatType{ gl::Internal_format::rg8i             , gl::Pixel_format::rg_integer     , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::rg16i            , gl::Pixel_format::rg_integer     , gl::Pixel_type::short_         },
    InternalFormatFormatType{ gl::Internal_format::rg32i            , gl::Pixel_format::rg_integer     , gl::Pixel_type::int_           },
    InternalFormatFormatType{ gl::Internal_format::rgb8ui           , gl::Pixel_format::rgb_integer    , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rgb16ui          , gl::Pixel_format::rgb_integer    , gl::Pixel_type::unsigned_short },
    InternalFormatFormatType{ gl::Internal_format::rgb32ui          , gl::Pixel_format::rgb_integer    , gl::Pixel_type::unsigned_int   },
    InternalFormatFormatType{ gl::Internal_format::rgb8i            , gl::Pixel_format::rgb_integer    , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::rgb16i           , gl::Pixel_format::rgb_integer    , gl::Pixel_type::short_         },
    InternalFormatFormatType{ gl::Internal_format::rgb32i           , gl::Pixel_format::rgb_integer    , gl::Pixel_type::int_           },
    InternalFormatFormatType{ gl::Internal_format::rgba8ui          , gl::Pixel_format::rgba_integer   , gl::Pixel_type::unsigned_byte  },
    InternalFormatFormatType{ gl::Internal_format::rgba16ui         , gl::Pixel_format::rgba_integer   , gl::Pixel_type::unsigned_short },
    InternalFormatFormatType{ gl::Internal_format::rgba32ui         , gl::Pixel_format::rgba_integer   , gl::Pixel_type::unsigned_int   },
    InternalFormatFormatType{ gl::Internal_format::rgba8i           , gl::Pixel_format::rgba_integer   , gl::Pixel_type::byte           },
    InternalFormatFormatType{ gl::Internal_format::rgba16i          , gl::Pixel_format::rgba_integer   , gl::Pixel_type::short_         },
    InternalFormatFormatType{ gl::Internal_format::rgba32i          , gl::Pixel_format::rgba_integer   , gl::Pixel_type::int_           },
    InternalFormatFormatType{ gl::Internal_format::depth_component16, gl::Pixel_format::depth_component, gl::Pixel_type::unsigned_short },
    InternalFormatFormatType{ gl::Internal_format::depth_component16, gl::Pixel_format::depth_component, gl::Pixel_type::unsigned_int   }
};

auto get_gl_pixel_byte_count(const erhe::dataformat::Format pixelformat)-> size_t
{
    const std::optional<gl::Internal_format> gl_internal_format_opt = gl_helpers::convert_to_gl(pixelformat);
    ERHE_VERIFY(gl_internal_format_opt.has_value());
    const gl::Internal_format internal_format = gl_internal_format_opt.value();

    for (const auto& entry : INTERNAL_FORMAT_INFO) {
        if (entry.internal_format == internal_format) {
            // For now, there are no packed entries
            return component_count(entry.format) * byte_count(entry.type);
        }
    }
    ERHE_FATAL("Bad internal format");
}

auto get_format_and_type(erhe::dataformat::Format pixelformat, gl::Pixel_format& format, gl::Pixel_type& type) -> bool
{
    const std::optional<gl::Internal_format> gl_internal_format_opt = gl_helpers::convert_to_gl(pixelformat);
    ERHE_VERIFY(gl_internal_format_opt.has_value());
    const gl::Internal_format internal_format = gl_internal_format_opt.value();

    for (const auto& entry : INTERNAL_FORMAT_INFO) {
        if (entry.internal_format == internal_format) {
            format = entry.format;
            type   = entry.type;
            return true;
        }
    }
    return false;
}

auto Texture_impl::is_array_target(gl::Texture_target target) -> bool
{
    switch (target) {
        case gl::Texture_target::texture_1d_array:
        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array:
        case gl::Texture_target::texture_cube_map_array: {
            return true;
        }
        default: {
            return false;
        }
    }
}

void convert_texture_dimensions_from_gl(const gl::Texture_target target, int& width, int& height, int& depth, int& array_layer_count)
{
    switch (target) {
        //using enum gl::Texture_target;
        case gl::Texture_target::texture_buffer: {
            array_layer_count = 1;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height == 1);
            ERHE_VERIFY(depth == 1);
            return;
        }

        case gl::Texture_target::texture_1d: {
            array_layer_count = 1;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height == 1);
            ERHE_VERIFY(depth == 1);
            return;
        }

        case gl::Texture_target::texture_1d_array: {
            ERHE_VERIFY(height >= 1);
            array_layer_count = height;
            height = 1;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(depth == 1);
            return;
        }
        case gl::Texture_target::texture_2d:
        case gl::Texture_target::texture_2d_multisample:
        case gl::Texture_target::texture_rectangle:
        {
            array_layer_count = 0;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth == 1);
            return;
        }
        case gl::Texture_target::texture_cube_map: {
            array_layer_count = 0;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth == 6);
            return;
        }

        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array: {
            ERHE_VERIFY(depth >= 1);
            array_layer_count = depth;
            depth = 1;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            return;
        }
        case gl::Texture_target::texture_3d: {
            array_layer_count = 0;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth >= 1);
            return;
        }
        case gl::Texture_target::texture_cube_map_array: {
            ERHE_VERIFY(depth % 6 == 0);
            array_layer_count = depth / 6;
            depth = 1;
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            return;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

void convert_texture_dimensions_to_gl(const gl::Texture_target target, int& width, int& height, int& depth, int array_layer_count)
{
    switch (target) {
        //using enum gl::Texture_target;
        case gl::Texture_target::texture_buffer: {
            ERHE_VERIFY(width == 0);
            ERHE_VERIFY(height == 0);
            ERHE_VERIFY(depth == 0);
            ERHE_VERIFY(array_layer_count == 0);
            return;
        }

        case gl::Texture_target::texture_1d: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height == 1);
            ERHE_VERIFY(depth == 1);
            ERHE_VERIFY(array_layer_count == 0);
            return;
        }

        case gl::Texture_target::texture_1d_array: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height == 1);
            ERHE_VERIFY(depth == 1);
            ERHE_VERIFY(array_layer_count >= 1);
            height = array_layer_count;
            return;
        }
        case gl::Texture_target::texture_2d:
        case gl::Texture_target::texture_2d_multisample:
        case gl::Texture_target::texture_rectangle: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth == 1);
            ERHE_VERIFY(array_layer_count == 0);
            return;
        }
        case gl::Texture_target::texture_cube_map: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth == 6); // TODO Is this correct?
            ERHE_VERIFY(array_layer_count == 0);
            return;
        }

        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(array_layer_count >= 1);
            depth = array_layer_count;
            return;
        }
        case gl::Texture_target::texture_3d: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth >= 1);
            ERHE_VERIFY(array_layer_count == 0);
            return;
        }
        case gl::Texture_target::texture_cube_map_array: {
            ERHE_VERIFY(width >= 1);
            ERHE_VERIFY(height >= 1);
            ERHE_VERIFY(depth == 6);
            ERHE_VERIFY(array_layer_count >= 1);
            depth = 6 * array_layer_count;
            return;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

void convert_texture_offset_to_gl(const gl::Texture_target target, int& x, int& y, int& z, int array_layer)
{
    static_cast<void>(x);
    static_cast<void>(y);
    switch (target) {
        //using enum gl::Texture_target;
        case gl::Texture_target::texture_buffer: {
            return;
        }

        case gl::Texture_target::texture_1d: {
            return;
        }

        case gl::Texture_target::texture_1d_array: {
            y = array_layer;
            return;
        }
        case gl::Texture_target::texture_2d:
        case gl::Texture_target::texture_2d_multisample:
        case gl::Texture_target::texture_rectangle: {
            return;
        }
        case gl::Texture_target::texture_cube_map: {
            return;
        }

        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array: {
            z = array_layer;
            return;
        }
        case gl::Texture_target::texture_3d: {
            return;
        }
        case gl::Texture_target::texture_cube_map_array: {
            z = 6 * array_layer;
            return;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

auto Texture_impl::get_storage_dimensions(const gl::Texture_target target) -> int
{
    switch (target) {
        //using enum gl::Texture_target;
        case gl::Texture_target::texture_buffer: {
            return 0;
        }

        case gl::Texture_target::texture_1d: {
            return 1;
        }

        case gl::Texture_target::texture_1d_array:
        case gl::Texture_target::texture_2d:
        case gl::Texture_target::texture_2d_multisample:
        case gl::Texture_target::texture_rectangle:
        case gl::Texture_target::texture_cube_map: {
            return 2;
        }

        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array:
        case gl::Texture_target::texture_3d:
        case gl::Texture_target::texture_cube_map_array: {
            return 3;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

auto Texture_impl::get_mipmap_dimensions(const Texture_type type) -> int
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

auto Texture_impl::get_mipmap_dimensions(const gl::Texture_target target) -> int
{
    switch (target) {
        //using enum gl::Texture_target;
        case gl::Texture_target::texture_buffer: {
            return 0;
        }

        case gl::Texture_target::texture_1d:
        case gl::Texture_target::texture_1d_array: {
            return 1;
        }

        case gl::Texture_target::texture_rectangle:
        case gl::Texture_target::texture_cube_map:
        case gl::Texture_target::texture_2d:
        case gl::Texture_target::texture_2d_multisample:
        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array: {
            return 2;
        }

        case gl::Texture_target::texture_3d:
        case gl::Texture_target::texture_cube_map_array: {
            return 3;
        }

        default: {
            ERHE_FATAL("Bad texture target");
        }
    }
}

Texture_impl::Texture_impl(Texture_impl&&) noexcept = default;

Texture_impl::~Texture_impl() noexcept
{
    SPDLOG_LOGGER_TRACE(log_texture, "Deleting texture {} {}", gl_name(), m_debug_label);
}

auto Texture_impl::gl_name() const -> GLuint
{
    return m_handle.gl_name();
}

auto Texture_impl::get_handle() const -> uint64_t
{
    return gl::get_texture_handle_arb(m_handle.gl_name());
}

auto get_texture_from_handle(uint64_t handle) -> GLuint
{
    return static_cast<GLuint>(handle & 0xffffffffu);
}

auto get_sampler_from_handle(uint64_t handle) -> GLuint
{
    return static_cast<GLuint>((handle & 0xffffffff00000000u) >> 32);
}

auto convert_to_gl_texture_target(Texture_type type, bool multisample, bool array) -> gl::Texture_target
{
    switch (type) {
        case Texture_type::texture_buffer: {
            ERHE_VERIFY(!multisample);
            ERHE_VERIFY(!array);
            return gl::Texture_target::texture_buffer;
        }
        case Texture_type::texture_1d: {
            ERHE_VERIFY(!multisample);
            return array
                ? gl::Texture_target::texture_1d_array
                : gl::Texture_target::texture_1d;
        }
        case Texture_type::texture_2d: {
            return array
                ? (multisample
                    ? gl::Texture_target::texture_2d_multisample_array
                    : gl::Texture_target::texture_2d_array)
                : (multisample
                    ? gl::Texture_target::texture_2d_multisample
                    : gl::Texture_target::texture_2d);
        }
        case Texture_type::texture_3d: {
            ERHE_VERIFY(!multisample);
            ERHE_VERIFY(!array);
            return gl::Texture_target::texture_3d;
        }
        case Texture_type::texture_cube_map: {
            ERHE_VERIFY(!multisample);
            return array 
                ? gl::Texture_target::texture_cube_map_array
                : gl::Texture_target::texture_cube_map;
        }
        default: {
            ERHE_FATAL("Bad texture type %u", static_cast<unsigned int>(type));
            return gl::Texture_target::texture_2d;
        }
    }
}

[[nodiscard]] auto convert_from_gl_texture_target(gl::Texture_target gl_texture_target, bool& multisample, bool& array) -> Texture_type
{
    switch (gl_texture_target) {
        case gl::Texture_target::texture_buffer: {
            multisample = false;
            array       = false;
            return Texture_type::texture_buffer;
        }
        case gl::Texture_target::texture_1d: {
            multisample = false;
            array       = false;
            return Texture_type::texture_1d;
        }
        case gl::Texture_target::texture_1d_array: {
            multisample = false;
            array      = true;
            return Texture_type::texture_1d;
        }
        case gl::Texture_target::texture_2d: {
            multisample = false;
            array       = false;
            return Texture_type::texture_2d;
        }
        case gl::Texture_target::texture_2d_array: {
            multisample = false;
            array       = true;
            return Texture_type::texture_2d;
        }
        case gl::Texture_target::texture_2d_multisample: {
            multisample = true;
            array       = false;
            return Texture_type::texture_2d;
        }
        case gl::Texture_target::texture_2d_multisample_array: {
            multisample = true;
            array       = true;
            return Texture_type::texture_2d;
        }
        case gl::Texture_target::texture_3d: {
            multisample = false;
            array       = false;
            return Texture_type::texture_3d;
        }
        case gl::Texture_target::texture_cube_map: {
            multisample = false;
            array       = false;
            return Texture_type::texture_cube_map;
        }
        case gl::Texture_target::texture_cube_map_array: {
            multisample = false;
            array       = true;
            return Texture_type::texture_cube_map;
        }
        default: {
            ERHE_FATAL("Bad gl::Texture_target %04x", static_cast<unsigned int>(gl_texture_target));
            multisample = false;
            array       = false;
            return Texture_type::texture_2d;
        }
    }
}


Texture_impl::Texture_impl(Device& device, const Create_info& create_info)
    : m_handle{
        convert_to_gl_texture_target(create_info.type, create_info.sample_count != 0, create_info.array_layer_count != 0),
        static_cast<GLuint>(create_info.wrap_texture_name),
        create_info.view_source ? Gl_texture::texture_view : Gl_texture::not_texture_view
    }
    , m_type                  {create_info.type}
    , m_pixelformat           {create_info.pixelformat}
    , m_fixed_sample_locations{create_info.fixed_sample_locations}
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
    , m_buffer                {create_info.buffer}
    , m_debug_label           {create_info.debug_label}
{
    ERHE_PROFILE_FUNCTION();

    gl::Texture_target gl_texture_target = convert_to_gl_texture_target(
        m_type,
        m_sample_count != 0,
        m_array_layer_count != 0
    );

    SPDLOG_LOGGER_TRACE(
        log_texture, "New texture {} {} {}x{}x{} [{}] {} sample count = {}",
        c_str(m_type),
        gl_name(), m_width, m_height, m_depth, m_array_count,
        erhe::dataformat::c_str(m_pixel_format), m_sample_count
    );

    if (!create_info.view_source) {
        const std::string debug_label = fmt::format("(T:{}) {}", gl_name(), m_debug_label);
        gl::object_label(gl::Object_identifier::texture, gl_name(), -1, debug_label.c_str());
    }

    // TODO consider different texture targets
    if (create_info.sample_count > 0) {
        const Format_properties format_properties = device.get_format_properties(create_info.pixelformat);
        for (int sample_count : format_properties.texture_2d_sample_counts) {
            m_sample_count = sample_count;
            if (sample_count >= create_info.sample_count) {
                break;
            }
        }
    }

    const auto dimensions = get_storage_dimensions(gl_texture_target);

    if (create_info.sparse && device.get_info().use_sparse_texture) {
        gl::texture_parameter_i(m_handle.gl_name(), gl::Texture_parameter_name::texture_sparse_arb, GL_TRUE);
        m_is_sparse = true;
    }

    std::optional<gl::Internal_format> internal_format_opt = gl_helpers::convert_to_gl(m_pixelformat);
    ERHE_VERIFY(internal_format_opt.has_value());
    gl::Internal_format internal_format = internal_format_opt.value();

    if (create_info.wrap_texture_name != 0) {
        ERHE_VERIFY(m_type == Texture_type::texture_2d); // TODO is this still correct?
        GLint target_i{0};
        gl::get_texture_parameter_iv(gl_name(), static_cast<gl::Get_texture_parameter>(GL_TEXTURE_TARGET), &target_i);
        gl_texture_target = static_cast<gl::Texture_target>(target_i);
        bool multisample{false};
        bool array{false};
        m_type = convert_from_gl_texture_target(gl_texture_target, multisample, array);

        ERHE_VERIFY(gl::is_texture(gl_name()));
        GLint max_framebuffer_width {0};
        GLint max_framebuffer_height{0};
        gl::get_integer_v(gl::Get_p_name::max_framebuffer_width, &max_framebuffer_width);
        gl::get_integer_v(gl::Get_p_name::max_framebuffer_width, &max_framebuffer_height);
        ERHE_VERIFY(create_info.width  <= max_framebuffer_width);
        ERHE_VERIFY(create_info.height <= max_framebuffer_height);
        GLint width   {0};
        GLint height  {0};
        GLint depth   {0};
        GLint samples {0};
        GLint fixed_sample_locations{0};
        GLint internal_format_i {0};
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_width,  &width);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_height, &height);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_depth,  &depth);
        gl::get_texture_level_parameter_iv(gl_name(), 0, static_cast<gl::Get_texture_parameter>(GL_TEXTURE_SAMPLES),                &samples);
        gl::get_texture_level_parameter_iv(gl_name(), 0, static_cast<gl::Get_texture_parameter>(GL_TEXTURE_FIXED_SAMPLE_LOCATIONS), &fixed_sample_locations);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_internal_format, &internal_format_i);
        internal_format = static_cast<gl::Internal_format>(internal_format_i);

        m_width  = width;
        m_height = height;
        m_depth  = depth;
        convert_texture_dimensions_from_gl(gl_texture_target, m_width, m_height, m_depth, m_array_layer_count);

        ERHE_VERIFY(m_width == create_info.width);
        ERHE_VERIFY(m_height == create_info.height);

        m_sample_count = samples;
        m_pixelformat  = gl_helpers::convert_from_gl(internal_format);
        GLint immutable_format_i{0};
        GLint immutable_levels  {0};
        gl::get_texture_parameter_iv(gl_name(), static_cast<gl::Get_texture_parameter>(GL_TEXTURE_IMMUTABLE_FORMAT), &immutable_format_i);
        gl::get_texture_parameter_iv(gl_name(), static_cast<gl::Get_texture_parameter>(GL_TEXTURE_IMMUTABLE_LEVELS), &immutable_levels);
        GLint red_size    {0};
        GLint green_size  {0};
        GLint blue_size   {0};
        GLint alpha_size  {0};
        GLint depth_size  {0};
        GLint stencil_size{0};
        GLint red_type    {0};
        GLint green_type  {0};
        GLint blue_type   {0};
        GLint alpha_type  {0};
        GLint depth_type  {0};
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_red_size,     &red_size);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_green_size,   &green_size);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_blue_size,    &blue_size);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_alpha_size,   &alpha_size);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_depth_size,   &depth_size);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_stencil_size, &stencil_size);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_red_type,     &red_type);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_green_type,   &green_type);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_blue_type,    &blue_type);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_alpha_type,   &alpha_type);
        gl::get_texture_level_parameter_iv(gl_name(), 0, gl::Get_texture_parameter::texture_depth_type,   &depth_type);
        static_cast<void>(red_size);
        static_cast<void>(green_size);
        static_cast<void>(blue_size);
        static_cast<void>(alpha_size);
        static_cast<void>(depth_size);
        static_cast<void>(stencil_size);
        static_cast<void>(red_type);
        static_cast<void>(green_type);
        static_cast<void>(blue_type);
        static_cast<void>(alpha_type);
        static_cast<void>(depth_type);
        
        //gl::Internal_format immutable_internal_format = static_cast<gl::Internal_format>(immutable_format_i);
        gl::texture_parameter_i(gl_name(), gl::Texture_parameter_name::texture_min_filter, GL_NEAREST);
        gl::texture_parameter_i(gl_name(), gl::Texture_parameter_name::texture_mag_filter, GL_NEAREST);
        m_level_count = get_texture_level_count(m_width, m_height, m_depth);
        return;
    }

    if (create_info.view_source) {
        gl::texture_view(
            gl_name(), gl_texture_target, create_info.view_source->get_impl().gl_name(), internal_format,
            create_info.view_base_level, create_info.level_count, create_info.view_base_array_layer, 1 // TODO layer count
        );
        const std::string debug_label = fmt::format("(T:{}) {} (texture view)", gl_name(), m_debug_label);
        gl::object_label(gl::Object_identifier::texture, gl_name(), -1, debug_label.c_str());
    } else {
        int gl_width  = m_width;
        int gl_height = m_height;
        int gl_depth  = m_depth;
        convert_texture_dimensions_to_gl(gl_texture_target, gl_width, gl_height, gl_depth, m_array_layer_count);
        switch (dimensions) {
            case 0: {
                ERHE_VERIFY(m_buffer != nullptr);
                ERHE_VERIFY(m_sample_count == 0);
                gl::texture_buffer(gl_name(), internal_format, m_buffer->get_impl().gl_name());
                break;
            }

            case 1: {
                ERHE_VERIFY(m_sample_count == 0);
                gl::texture_storage_1d(gl_name(), m_level_count, internal_format, gl_width);
                break;
            }

            case 2: {
                if (m_sample_count == 0) {
                    gl::texture_storage_2d(gl_name(), m_level_count, internal_format, gl_width, gl_height);
                } else {
                    gl::texture_storage_2d_multisample(
                        gl_name(),
                        m_sample_count,
                        internal_format,
                        gl_width,
                        gl_height,
                        m_fixed_sample_locations ? GL_TRUE : GL_FALSE
                    );
                }
                break;
            }

            case 3: {
                gl::texture_storage_3d(gl_name(), m_level_count, internal_format, gl_width, gl_height, gl_depth);
                break;
            }

            default: {
                ERHE_FATAL("Bad texture target");
            }
        }
    }

    SPDLOG_LOGGER_TRACE(
        log_texture,
        "Created texture {} / {} {}x{} {} sample count = {}",
        m_debug_label,
        gl_name(),
        m_width,
        m_height,
        erhe::dataformat::c_str(m_pixelformat),
        m_sample_count
    );
}

auto Texture_impl::is_sparse() const -> bool
{
    return m_is_sparse;
}

auto Texture_impl::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

auto Texture_impl::get_texture_type() const -> Texture_type
{
    return m_type;
}

auto Texture_impl::is_layered() const -> bool
{
    const gl::Texture_target gl_texture_target = convert_to_gl_texture_target(
        m_type,
        m_sample_count != 0,
        m_array_layer_count != 0
    );

    switch (gl_texture_target) {
        //using enum gl::Texture_target;
        case gl::Texture_target::texture_buffer:
        case gl::Texture_target::texture_1d:
        case gl::Texture_target::texture_2d:
        case gl::Texture_target::texture_2d_multisample:
        case gl::Texture_target::texture_rectangle:
        case gl::Texture_target::texture_cube_map:
        case gl::Texture_target::texture_3d: {
            ERHE_VERIFY(m_array_layer_count == 0);
            return false;
        }

        case gl::Texture_target::texture_1d_array:
        case gl::Texture_target::texture_2d_array:
        case gl::Texture_target::texture_2d_multisample_array:
        case gl::Texture_target::texture_cube_map_array: {
            ERHE_VERIFY(m_array_layer_count >= 1);
            return true;
        }

        default: {
            ERHE_FATAL("Bad texture type");
        }
    }
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

auto Texture_impl::get_pixelformat() const -> erhe::dataformat::Format
{
    return m_pixelformat;
}

auto Texture_impl::get_sample_count() const -> int
{
    return m_sample_count;
}

auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    ERHE_VERIFY(lhs.gl_name() != 0);
    ERHE_VERIFY(rhs.gl_name() != 0);

    return lhs.gl_name() == rhs.gl_name();
}

auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

[[nodiscard]] auto gl_name(const Texture& texture) -> GLuint
{
    return texture.get_impl().gl_name();
}

} // namespace erhe::graphics
