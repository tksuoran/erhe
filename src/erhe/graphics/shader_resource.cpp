#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/toolkit/verify.hpp"

#include <algorithm>

namespace erhe::graphics
{

namespace
{

auto glsl_token(gl::Uniform_type type)
-> const char*
{
    switch (type)
    {
        case gl::Uniform_type::int_:              return "int   ";
        case gl::Uniform_type::int_vec2:          return "ivec2 ";
        case gl::Uniform_type::int_vec3:          return "ivec3 ";
        case gl::Uniform_type::int_vec4:          return "ivec4 ";
        case gl::Uniform_type::unsigned_int:      return "uint  ";
        case gl::Uniform_type::unsigned_int_vec2: return "uvec2 ";
        case gl::Uniform_type::unsigned_int_vec3: return "uvec3 ";
        case gl::Uniform_type::unsigned_int_vec4: return "uvec4 ";
        case gl::Uniform_type::float_:            return "float ";
        case gl::Uniform_type::float_vec2:        return "vec2  ";
        case gl::Uniform_type::float_vec3:        return "vec3  ";
        case gl::Uniform_type::float_vec4:        return "vec4  ";
        case gl::Uniform_type::float_mat4:        return "mat4  ";

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
            FATAL("Bad uniform type");
        }
    }
}

static constexpr unsigned int sampler_is_sampler  = 0b000001u;
static constexpr unsigned int sampler_multisample = 0b000010u;
static constexpr unsigned int sampler_cube        = 0b000100u;
static constexpr unsigned int sampler_array       = 0b001000u;
static constexpr unsigned int sampler_shadow      = 0b010000u;
static constexpr unsigned int sampler_rectangle   = 0b100000u;

struct Type_details
{
    Type_details(gl::Uniform_type type,
                 gl::Uniform_type basic_type,
                 size_t           component_count)
        : type           {type}
        , basic_type     {basic_type}
        , component_count{component_count}
    {
    }

    Type_details(gl::Uniform_type   type,
                 gl::Uniform_type   basic_type,
                 gl::Texture_target texture_target,
                 unsigned int       sampler_dimension,
                 unsigned int       sampler_mask)
        : type             {type}
        , basic_type       {basic_type}
        , texture_target   {texture_target}
        , sampler_dimension{sampler_dimension}
        , sampler_mask     {sampler_is_sampler | sampler_mask}
    {
    }

    auto is_sampler    () const -> bool { return sampler_mask != 0; }
    auto is_multisample() const -> bool { return (sampler_mask & sampler_multisample) != 0; }
    auto is_cube       () const -> bool { return (sampler_mask & sampler_cube       ) != 0; }
    auto is_array      () const -> bool { return (sampler_mask & sampler_array      ) != 0; }
    auto is_shadow     () const -> bool { return (sampler_mask & sampler_shadow     ) != 0; }
    auto is_rectangle  () const -> bool { return (sampler_mask & sampler_rectangle  ) != 0; }

    gl::Uniform_type   type             {gl::Uniform_type::bool_};
    gl::Uniform_type   basic_type       {gl::Uniform_type::bool_};
    gl::Texture_target texture_target   {gl::Texture_target::texture_1d};
    size_t             component_count  {0};
    size_t             sampler_dimension{0};
    unsigned int       sampler_mask     {0};
};

auto get_type_details(gl::Uniform_type type)
-> Type_details
{
    switch (type)
    {
        case gl::Uniform_type::int_:              return Type_details(type, gl::Uniform_type::int_,         1);
        case gl::Uniform_type::int_vec2:          return Type_details(type, gl::Uniform_type::int_,         2);
        case gl::Uniform_type::int_vec3:          return Type_details(type, gl::Uniform_type::int_,         3);
        case gl::Uniform_type::int_vec4:          return Type_details(type, gl::Uniform_type::int_,         4);
        case gl::Uniform_type::unsigned_int:      return Type_details(type, gl::Uniform_type::unsigned_int, 1);
        case gl::Uniform_type::unsigned_int_vec2: return Type_details(type, gl::Uniform_type::unsigned_int, 2);
        case gl::Uniform_type::unsigned_int_vec3: return Type_details(type, gl::Uniform_type::unsigned_int, 3);
        case gl::Uniform_type::unsigned_int_vec4: return Type_details(type, gl::Uniform_type::unsigned_int, 4);
        case gl::Uniform_type::float_:            return Type_details(type, gl::Uniform_type::float_,       1);
        case gl::Uniform_type::float_vec2:        return Type_details(type, gl::Uniform_type::float_,       2);
        case gl::Uniform_type::float_vec3:        return Type_details(type, gl::Uniform_type::float_,       3);
        case gl::Uniform_type::float_vec4:        return Type_details(type, gl::Uniform_type::float_,       4);
        case gl::Uniform_type::float_mat4:        return Type_details(type, gl::Uniform_type::float_vec4,   4);

        case gl::Uniform_type::sampler_1d:                                  return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d,        1, 0);
        case gl::Uniform_type::sampler_2d:                                  return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d,        2, 0);
        case gl::Uniform_type::sampler_3d:                                  return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_3d,        3, 0);
        case gl::Uniform_type::sampler_cube:                                return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_cube_map,  2, sampler_cube);
 
        case gl::Uniform_type::sampler_1d_shadow:                           return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d,        1, sampler_shadow);
        case gl::Uniform_type::sampler_2d_shadow:                           return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d,        2, sampler_shadow);
        case gl::Uniform_type::sampler_2d_rect:                             return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_rectangle, 2, sampler_rectangle);
        case gl::Uniform_type::sampler_2d_rect_shadow:                      return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_rectangle, 2, sampler_shadow | sampler_rectangle);
        case gl::Uniform_type::sampler_1d_array:                            return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d_array,  1, sampler_array);
        case gl::Uniform_type::sampler_2d_array:                            return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d_array,  2, sampler_array);
        case gl::Uniform_type::sampler_buffer:                              return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_buffer,    1, 0);
        case gl::Uniform_type::sampler_1d_array_shadow:                     return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_1d_array,  1, sampler_array | sampler_shadow);
        case gl::Uniform_type::sampler_2d_array_shadow:                     return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_2d_array,  2, sampler_array | sampler_shadow);
        case gl::Uniform_type::sampler_cube_shadow:                         return Type_details(type, gl::Uniform_type::float_, gl::Texture_target::texture_cube_map,  2, sampler_cube | sampler_shadow);

        case gl::Uniform_type::int_sampler_1d:                              return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_1d,        1, 0);
        case gl::Uniform_type::int_sampler_2d:                              return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_2d,        2, 0);
        case gl::Uniform_type::int_sampler_3d:                              return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_3d,        3, 0);
        case gl::Uniform_type::int_sampler_cube:                            return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_cube_map,  2, sampler_cube);
        case gl::Uniform_type::int_sampler_2d_rect:                         return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_rectangle, 2, sampler_rectangle);
        case gl::Uniform_type::int_sampler_1d_array:                        return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_1d_array,  1, sampler_array);
        case gl::Uniform_type::int_sampler_2d_array:                        return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_2d_array,  2, sampler_array);
        case gl::Uniform_type::int_sampler_buffer:                          return Type_details(type, gl::Uniform_type::int_, gl::Texture_target::texture_buffer,    1, 0);

        case gl::Uniform_type::unsigned_int_sampler_1d:                     return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_1d,        1, 0);
        case gl::Uniform_type::unsigned_int_sampler_2d:                     return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d,        2, 0);
        case gl::Uniform_type::unsigned_int_sampler_3d:                     return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_3d,        3, 0);
        case gl::Uniform_type::unsigned_int_sampler_cube:                   return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_cube_map,  2, sampler_cube);
        case gl::Uniform_type::unsigned_int_sampler_2d_rect:                return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_rectangle, 2, sampler_rectangle);
        case gl::Uniform_type::unsigned_int_sampler_1d_array:               return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_1d_array,  1, sampler_array);
        case gl::Uniform_type::unsigned_int_sampler_2d_array:               return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d_array,  2, sampler_array);
        case gl::Uniform_type::unsigned_int_sampler_buffer:                 return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_buffer,    1, 0);

        case gl::Uniform_type::sampler_cube_map_array:                      return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_cube_map_array,  2, sampler_cube | sampler_array);
        case gl::Uniform_type::sampler_cube_map_array_shadow:               return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_cube_map_array,  2, sampler_cube | sampler_array | sampler_shadow);

        case gl::Uniform_type::int_sampler_cube_map_array:                  return Type_details(type, gl::Uniform_type::int_,         gl::Texture_target::texture_cube_map_array,  2, sampler_cube | sampler_array);
        case gl::Uniform_type::unsigned_int_sampler_cube_map_array:         return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_cube_map_array,  2, sampler_cube | sampler_array);

        case gl::Uniform_type::sampler_2d_multisample:                      return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_2d_multisample,  2, sampler_multisample);
        case gl::Uniform_type::int_sampler_2d_multisample:                  return Type_details(type, gl::Uniform_type::int_,         gl::Texture_target::texture_2d_multisample,  2, sampler_multisample);
        case gl::Uniform_type::unsigned_int_sampler_2d_multisample:         return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d_multisample,  2, sampler_multisample);

        case gl::Uniform_type::sampler_2d_multisample_array:                return Type_details(type, gl::Uniform_type::float_,       gl::Texture_target::texture_2d_multisample,  2, sampler_multisample | sampler_array);
        case gl::Uniform_type::int_sampler_2d_multisample_array:            return Type_details(type, gl::Uniform_type::int_,         gl::Texture_target::texture_2d_multisample,  2, sampler_multisample | sampler_array);
        case gl::Uniform_type::unsigned_int_sampler_2d_multisample_array:   return Type_details(type, gl::Uniform_type::unsigned_int, gl::Texture_target::texture_2d_multisample,  2, sampler_multisample | sampler_array);

        default:
        {
            FATAL("Bad uniform type");
        }
    }
}

auto get_type_size(gl::Uniform_type type)
-> size_t
{
    switch (type)
    {
        case gl::Uniform_type::int_:         return sizeof(int32_t);
        case gl::Uniform_type::unsigned_int: return sizeof(uint32_t);
        case gl::Uniform_type::float_:       return sizeof(float);
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

auto get_pack_size(gl::Uniform_type type)
-> size_t
{
    const auto type_details = get_type_details(type);
    if (type_details.is_sampler())
    {
        return 0; // samplers are opaque types and as such do not have size
    }

    return type_details.component_count * get_type_size(type_details.basic_type);
}

} // anonymous namespace

auto Shader_resource::is_basic(Type type)
-> bool
{
    switch (type)
    {
        case Type::basic                : return true;
        case Type::sampler              : return true;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return false;
        case Type::uniform_block        : return false;
        case Type::shader_storage_block : return false;
        default:
        {
            FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::is_aggregate(Shader_resource::Type type)
-> bool
{
    switch (type)
    {
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::should_emit_members(Shader_resource::Type type)
-> bool
{
    switch (type)
    {
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::is_block(Shader_resource::Type type)
-> bool
{
    switch (type)
    {
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::uses_binding_points(Shader_resource::Type type)
-> bool
{
    switch (type)
    {
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::default_uniform_block: return false;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default:
        {
            FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::c_str(Shader_resource::Precision v)
-> const char*
{
    switch (v)
    {
        case Precision::lowp:    return "lowp";
        case Precision::mediump: return "mediump";
        case Precision::highp:   return "highp";
        case Precision::superp:  return "superp";
        default:
        {
            FATAL("Bad uniform precision\n");
        }
    }
};

// Struct type
Shader_resource::Shader_resource(std::string_view struct_type_name,
                                 Shader_resource* parent /* = nullptr */)
    : m_type            {Type::struct_type}
    , m_name            {struct_type_name}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->next_member_offset() : 0}
{
}

// Struct member
Shader_resource::Shader_resource(std::string_view                struct_member_name,
                                 gsl::not_null<Shader_resource*> struct_type,
                                 std::optional<size_t>           array_size /* = {} */,
                                 Shader_resource*                parent /* = nullptr */)
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
Shader_resource::Shader_resource(std::string_view      block_name,
                                 int                   binding_point,
                                 Type                  block_type,
                                 std::optional<size_t> array_size /* = {} */)
    : m_type         {block_type}
    , m_name         {block_name}
    , m_array_size   {array_size}
    , m_binding_point{binding_point}
{
    Expects((int)binding_point < Configuration::limits.max_uniform_buffer_bindings);

    VERIFY((block_type == Type::uniform_block) || (block_type == Type::shader_storage_block));
}

// Basic type
Shader_resource::Shader_resource(std::string_view      basic_name,
                                 gl::Uniform_type      basic_type,
                                 std::optional<size_t> array_size /* = {} */,
                                 Shader_resource*      parent /* = nullptr */)
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
Shader_resource::Shader_resource(std::string_view                sampler_name,
                                 gsl::not_null<Shader_resource*> parent,
                                 int                             location,
                                 gl::Uniform_type                sampler_type,
                                 std::optional<size_t>           array_size /* = {} */,
                                 std::optional<int>              dedicated_texture_unit /* = {} */)
    : m_type                        {Type::sampler}
    , m_name                        {sampler_name}
    , m_array_size                  {array_size}
    , m_parent                      {parent}
    , m_basic_type                  {sampler_type}
    , m_location                    {location}
    , m_dedicated_texture_unit_index{dedicated_texture_unit}
{
}

// Constructor with no arguments creates default uniform block
Shader_resource::Shader_resource()
    : m_location{0} // next location
{
}

Shader_resource::~Shader_resource() = default;

auto Shader_resource::is_array() const
-> bool
{
    return m_array_size.has_value();
}

auto Shader_resource::dedicated_texture_unit_index() const
-> std::optional<int>
{
    Expects(m_type == Type::sampler);

    return m_dedicated_texture_unit_index;
}

auto Shader_resource::type() const
-> Shader_resource::Type
{
    return m_type;
}

auto Shader_resource::name() const
-> const std::string&
{
    return m_name;
}

auto Shader_resource::array_size() const
-> std::optional<size_t>
{
    return m_array_size;
}

auto Shader_resource::basic_type() const
-> gl::Uniform_type
{
    Expects(is_basic(m_type));

    return m_basic_type;
}

// Only? for uniforms in default uniform block
// For default uniform block, this is the next available location.
auto Shader_resource::location() const
-> int
{
    Expects(m_parent != nullptr);
    Expects(m_parent->type() == Type::default_uniform_block);

    return m_location;
}

auto Shader_resource::index_in_parent() const
-> size_t
{
    Expects(m_parent != nullptr);
    Expects(is_aggregate(m_parent->type()));

    return m_index_in_parent; // TODO
}

auto Shader_resource::offset_in_parent() const
-> size_t
{
    Expects(m_parent != nullptr);
    Expects(is_aggregate(m_parent->type()));

    return m_offset_in_parent; // TODO
}

auto Shader_resource::parent() const
-> Shader_resource*
{
    return m_parent;
}

auto Shader_resource::member_count() const
-> size_t
{
    Expects(is_aggregate(m_type));

    return m_members.size();
}

auto Shader_resource::member(std::string_view name) const
-> Shader_resource*
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

auto Shader_resource::binding_point() const
-> unsigned int
{
    Expects(uses_binding_points(m_type));
    Expects(m_binding_point >= 0);
    return static_cast<unsigned int>(m_binding_point);
}

// Returns size of block.
// For arrays, size of one element is returned.
auto Shader_resource::size_bytes() const
-> size_t
{
    if (m_type == Type::struct_type)
    {
        // Assume all members have been added
        return m_offset;
    }

    if (m_type == Type::struct_member)
    {
        return m_struct_type->size_bytes();
    }

    // TODO Shader storage buffer alignment?
    if (is_block(m_type))
    {
        size_t padded_size = m_offset;
        while ((padded_size % Configuration::implementation_defined.uniform_buffer_offset_alignment) != 0)
        {
            ++padded_size;
        }

        return padded_size;
    }
    else
    {
        size_t element_size = get_pack_size(m_basic_type);
        size_t array_multiplier{1};
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

auto Shader_resource::offset() const
-> size_t
{
    Expects(is_aggregate(m_type));
    return m_offset;
}

auto Shader_resource::next_member_offset() const
-> size_t
{
    Expects(is_aggregate(m_type));
    return m_offset;
}

auto Shader_resource::type_string() const
-> std::string
{
    if ((m_parent != nullptr) && (m_parent->type() == Type::default_uniform_block))
    {
        Expects(m_type != Type::default_uniform_block);
        return "uniform ";
    }

    switch (m_type)
    {
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
            FATAL("Samplers are only allowed in default uniform block");
        }

        default:
        {
            FATAL("Bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::layout_string() const
-> std::string
{
    if ((m_location      == -1) &&
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

auto Shader_resource::source(int indent_level /* = 0 */) const
-> std::string
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
            if ((m_type == Type::uniform_block       ) ||
                (m_type == Type::shader_storage_block))
            {
                ss << "_block";
            }
            ss << " {\n";
        }
        for (const auto& member : m_members)
        {
            int extra_indent = (m_type == Type::default_uniform_block) ? 0 : 1;
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

    if ((m_type != Type::default_uniform_block) &&
        (m_type != Type::struct_type))
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

auto Shader_resource::add_struct(std::string_view                name,
                                 gsl::not_null<Shader_resource*> struct_type,
                                 std::optional<size_t>           array_size /* = {} */)
-> Shader_resource*
{
    align_offset_to(4); // align by 4 bytes TODO do what spec says
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, struct_type, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_sampler(std::string_view      name,
                                  gl::Uniform_type      sampler_type,
                                  std::optional<size_t> array_size /* = {} */,
                                  std::optional<int>    dedicated_texture_unit /* = {} */)
-> Shader_resource*
{
    Expects(m_type == Type::default_uniform_block);
    Expects(!array_size.has_value() || array_size.value() > 0); // no unsized sampler arrays

    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, this, m_location, sampler_type, array_size, dedicated_texture_unit)).get();
    const int count = array_size.has_value() ? static_cast<int>(array_size.value()) : 1;
    m_location += count;
    return new_member;
}

auto Shader_resource::add_float(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::float_, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_vec2(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(2 * 4); // align by 2 * 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::float_vec2, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_vec3(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::float_vec3, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_vec4(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::float_vec4, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_mat4(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::float_mat4, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_int(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::int_, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

auto Shader_resource::add_uint(std::string_view name, std::optional<size_t> array_size /* = {} */)
-> Shader_resource*
{
    Expects(is_aggregate(m_type));
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(std::make_unique<Shader_resource>(name, gl::Uniform_type::unsigned_int, array_size, this)).get();
    m_offset += new_member->size_bytes();
    return new_member;
}

void Shader_resource::align_offset_to(unsigned int alignment)
{
    while ((m_offset % alignment) != 0)
    {
        ++m_offset;
    }
}

void Shader_resource::indent(std::stringstream& ss, int indent_level) const
{
    for (int i = 0; i < indent_level; ++i)
    {
        ss << "    ";
    }
}


} // namespace erhe::graphics
