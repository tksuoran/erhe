#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <cstddef>
#include <string>
#include <type_traits> // enable_if

namespace erhe::graphics {

enum class Vendor : unsigned int {
    Unknown = 0,
    Nvidia  = 1,
    Amd     = 2,
    Intel   = 3
};

enum class Glsl_type
{
    invalid = 0,
    float_,
    float_vec2,
    float_vec3,
    float_vec4,
    bool_,
    int_,
    int_vec2,
    int_vec3,
    int_vec4,
    unsigned_int,
    unsigned_int_vec2,
    unsigned_int_vec3,
    unsigned_int_vec4,
    unsigned_int64,
    float_mat_2x2,
    float_mat_3x3,
    float_mat_4x4,
    sampler_1d,
    sampler_2d,
    sampler_3d,
    sampler_cube,
    sampler_1d_shadow,
    sampler_2d_shadow,
    sampler_1d_array,
    sampler_2d_array,
    sampler_buffer,
    sampler_1d_array_shadow,
    sampler_2d_array_shadow,
    sampler_cube_shadow,
    int_sampler_1d,
    int_sampler_2d,
    int_sampler_3d,
    int_sampler_cube,
    int_sampler_1d_array,
    int_sampler_2d_array,
    int_sampler_buffer,
    unsigned_int_sampler_1d,
    unsigned_int_sampler_2d,
    unsigned_int_sampler_3d,
    unsigned_int_sampler_cube,
    unsigned_int_sampler_1d_array,
    unsigned_int_sampler_2d_array,
    unsigned_int_sampler_buffer,
    sampler_cube_map_array,
    sampler_cube_map_array_shadow,
    int_sampler_cube_map_array,
    unsigned_int_sampler_cube_map_array,
    sampler_2d_multisample,
    int_sampler_2d_multisample,
    unsigned_int_sampler_2d_multisample,
    sampler_2d_multisample_array,
    int_sampler_2d_multisample_array,
    unsigned_int_sampler_2d_multisample_array,
};

enum class Primitive_type : unsigned int
{
    point          = 0,
    line           = 1,
    line_strip     = 2,
    triangle       = 3,
    triangle_strip = 4
};

enum class Buffer_target : unsigned int
{
    index         = 0,
    vertex        = 1,
    uniform       = 2,
    storage       = 3,
    draw_indirect = 4,
    uniform_texel = 5,
    storage_texel = 6,
    transfer_src  = 7,
    transfer_dst  = 8
};

enum class Texture_type : unsigned int
{
    texture_buffer   = 0,
    texture_1d       = 1,
    texture_2d       = 2,
    texture_3d       = 3,
    texture_cube_map = 4
};

enum class Filter : unsigned int
{
    nearest = 0,
    linear
};

enum class Sampler_mipmap_mode : unsigned int
{
    not_mipmapped = 0,
    nearest,
    linear
};

enum class Sampler_address_mode : unsigned int
{
    repeat = 0,
    clamp_to_edge,
    mirrored_repeat
};

enum class Blend_equation_mode : unsigned int
{
    func_add              = 0,
    func_reverse_subtract = 1,
    func_subtract         = 2,
    max_                  = 3,
    min_                  = 4
};

enum class Blending_factor : unsigned int
{
    zero                     = 0,
    constant_alpha           = 1,
    constant_color           = 2,
    dst_alpha                = 3,
    dst_color                = 4,
    one                      = 5,
    one_minus_constant_alpha = 6,
    one_minus_constant_color = 7,
    one_minus_dst_alpha      = 8,
    one_minus_dst_color      = 9,
    one_minus_src1_alpha     = 10,
    one_minus_src1_color     = 11,
    one_minus_src_alpha      = 12,
    one_minus_src_color      = 13,
    src1_alpha               = 14,
    src1_color               = 15,
    src_alpha                = 16,
    src_alpha_saturate       = 17,
    src_color                = 18
};

enum class Compare_operation : unsigned int
{
    never = 0,
    less,
    equal,
    less_or_equal,
    greater,
    not_equal,
    greater_or_equal,
    always
};

enum class Cull_face_mode : unsigned int
{
    back           = 0,
    front          = 1,
    front_and_back = 2
};

enum class Front_face_direction : unsigned int
{
    ccw = 0,
    cw  = 1
};

enum class Polygon_mode : unsigned int
{
    fill  = 0,
    line  = 1,
    point = 2
};

enum class Shader_type : unsigned int
{
    vertex_shader          = 0,
    fragment_shader        = 1,
    compute_shader         = 2,
    geometry_shader        = 3,
    tess_control_shader    = 4,
    tess_evaluation_shader = 5
};

enum class Stencil_face_direction : unsigned int
{
    back           = 0,
    front          = 1,
    front_and_back = 2
};

enum class Stencil_op : unsigned int
{
    zero      = 0,
    decr      = 1,
    decr_wrap = 2,
    incr      = 3,
    incr_wrap = 4,
    invert    = 5,
    keep      = 6,
    replace   = 7
};

enum class Buffer_usage : unsigned int {
    none          = 0x0000u,
    vertex        = 0x0001u,
    index         = 0x0002u,
    uniform       = 0x0004u,
    storage       = 0x0008u,
    indirect      = 0x0010u,
    uniform_texel = 0x0020u,
    storage_texel = 0x0040u,
    transfer_src  = 0x0100u,
    transfer_dst  = 0x0200u,
    transfer      = 0x0300u
};

enum class Memory_usage : unsigned int {
    cpu_to_gpu = 0,
    gpu_only   = 1,
    gpu_to_cpu = 2
};

class Memory_allocation_create_flag_bit {
public:
    static constexpr uint64_t memoryless           =  0u;
    static constexpr uint64_t dedicated_allocation =  1u;
    static constexpr uint64_t never_allocate       =  2u;
    static constexpr uint64_t mapped               =  3u;
    static constexpr uint64_t upper_address        =  4u;
    static constexpr uint64_t dont_bind            =  5u;
    static constexpr uint64_t within_budget        =  6u;
    static constexpr uint64_t can_alias            =  7u;
    static constexpr uint64_t strategy_min_memory  =  8u;
    static constexpr uint64_t strategy_min_time    =  9u;
    static constexpr uint64_t strategy_min_offset  = 10u;
};

class Memory_allocation_create_flag_bit_mask {
public:
    static constexpr uint64_t none                 = uint64_t{0};
    static constexpr uint64_t dedicated_allocation = uint64_t{1} << Memory_allocation_create_flag_bit::dedicated_allocation              ;
    static constexpr uint64_t never_allocate       = uint64_t{1} << Memory_allocation_create_flag_bit::never_allocate                    ;
    static constexpr uint64_t mapped               = uint64_t{1} << Memory_allocation_create_flag_bit::mapped                            ;
    static constexpr uint64_t upper_address        = uint64_t{1} << Memory_allocation_create_flag_bit::upper_address                     ;
    static constexpr uint64_t dont_bind            = uint64_t{1} << Memory_allocation_create_flag_bit::dont_bind                         ;
    static constexpr uint64_t within_budget        = uint64_t{1} << Memory_allocation_create_flag_bit::within_budget                     ;
    static constexpr uint64_t can_alias            = uint64_t{1} << Memory_allocation_create_flag_bit::can_alias                         ;
    static constexpr uint64_t strategy_min_memory  = uint64_t{1} << Memory_allocation_create_flag_bit::strategy_min_memory               ;
    static constexpr uint64_t strategy_min_time    = uint64_t{1} << Memory_allocation_create_flag_bit::strategy_min_time                 ;
    static constexpr uint64_t strategy_min_offset  = uint64_t{1} << Memory_allocation_create_flag_bit::strategy_min_offset               ;
};

class Memory_property_flag_bit {
public:
    static constexpr uint64_t host_read         = 0u;
    static constexpr uint64_t host_write        = 1u;
    static constexpr uint64_t host_coherent     = 2u;
    static constexpr uint64_t host_persistent   = 3u;
    static constexpr uint64_t host_cached       = 4u;
    static constexpr uint64_t device_local      = 5u;
    static constexpr uint64_t lazily_allocated  = 6u;
};

class Memory_property_flag_bit_mask {
public:
    static constexpr uint64_t none             = uint64_t{0};
    static constexpr uint64_t host_read        = uint64_t{1} << Memory_property_flag_bit::host_read       ;
    static constexpr uint64_t host_write       = uint64_t{1} << Memory_property_flag_bit::host_write      ;
    static constexpr uint64_t host_coherent    = uint64_t{1} << Memory_property_flag_bit::host_coherent   ;
    static constexpr uint64_t host_persistent  = uint64_t{1} << Memory_property_flag_bit::host_persistent ;
    static constexpr uint64_t host_cached      = uint64_t{1} << Memory_property_flag_bit::host_cached     ;
    static constexpr uint64_t device_local     = uint64_t{1} << Memory_property_flag_bit::device_local    ;
    static constexpr uint64_t lazily_allocated = uint64_t{1} << Memory_property_flag_bit::lazily_allocated;
};

[[nodiscard]] auto get_memory_usage_from_memory_properties(uint64_t memory_property_bit_mask) -> Memory_usage;

enum class Buffer_map_flags : unsigned int {
    none              = 0x00,
    invalidate_range  = 0x01,
    invalidate_buffer = 0x02,
    explicit_flush    = 0x04
};

enum class Memory_barrier_mask : unsigned int {
    all_barrier_bits                 = 0xFFFFFFFFu,
    atomic_counter_barrier_bit       = 0x00001000u,
    buffer_update_barrier_bit        = 0x00000200u,
    client_mapped_buffer_barrier_bit = 0x00004000u,
    command_barrier_bit              = 0x00000040u,
    element_array_barrier_bit        = 0x00000002u,
    framebuffer_barrier_bit          = 0x00000400u,
    pixel_buffer_barrier_bit         = 0x00000080u,
    query_buffer_barrier_bit         = 0x00008000u,
    shader_image_access_barrier_bit  = 0x00000020u,
    shader_storage_barrier_bit       = 0x00002000u,
    texture_fetch_barrier_bit        = 0x00000008u,
    texture_update_barrier_bit       = 0x00000100u,
    transform_feedback_barrier_bit   = 0x00000800u,
    uniform_barrier_bit              = 0x00000004u,
    vertex_attrib_array_barrier_bit  = 0x00000001u
};

enum class Ring_buffer_usage : unsigned int
{
    None       = 0,
    CPU_write  = 1,
    CPU_read   = 2,
    GPU_access = 3
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

template<> struct Enable_bit_mask_operators<Buffer_usage       > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Buffer_map_flags   > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Memory_barrier_mask> { static const bool enable = true; };

[[nodiscard]] auto to_string       (Buffer_usage      usage      ) -> std::string;
[[nodiscard]] auto c_str           (Memory_usage      memory_sage) -> const char*;
[[nodiscard]] auto to_string_memory_allocation_create_flag_bit_mask(uint64_t mask) -> std::string;
[[nodiscard]] auto to_string_memory_property_flag_bit_mask         (uint64_t mask) -> std::string;
[[nodiscard]] auto to_string       (Buffer_map_flags  flags     ) -> std::string;
[[nodiscard]] auto get_buffer_usage(Buffer_target     target    ) -> Buffer_usage;

[[nodiscard]] auto to_glsl_attribute_type(const erhe::dataformat::Format format) -> Glsl_type;
[[nodiscard]] auto glsl_type_c_str(const Glsl_type            type) -> const char*;
[[nodiscard]] auto get_dimension  (const Glsl_type            type) -> std::size_t;
[[nodiscard]] auto c_str          (const Primitive_type       primitive_type) -> const char*;
[[nodiscard]] auto c_str          (const Buffer_target        buffer_target) -> const char*;
[[nodiscard]] auto is_indexed     (const Buffer_target        buffer_target) -> bool;
[[nodiscard]] auto c_str          (const Texture_type         texture_type) -> const char*;
[[nodiscard]] auto c_str          (const Filter               filter) -> const char*;
[[nodiscard]] auto c_str          (const Sampler_mipmap_mode  sampler_mipmap_mode) -> const char*;
[[nodiscard]] auto c_str          (const Sampler_address_mode sampler_address_mode) -> const char*;
[[nodiscard]] auto c_str          (const Compare_operation    compare_operation) -> const char*;
[[nodiscard]] auto c_str          (const Shader_type          shader_type) -> const char*;

class Image_usage_flag_bit
{
public:
    static constexpr uint64_t transfer_src             = 0;
    static constexpr uint64_t transfer_dst             = 1;
    static constexpr uint64_t sampled                  = 2;
    static constexpr uint64_t storage                  = 3;
    static constexpr uint64_t color_attachment         = 4;
    static constexpr uint64_t depth_stencil_attachment = 5;
    static constexpr uint64_t transient_attachment     = 6;
    static constexpr uint64_t input_attachment         = 7;
    static constexpr uint64_t host_transfer            = 8;
};

class Image_usage_flag_bit_mask
{
public:
    static constexpr uint64_t none                     = uint64_t{0};
    static constexpr uint64_t transfer_src             = uint64_t{1} << Image_usage_flag_bit::transfer_src            ;
    static constexpr uint64_t transfer_dst             = uint64_t{1} << Image_usage_flag_bit::transfer_dst            ;
    static constexpr uint64_t sampled                  = uint64_t{1} << Image_usage_flag_bit::sampled                 ;
    static constexpr uint64_t storage                  = uint64_t{1} << Image_usage_flag_bit::storage                 ;
    static constexpr uint64_t color_attachment         = uint64_t{1} << Image_usage_flag_bit::color_attachment        ;
    static constexpr uint64_t depth_stencil_attachment = uint64_t{1} << Image_usage_flag_bit::depth_stencil_attachment;
    static constexpr uint64_t transient_attachment     = uint64_t{1} << Image_usage_flag_bit::transient_attachment    ;
    static constexpr uint64_t input_attachment         = uint64_t{1} << Image_usage_flag_bit::input_attachment        ;
    static constexpr uint64_t host_transfer            = uint64_t{1} << Image_usage_flag_bit::host_transfer           ;
};

} // namespace erhe::graphics
