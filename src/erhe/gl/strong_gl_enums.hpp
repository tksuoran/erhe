#ifndef strong_gl_enums_hpp_erhe_gl
#define strong_gl_enums_hpp_erhe_gl

#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/enum_base_zero_functions.hpp"
#include "erhe/gl/gl.hpp"
#include <type_traits>

namespace gl
{

class Draw_arrays_indirect_command
{
public:
    uint32_t count;
    uint32_t instance_count;
    uint32_t first;
    uint32_t base_instance;
};

class Draw_elements_indirect_command
{
public:
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t base_vertex;
    uint32_t base_instance;
};

template<typename Enum>
struct Enable_bit_mask_operators
{
    static const bool enable = false;
};

template<typename Enum>
constexpr auto
operator |(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum> (static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template<typename Enum>
constexpr auto
operator &(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum> (static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

template<> struct Enable_bit_mask_operators<Buffer_storage_mask   > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Clear_buffer_mask     > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Map_buffer_access_mask> { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Memory_barrier_mask   > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Sync_object_mask      > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Use_program_stage_mask> { static const bool enable = true; };

}

#endif

#if 0

enum begin_feedback_mode
{
    points    = ((unsigned int)0x0000),
    lines     = ((unsigned int)0x0001),
    triangles = ((unsigned int)0x0004),
};

enum blit_framebuffer_filter
{
    nearest = ((unsigned int)0x2600),
    linear  = ((unsigned int)0x2601),
};

enum draw_elements_type
{
    unsigned_byte  = ((unsigned int)0x1401),
    unsigned_short = ((unsigned int)0x1403),
    unsigned_int   = ((unsigned int)0x1405),
};

enum framebuffer_error_code
{
    framebuffer_undefined                     = ((unsigned int)0x8219),
    framebuffer_complete                      = ((unsigned int)0x8CD5),
    framebuffer_incomplete_attachment         = ((unsigned int)0x8CD6),
    framebuffer_incomplete_missing_attachment = ((unsigned int)0x8CD7),
    framebuffer_incomplete_draw_buffer        = ((unsigned int)0x8CDB),
    framebuffer_incomplete_read_buffer        = ((unsigned int)0x8CDC),
    framebuffer_unsupported                   = ((unsigned int)0x8CDD),
    framebuffer_incomplete_multisample        = ((unsigned int)0x8D56),
    framebuffer_incomplete_layer_targets      = ((unsigned int)0x8DA8),
    framebuffer_incomplete_layer_count        = ((unsigned int)0x8DA9),
};

enum sampler_parameter
{
    texture_border_color       = ((unsigned int)0x1004),
    texture_mag_filter         = ((unsigned int)0x2800),
    texture_min_filter         = ((unsigned int)0x2801),
    texture_wrap_s             = ((unsigned int)0x2802),
    texture_wrap_t             = ((unsigned int)0x2803),
    texture_wrap_r             = ((unsigned int)0x8072),
    texture_min_lod            = ((unsigned int)0x813a),
    texture_max_lod            = ((unsigned int)0x813b),
    texture_max_anisotropy_ext = ((unsigned int)0x84fe),
    texture_lod_bias           = ((unsigned int)0x8501),
    texture_compare_mode       = ((unsigned int)0x884c),
    texture_compare_func       = ((unsigned int)0x884d),
};

enum texture_parameter_name
{
    texture_border_color       = ((unsigned int)0x1004),
    texture_mag_filter         = ((unsigned int)0x2800),
    texture_min_filter         = ((unsigned int)0x2801),
    texture_wrap_s             = ((unsigned int)0x2802),
    texture_wrap_t             = ((unsigned int)0x2803),
    texture_priority           = ((unsigned int)0x8066),
    texture_depth              = ((unsigned int)0x8071),
    texture_wrap_r             = ((unsigned int)0x8072),
    texture_compare_fail_value = ((unsigned int)0x80bf),
    texture_min_lod      = ((unsigned int)0x813a),
    texture_max_lod      = ((unsigned int)0x813b),
    texture_base_level   = ((unsigned int)0x813c),
    texture_max_level    = ((unsigned int)0x813d),
    generate_mipmap      = ((unsigned int)0x8191),
    texture_lod_bias     = ((unsigned int)0x8501),
    depth_texture_mode   = ((unsigned int)0x884b),
    texture_compare_mode = ((unsigned int)0x884c),
    texture_compare_func = ((unsigned int)0x884d),
    texture_swizzle_r    = ((unsigned int)0x8e42),
    texture_swizzle_g    = ((unsigned int)0x8e43),
    texture_swizzle_b    = ((unsigned int)0x8e44),
    texture_swizzle_a    = ((unsigned int)0x8e45),
    texture_swizzle_rgba = ((unsigned int)0x8e46),
};

enum texture_level_parameter_name
{
    texture_width                 = ((unsigned int)0x1000),
    texture_height                = ((unsigned int)0x1001),
    texture_depth                 = ((unsigned int)0x8071),
    texture_internal_format       = ((unsigned int)0x1003),
    texture_border                = ((unsigned int)0x1005),
    texture_red_size              = ((unsigned int)0x805C),
    texture_green_size            = ((unsigned int)0x805D),
    texture_blue_size             = ((unsigned int)0x805E),
    texture_alpha_size            = ((unsigned int)0x805F),
    texture_depth_size            = ((unsigned int)0x884A),
    texture_compressed            = ((unsigned int)0x86A1),
    texture_compressed_image_size = ((unsigned int)0x86A0)
};

} // namespace gl

#endif
