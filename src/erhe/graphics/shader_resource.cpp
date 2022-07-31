#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/toolkit/verify.hpp"

#include <algorithm>

namespace erhe::graphics
{

namespace
{

auto glsl_token(const gl::Uniform_type type) -> const char*
{
    switch (type)
    {
        //using enum gl::Uniform_type;
        case gl::Uniform_type::int_:               return "int      ";
        case gl::Uniform_type::int_vec2:           return "ivec2    ";
        case gl::Uniform_type::int_vec3:           return "ivec3    ";
        case gl::Uniform_type::int_vec4:           return "ivec4    ";
        case gl::Uniform_type::unsigned_int:       return "uint     ";
        case gl::Uniform_type::unsigned_int_vec2:  return "uvec2    ";
        case gl::Uniform_type::unsigned_int_vec3:  return "uvec3    ";
        case gl::Uniform_type::unsigned_int_vec4:  return "uvec4    ";
        case gl::Uniform_type::unsigned_int64_arb: return "uint64_t ";
        case gl::Uniform_type::float_:             return "float    ";
        case gl::Uniform_type::float_vec2:         return "vec2     ";
        case gl::Uniform_type::float_vec3:         return "vec3     ";
        case gl::Uniform_type::float_vec4:         return "vec4     ";
        case gl::Uniform_type::float_mat4:         return "mat4     ";
        case gl::Uniform_type::double_:            return "double   ";
        case gl::Uniform_type::double_vec2:        return "dvec2    ";
        case gl::Uniform_type::double_vec3:        return "dvec3    ";
        case gl::Uniform_type::double_vec4:        return "dvec4    ";
        case gl::Uniform_type::double_mat4:        return "dmat4    ";

        case gl::Uniform_type::sampler_1d:                                  return "sampler1D";
        case gl::Uniform_type::sampler_2d:                                  return "sampler2D";
        case gl::Uniform_type::sampler_3d:                                  return "sampler3D";
        case gl::Uniform_type::sampler_cube:                                return "samplerCube";
        case gl::Uniform_type::sampler_1d_shadow:                           return "sampler1DShadow";
        case gl::Uniform_type::sampler_2d_shadow:                           return "sampler2DShadow";
        case gl::Uniform_type::sampler_2d_rect:                             return "sampler2DRect";
        case gl::Uniform_type::sampler_2d_rect_shadow:                      return "sampler2DRectShadow";
        case gl::Uniform_type::sampler_1d_array:                            return "sampler1DArray";
        case gl::Uniform_type::sampler_2d_array:                            return "sampler2DArray";
        case gl::Uniform_type::sampler_buffer:                              return "samplerBuffer";
        case gl::Uniform_type::sampler_1d_array_shadow:                     return "sampler1DArrayShadow";
        case gl::Uniform_type::sampler_2d_array_shadow:                     return "sampler2DArrayShadow";
        case gl::Uniform_type::sampler_cube_shadow:                         return "samplerCubeShadow";
        case gl::Uniform_type::int_sampler_1d:                              return "intSampler1D";
        case gl::Uniform_type::int_sampler_2d:                              return "intSampler2D";
        case gl::Uniform_type::int_sampler_3d:                              return "intSampler3D";
        case gl::Uniform_type::int_sampler_cube:                            return "intSamplerCube";
        case gl::Uniform_type::int_sampler_2d_rect:                         return "intSampler2DRect";
        case gl::Uniform_type::int_sampler_1d_array:                        return "intSampler1DArray";
        case gl::Uniform_type::int_sampler_2d_array:                        return "intSampler2DArray";
        case gl::Uniform_type::int_sampler_buffer:                          return "intSamplerBuffer";
        case gl::Uniform_type::unsigned_int_sampler_1d:                     return "unsignedIntSampler1D";
        case gl::Uniform_type::unsigned_int_sampler_2d:                     return "unsignedIntSampler2D";
        case gl::Uniform_type::unsigned_int_sampler_3d:                     return "unsignedIntSampler3D";
        case gl::Uniform_type::unsigned_int_sampler_cube:                   return "unsignedIntSamplerCube";
        case gl::Uniform_type::unsigned_int_sampler_2d_rect:                return "unsignedIntSampler2DRect";
        case gl::Uniform_type::unsigned_int_sampler_1d_array:               return "unsignedIntSampler1DArray";
        case gl::Uniform_type::unsigned_int_sampler_2d_array:               return "unsignedIntSampler2DArray";
        case gl::Uniform_type::unsigned_int_sampler_buffer:                 return "unsignedIntSamplerBuffer";
        case gl::Uniform_type::sampler_cube_map_array:                      return "samplerCubemapArray";
        case gl::Uniform_type::sampler_cube_map_array_shadow:               return "samplerCubemapArrayShadow";
        case gl::Uniform_type::int_sampler_cube_map_array:                  return "intSamplerCubemapArray";
        case gl::Uniform_type::unsigned_int_sampler_cube_map_array:         return "unsignedIntSamplerCubemapArray";
        case gl::Uniform_type::sampler_2d_multisample:                      return "sampler2DMultisampler";
        case gl::Uniform_type::int_sampler_2d_multisample:                  return "intSampler2DMultisampler";
        case gl::Uniform_type::unsigned_int_sampler_2d_multisample:         return "unsignedIntSampler2DMultisample";
        case gl::Uniform_type::sampler_2d_multisample_array:                return "sampler2DMultisampleArray";
        case gl::Uniform_type::int_sampler_2d_multisample_array:            return "intSampler2DMultisampleArray";
        case gl::Uniform_type::unsigned_int_sampler_2d_multisample_array:   return "unsignedIntSampler2DMultisampleArray";

        default:
        {
            ERHE_FATAL("Bad uniform type");
        }
    }
}

static constexpr unsigned int sampler_flags_is_sampler  = 0b000001u;
static constexpr unsigned int sampler_flags_multisample = 0b000010u;
static constexpr unsigned int sampler_flags_cube        = 0b000100u;
static constexpr unsigned int sampler_flags_array       = 0b001000u;
static constexpr unsigned int sampler_flags_shadow      = 0b010000u;
static constexpr unsigned int sampler_flags_rectangle   = 0b100000u;

class Type_details
{
public:
    Type_details(
        const gl::Uniform_type type,
        const gl::Uniform_type basic_type,
        const std::size_t      component_count
    )
        : type           {type}
        , basic_type     {basic_type}
        , component_count{component_count}
    {
    }

    Type_details(
        const gl::Uniform_type   type,
        const gl::Uniform_type   basic_type,
        const gl::Texture_target texture_target,
        const unsigned int       sampler_dimension,
        const unsigned int       sampler_mask
    )
        : type             {type}
        , basic_type       {basic_type}
        , texture_target   {texture_target}
        , sampler_dimension{sampler_dimension}
        , sampler_mask     {sampler_flags_is_sampler | sampler_mask}
    {
    }

    [[nodiscard]] auto is_sampler    () const -> bool { return sampler_mask != 0; }
    [[nodiscard]] auto is_multisample() const -> bool { return (sampler_mask & sampler_flags_multisample) != 0; }
    [[nodiscard]] auto is_cube       () const -> bool { return (sampler_mask & sampler_flags_cube       ) != 0; }
    [[nodiscard]] auto is_array      () const -> bool { return (sampler_mask & sampler_flags_array      ) != 0; }
    [[nodiscard]] auto is_shadow     () const -> bool { return (sampler_mask & sampler_flags_shadow     ) != 0; }
    [[nodiscard]] auto is_rectangle  () const -> bool { return (sampler_mask & sampler_flags_rectangle  ) != 0; }

    gl::Uniform_type   type             {gl::Uniform_type::bool_};
    gl::Uniform_type   basic_type       {gl::Uniform_type::bool_};
    gl::Texture_target texture_target   {gl::Texture_target::texture_1d};
    std::size_t        component_count  {0};
    std::size_t        sampler_dimension{0};
    unsigned int       sampler_mask     {0};
};

auto get_type_details(const gl::Uniform_type type) -> Type_details
{
    switch (type)
    {
        //using enum gl::Uniform_type;
        case gl::Uniform_type::int_:                                        return Type_details(type, gl::Uniform_type::int_,               1);
        case gl::Uniform_type::int_vec2:                                    return Type_details(type, gl::Uniform_type::int_,               2);
        case gl::Uniform_type::int_vec3:                                    return Type_details(type, gl::Uniform_type::int_,               3);
        case gl::Uniform_type::int_vec4:                                    return Type_details(type, gl::Uniform_type::int_,               4);
        case gl::Uniform_type::unsigned_int:                                return Type_details(type, gl::Uniform_type::unsigned_int,       1);
        case gl::Uniform_type::unsigned_int_vec2:                           return Type_details(type, gl::Uniform_type::unsigned_int,       2);
        case gl::Uniform_type::unsigned_int_vec3:                           return Type_details(type, gl::Uniform_type::unsigned_int,       3);
        case gl::Uniform_type::unsigned_int_vec4:                           return Type_details(type, gl::Uniform_type::unsigned_int,       4);
        case gl::Uniform_type::unsigned_int64_arb:                          return Type_details(type, gl::Uniform_type::unsigned_int64_arb, 1);
        case gl::Uniform_type::float_:                                      return Type_details(type, gl::Uniform_type::float_,             1);
        case gl::Uniform_type::float_vec2:                                  return Type_details(type, gl::Uniform_type::float_,             2);
        case gl::Uniform_type::float_vec3:                                  return Type_details(type, gl::Uniform_type::float_,             3);
        case gl::Uniform_type::float_vec4:                                  return Type_details(type, gl::Uniform_type::float_,             4);
        case gl::Uniform_type::float_mat4:                                  return Type_details(type, gl::Uniform_type::float_vec4,         4);
        case gl::Uniform_type::double_:                                     return Type_details(type, gl::Uniform_type::double_,            1);
        case gl::Uniform_type::double_vec2:                                 return Type_details(type, gl::Uniform_type::double_,            2);
        case gl::Uniform_type::double_vec3:                                 return Type_details(type, gl::Uniform_type::double_,            3);
        case gl::Uniform_type::double_vec4:                                 return Type_details(type, gl::Uniform_type::double_,            4);
        case gl::Uniform_type::double_mat4:                                 return Type_details(type, gl::Uniform_type::double_vec4,        4);

        case gl::Uniform_type::sampler_1d:                                  return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d,        1, 0);
        case gl::Uniform_type::sampler_2d:                                  return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d,        2, 0);
        case gl::Uniform_type::sampler_3d:                                  return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_3d,        3, 0);
        case gl::Uniform_type::sampler_cube:                                return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_cube_map,  2, sampler_flags_cube);

        case gl::Uniform_type::sampler_1d_shadow:                           return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d,        1, sampler_flags_shadow);
        case gl::Uniform_type::sampler_2d_shadow:                           return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d,        2, sampler_flags_shadow);
        case gl::Uniform_type::sampler_2d_rect:                             return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_rectangle, 2, sampler_flags_rectangle);
        case gl::Uniform_type::sampler_2d_rect_shadow:                      return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_rectangle, 2, sampler_flags_shadow | sampler_flags_rectangle);
        case gl::Uniform_type::sampler_1d_array:                            return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d_array,  1, sampler_flags_array);
        case gl::Uniform_type::sampler_2d_array:                            return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d_array,  2, sampler_flags_array);
        case gl::Uniform_type::sampler_buffer:                              return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_buffer,    1, 0);
        case gl::Uniform_type::sampler_1d_array_shadow:                     return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d_array,  1, sampler_flags_array | sampler_flags_shadow);
        case gl::Uniform_type::sampler_2d_array_shadow:                     return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d_array,  2, sampler_flags_array | sampler_flags_shadow);
        case gl::Uniform_type::sampler_cube_shadow:                         return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_cube_map,  2, sampler_flags_cube | sampler_flags_shadow);

        case gl::Uniform_type::int_sampler_1d:                              return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_1d,        1, 0);
        case gl::Uniform_type::int_sampler_2d:                              return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_2d,        2, 0);
        case gl::Uniform_type::int_sampler_3d:                              return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_3d,        3, 0);
        case gl::Uniform_type::int_sampler_cube:                            return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_cube_map,  2, sampler_flags_cube);
        case gl::Uniform_type::int_sampler_2d_rect:                         return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_rectangle, 2, sampler_flags_rectangle);
        case gl::Uniform_type::int_sampler_1d_array:                        return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_1d_array,  1, sampler_flags_array);
        case gl::Uniform_type::int_sampler_2d_array:                        return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_2d_array,  2, sampler_flags_array);
        case gl::Uniform_type::int_sampler_buffer:                          return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_buffer,    1, 0);

        case gl::Uniform_type::unsigned_int_sampler_1d:                     return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_1d,        1, 0);
        case gl::Uniform_type::unsigned_int_sampler_2d:                     return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d,        2, 0);
        case gl::Uniform_type::unsigned_int_sampler_3d:                     return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_3d,        3, 0);
        case gl::Uniform_type::unsigned_int_sampler_cube:                   return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_cube_map,  2, sampler_flags_cube);
        case gl::Uniform_type::unsigned_int_sampler_2d_rect:                return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_rectangle, 2, sampler_flags_rectangle);
        case gl::Uniform_type::unsigned_int_sampler_1d_array:               return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_1d_array,  1, sampler_flags_array);
        case gl::Uniform_type::unsigned_int_sampler_2d_array:               return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d_array,  2, sampler_flags_array);
        case gl::Uniform_type::unsigned_int_sampler_buffer:                 return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_buffer,    1, 0);

        case gl::Uniform_type::sampler_cube_map_array:                      return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_cube_map_array,  2, sampler_flags_cube | sampler_flags_array);
        case gl::Uniform_type::sampler_cube_map_array_shadow:               return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_cube_map_array,  2, sampler_flags_cube | sampler_flags_array | sampler_flags_shadow);

        case gl::Uniform_type::int_sampler_cube_map_array:                  return Type_details(type, gl::Uniform_type::int_,         gl::Texture_target::texture_cube_map_array,  2, sampler_flags_cube | sampler_flags_array);
        case gl::Uniform_type::unsigned_int_sampler_cube_map_array:         return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_cube_map_array,  2, sampler_flags_cube | sampler_flags_array);

        case gl::Uniform_type::sampler_2d_multisample:                      return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_2d_multisample,  2, sampler_flags_multisample);
        case gl::Uniform_type::int_sampler_2d_multisample:                  return Type_details(type, gl::Uniform_type::int_,         gl::Texture_target::texture_2d_multisample,  2, sampler_flags_multisample);
        case gl::Uniform_type::unsigned_int_sampler_2d_multisample:         return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d_multisample,  2, sampler_flags_multisample);

        case gl::Uniform_type::sampler_2d_multisample_array:                return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_2d_multisample,  2, sampler_flags_multisample | sampler_flags_array);
        case gl::Uniform_type::int_sampler_2d_multisample_array:            return Type_details(type, gl::Uniform_type::int_,         gl::Texture_target::texture_2d_multisample,  2, sampler_flags_multisample | sampler_flags_array);
        case gl::Uniform_type::unsigned_int_sampler_2d_multisample_array:   return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d_multisample,  2, sampler_flags_multisample | sampler_flags_array);

        default:
        {
            ERHE_FATAL("Bad uniform type");
        }
    }
}

auto get_type_size(const gl::Uniform_type type) -> std::size_t
{
    switch (type)
    {
        //using enum gl::Uniform_type;
        case gl::Uniform_type::int_:               return 4;
        case gl::Uniform_type::unsigned_int:       return 4;
        case gl::Uniform_type::unsigned_int64_arb: return 8;
        case gl::Uniform_type::float_:             return 4;
        case gl::Uniform_type::double_:            return 8;
        default:
        {
            auto type_details = get_type_details(type);
            if (type_details.is_sampler())
            {
                return 0; // samplers are opaque types and as such do not have size
            }
            return type_details.component_count * get_type_size(type_details.basic_type);
        }
    }
}

auto get_pack_size(const gl::Uniform_type type) -> std::size_t
{
    const auto type_details = get_type_details(type);
    if (type_details.is_sampler())
    {
        return 0; // samplers are opaque types and as such do not have size
    }

    return type_details.component_count * get_type_size(type_details.basic_type);
}

} // anonymous namespace

auto Shader_resource::is_basic(const Type type) -> bool
{
    switch (type)
    {
        //using enum Type;
        case Type::basic                : return true;
        case Type::sampler              : return true;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return false;
        case Type::uniform_block        : return false;
        case Type::shader_storage_block : return false;
        default:
        {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::is_aggregate(const Shader_resource::Type type) -> bool
{
    switch (type)
    {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::should_emit_members(const Shader_resource::Type type) -> bool
{
    switch (type)
    {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::is_block(const Shader_resource::Type type) -> bool
{
    switch (type)
    {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::uses_binding_points(const Shader_resource::Type type) -> bool
{
    switch (type)
    {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return false;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::c_str(const Shader_resource::Precision precision) -> const char*
{
    switch (precision)
    {
        //using enum Shader_resource::Precision;
        case Shader_resource::Precision::lowp:    return "lowp";
        case Shader_resource::Precision::mediump: return "mediump";
        case Shader_resource::Precision::highp:   return "highp";
        case Shader_resource::Precision::superp:  return "superp";
        default:
        {
            ERHE_FATAL("Bad uniform precision");
        }
    }
};

// Struct type
Shader_resource::Shader_resource(
    const std::string_view struct_type_name,
    Shader_resource*       parent /* = nullptr */
)
    : m_type            {Type::struct_type}
    , m_name            {struct_type_name}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->next_member_offset() : 0}
{
}

// Struct member
Shader_resource::Shader_resource(
    const std::string_view              struct_member_name,
    gsl::not_null<Shader_resource*>     struct_type,
    const nonstd::optional<std::size_t> array_size /* = {} */,
    Shader_resource*                    parent /* = nullptr */
)
    : m_type            {Type::struct_member}
    , m_name            {struct_member_name}
    , m_array_size      {array_size}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->next_member_offset() : 0}
    , m_struct_type     {struct_type}
{
}

// Block (uniform block or shader storage block)
Shader_resource::Shader_resource(
    const std::string_view              name,
    const int                           binding_point,
    const Type                          type,
    const nonstd::optional<std::size_t> array_size /* = {} */
)
    : m_type         {type}
    , m_name         {name}
    , m_array_size   {array_size}
    , m_binding_point{binding_point}
{
    //// Would be nice to be able to verify this at the time of construction,
    //// but Instance might not be ready yet.
    ////
    //// if (type == Type::uniform_block)
    //// {
    ////     ERHE_VERIFY(binding_point < Instance::limits.max_uniform_buffer_bindings);
    //// }
    //// if (type == Type::shader_storage_block)
    //// {
    ////     ERHE_VERIFY(binding_point < Instance::limits.max_shader_storage_buffer_bindings);
    //// }
    //// if (type == Type::sampler)
    //// {
    ////     // TODO Which limit to use?
    ////     ERHE_VERIFY(binding_point < Instance::limits.max_combined_texture_image_units);
    //// }
}

// Basic type
Shader_resource::Shader_resource(
    const std::string_view              basic_name,
    const gl::Uniform_type              basic_type,
    const nonstd::optional<std::size_t> array_size /* = {} */,
    Shader_resource*                    parent /* = nullptr */
)
    : m_type            {Type::basic}
    , m_name            {basic_name}
    , m_array_size      {array_size}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->next_member_offset() : 0}
    , m_basic_type      {basic_type}
{
}

// Sampler
Shader_resource::Shader_resource(
    const std::string_view              sampler_name,
    gsl::not_null<Shader_resource*>     parent,
    const int                           location,
    const gl::Uniform_type              sampler_type,
    const nonstd::optional<std::size_t> array_size /* = {} */,
    const nonstd::optional<int>         dedicated_texture_unit /* = {} */
)
    : m_type         {Type::sampler}
    , m_name         {sampler_name}
    , m_array_size   {array_size}
    , m_parent       {parent}
    , m_basic_type   {sampler_type}
    , m_location     {location}
    , m_binding_point{
        dedicated_texture_unit.has_value()
            ? dedicated_texture_unit.value()
            : -1
    }
{
}

// Constructor with no arguments creates default uniform block
Shader_resource::Shader_resource()
    : m_location{0} // next location
{
}

Shader_resource::~Shader_resource() noexcept
{
}

auto Shader_resource::is_array() const -> bool
{
    return m_array_size.has_value();
}

auto Shader_resource::type() const -> Shader_resource::Type
{
    return m_type;
}

auto Shader_resource::name() const -> const std::string&
{
    return m_name;
}

auto Shader_resource::array_size() const -> nonstd::optional<std::size_t>
{
    return m_array_size;
}

auto Shader_resource::basic_type() const -> gl::Uniform_type
{
    Expects(is_basic(m_type));

    return m_basic_type;
}

// Only? for uniforms in default uniform block
// For default uniform block, this is the next available location.
auto Shader_resource::location() const -> int
{
    Expects(m_parent != nullptr);
    Expects(m_parent->type() == Type::default_uniform_block);

    return m_location;
}

auto Shader_resource::index_in_parent() const -> std::size_t
{
    Expects(m_parent != nullptr);
    Expects(is_aggregate(m_parent->type()));

    return m_index_in_parent; // TODO
}

auto Shader_resource::offset_in_parent() const -> std::size_t
{
    Expects(m_parent != nullptr);
    Expects(is_aggregate(m_parent->type()));

    return m_offset_in_parent; // TODO
}

auto Shader_resource::parent() const -> Shader_resource*
{
    return m_parent;
}

auto Shader_resource::member_count() const -> std::size_t
{
    Expects(is_aggregate(m_type));

    return m_members.size();
}

auto Shader_resource::member(const std::string_view name) const -> Shader_resource*
{
    for (const auto& member : m_members)
    {
        if (member->name() == name)
        {
            return member.get();
        }
    }

    return {};
}

auto Shader_resource::binding_point() const -> unsigned int
{
    Expects(uses_binding_points(m_type));
    Expects(m_binding_point >= 0);
    return static_cast<unsigned int>(m_binding_point);
}

auto Shader_resource::size_bytes() const -> std::size_t
{
    if (m_type == Type::struct_type)
    {
        // Assume all members have been added
        return m_offset;
    }

    if (m_type == Type::struct_member)
    {
        std::size_t struct_size = m_struct_type->size_bytes();
        std::size_t array_multiplier{1};
        if (m_array_size.has_value() && (m_array_size.value() > 0))
        {
            array_multiplier = m_array_size.value();

            // arrays in uniform block are packed with std140
            if ((m_parent != nullptr) && (m_parent->type() == Type::uniform_block))
            {
                while ((struct_size % 16) != 0)
                {
                    ++struct_size;
                }
            }
        }

        return struct_size * array_multiplier;
    }

    // TODO Shader storage buffer alignment?
    if (is_block(m_type))
    {
        std::size_t padded_size = m_offset;
        while ((padded_size % Instance::implementation_defined.uniform_buffer_offset_alignment) != 0)
        {
            ++padded_size;
        }

        return padded_size;
    }
    else
    {
        std::size_t element_size = get_pack_size(m_basic_type);
        std::size_t array_multiplier{1};
        if (m_array_size.has_value() && (m_array_size.value() > 0))
        {
            array_multiplier = m_array_size.value();

            // arrays in uniform block are packed with std140
            if ((m_parent != nullptr) && (m_parent->type() == Type::uniform_block))
            {
                while ((element_size % 16) != 0)
                {
                    ++element_size;
                }
            }
        }

        return element_size * array_multiplier;
    }
}

auto Shader_resource::offset() const -> std::size_t
{
    Expects(is_aggregate(m_type));
    return m_offset;
}

auto Shader_resource::next_member_offset() const -> std::size_t
{
    Expects(is_aggregate(m_type));
    return m_offset;
}

auto Shader_resource::type_string() const -> std::string
{
    if ((m_parent != nullptr) && (m_parent->type() == Type::default_uniform_block))
    {
        Expects(m_type != Type::default_uniform_block);
        return "uniform ";
    }

    switch (m_type)
    {
        //using enum Type;
        case Type::basic:
        {
            return " ";
        }

        case Type::struct_type:
        {
            return "struct ";
        }

        case Type::struct_member:
        {
            return m_struct_type->name() + " ";
        }

        case Type::default_uniform_block:
        {
            Expects(m_parent == nullptr);
            return "";
        }

        case Type::uniform_block:
        {
            Expects(m_parent == nullptr);
            return "uniform ";
        }

        case Type::shader_storage_block:
        {
            Expects(m_parent == nullptr);
            return "buffer ";
        }

        case Type::sampler:
        {
            ERHE_FATAL("Samplers are only allowed in default uniform block");
        }

        default:
        {
            ERHE_FATAL("Bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::layout_string() const -> std::string
{
    if (
        (m_location      == -1) &&
        (m_binding_point == -1))
    {
        return {};
    }

    std::stringstream ss;
    bool first{true};

    ss << "layout(";
    if (m_type == Type::uniform_block)
    {
        ss << "std140";
        first = false;
    }
    else if (m_type == Type::shader_storage_block)
    {
        ss << "std430";
        first = false;
    }
    if (m_location != -1)
    {
        if (!first)
        {
            ss << ", ";
        }
        ss << "location = " << m_location;
        first = false;
    }
    if ((m_parent != nullptr) && (m_parent->type() == Type::struct_type))
    {
        if (!first)
        {
            ss << ", ";
        }
        ss << "offset = " << m_offset_in_parent;
        first = false;
    }
    if (m_binding_point != -1)
    {
        if (!first)
        {
            ss << ", ";
        }
        ss << "binding = " << m_binding_point;
        //first = false;
    }
    ss << ") ";
    return ss.str();
}

auto Shader_resource::source(
    const int indent_level /* = 0 */
) const -> std::string
{
    std::stringstream ss;

    indent(ss, indent_level);

    if (m_type != Type::default_uniform_block)
    {
        ss << layout_string();
        ss << type_string();
    }

    if (should_emit_members(m_type))
    {
        if (m_type != Type::default_uniform_block)
        {
            ss << name();
            if (
                (m_type == Type::uniform_block       ) ||
                (m_type == Type::shader_storage_block)
            )
            {
                ss << "_block";
            }
            ss << " {\n";
        }
        for (const auto& member : m_members)
        {
            const int extra_indent = (m_type == Type::default_uniform_block) ? 0 : 1;
            ss << member->source(indent_level + extra_indent);
        }
        if (m_type != Type::default_uniform_block)
        {
            indent(ss, indent_level);
            ss << "}";
        }
    }
    else if (m_type == Type::struct_member)
    {
        // nada ss << m_struct_type->name();
    }
    else
    {
        ss << glsl_token(basic_type());
    }

    if (
        (m_type != Type::default_uniform_block) &&
        (m_type != Type::struct_type)
    )
    {
        ss << " " << name();
        if (array_size().has_value())
        {
            ss << "[";
            if (array_size() != 0)
            {
                ss << array_size().value();
            }
            ss << "]";
        }
    }

    if (m_type != Type::default_uniform_block)
    {
        // default uniform block only lists members
        ss << ";\n";
    }

    return ss.str();
}

auto Shader_resource::add_struct(
    const std::string_view              name,
    gsl::not_null<Shader_resource*>     struct_type,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    align_offset_to(4); // align by 4 bytes TODO do what spec says
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, struct_type, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_sampler(
    const std::string_view              name,
    const gl::Uniform_type              sampler_type,
    const nonstd::optional<int>         dedicated_texture_unit, /* = {} */
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(m_type == Type::default_uniform_block);
    Expects(!array_size.has_value() || array_size.value() > 0); // no unsized sampler arrays

    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            this,
            dedicated_texture_unit.has_value() ? -1 : m_location,
            sampler_type,
            array_size,
            dedicated_texture_unit
        )
    ).get();
    const int count = array_size.has_value() ? static_cast<int>(array_size.value()) : 1;
    if (!dedicated_texture_unit.has_value())
    {
        m_location += count;
    }
    return new_member;
}

auto Shader_resource::add_float(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::float_,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_vec2(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(2 * 4); // align by 2 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::float_vec2,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_vec3(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::float_vec3,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_vec4(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::float_vec4,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_mat4(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::float_mat4,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_int(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::int_,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_uint(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::unsigned_int,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_uvec2(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(2 * 4); // align by 2 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::unsigned_int_vec2,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_uvec3(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::unsigned_int_vec3,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_uvec4(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::unsigned_int_vec4,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_uint64(
    const std::string_view              name,
    const nonstd::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(8); // align by 8 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            name,
            gl::Uniform_type::unsigned_int64_arb,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

void Shader_resource::align_offset_to(const unsigned int alignment)
{
    while ((m_offset % alignment) != 0)
    {
        ++m_offset;
    }
}

void Shader_resource::indent(
    std::stringstream& ss,
    const int indent_level
) const
{
    for (int i = 0; i < indent_level; ++i)
    {
        ss << "    ";
    }
}


} // namespace erhe::graphics
