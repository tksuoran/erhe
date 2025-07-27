#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_verify/verify.hpp"

#include "fmt/format.h"

#include <algorithm>

namespace erhe::graphics {

namespace {

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
        const Glsl_type   type,
        const Glsl_type   basic_type,
        const std::size_t component_count
    )
        : type           {type}
        , basic_type     {basic_type}
        , component_count{component_count}
    {
    }

    Type_details(
        const Glsl_type    type,
        const Glsl_type    basic_type,
        const Texture_type texture_target,
        const unsigned int sampler_dimension,
        const unsigned int sampler_mask
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

    Glsl_type    type             {Glsl_type::bool_};
    Glsl_type    basic_type       {Glsl_type::bool_};
    Texture_type texture_target   {Texture_type::texture_1d};
    std::size_t  component_count  {0};
    std::size_t  sampler_dimension{0};
    unsigned int sampler_mask     {0};
};

auto get_type_details(const Glsl_type type) -> Type_details
{
    switch (type) {
        case Glsl_type::int_:                                        return Type_details(type, Glsl_type::int_,               1);
        case Glsl_type::int_vec2:                                    return Type_details(type, Glsl_type::int_,               2);
        case Glsl_type::int_vec3:                                    return Type_details(type, Glsl_type::int_,               3);
        case Glsl_type::int_vec4:                                    return Type_details(type, Glsl_type::int_,               4);
        case Glsl_type::unsigned_int:                                return Type_details(type, Glsl_type::unsigned_int,       1);
        case Glsl_type::unsigned_int_vec2:                           return Type_details(type, Glsl_type::unsigned_int,       2);
        case Glsl_type::unsigned_int_vec3:                           return Type_details(type, Glsl_type::unsigned_int,       3);
        case Glsl_type::unsigned_int_vec4:                           return Type_details(type, Glsl_type::unsigned_int,       4);
        case Glsl_type::float_:                                      return Type_details(type, Glsl_type::float_,             1);
        case Glsl_type::float_vec2:                                  return Type_details(type, Glsl_type::float_,             2);
        case Glsl_type::float_vec3:                                  return Type_details(type, Glsl_type::float_,             3);
        case Glsl_type::float_vec4:                                  return Type_details(type, Glsl_type::float_,             4);
        case Glsl_type::float_mat_2x2:                               return Type_details(type, Glsl_type::float_vec2,         2);
        case Glsl_type::float_mat_3x3:                               return Type_details(type, Glsl_type::float_vec3,         3);
        case Glsl_type::float_mat_4x4:                               return Type_details(type, Glsl_type::float_vec4,         4);

        case Glsl_type::sampler_1d:                                  return Type_details(type, Glsl_type::float_, Texture_type::texture_1d,       1, 0);
        case Glsl_type::sampler_2d:                                  return Type_details(type, Glsl_type::float_, Texture_type::texture_2d,       2, 0);
        case Glsl_type::sampler_3d:                                  return Type_details(type, Glsl_type::float_, Texture_type::texture_3d,       3, 0);
        case Glsl_type::sampler_cube:                                return Type_details(type, Glsl_type::float_, Texture_type::texture_cube_map, 2, sampler_flags_cube);

        case Glsl_type::sampler_1d_shadow:                           return Type_details(type, Glsl_type::float_, Texture_type::texture_1d,        1, sampler_flags_shadow);
        case Glsl_type::sampler_2d_shadow:                           return Type_details(type, Glsl_type::float_, Texture_type::texture_2d,        2, sampler_flags_shadow);
        case Glsl_type::sampler_1d_array:                            return Type_details(type, Glsl_type::float_, Texture_type::texture_1d,        1, sampler_flags_array);
        case Glsl_type::sampler_2d_array:                            return Type_details(type, Glsl_type::float_, Texture_type::texture_2d,        2, sampler_flags_array);
        case Glsl_type::sampler_buffer:                              return Type_details(type, Glsl_type::float_, Texture_type::texture_buffer,    1, 0);
        case Glsl_type::sampler_1d_array_shadow:                     return Type_details(type, Glsl_type::float_, Texture_type::texture_1d,        1, sampler_flags_array | sampler_flags_shadow);
        case Glsl_type::sampler_2d_array_shadow:                     return Type_details(type, Glsl_type::float_, Texture_type::texture_2d,        2, sampler_flags_array | sampler_flags_shadow);
        case Glsl_type::sampler_cube_shadow:                         return Type_details(type, Glsl_type::float_, Texture_type::texture_cube_map,  2, sampler_flags_cube | sampler_flags_shadow);

        case Glsl_type::int_sampler_1d:                              return Type_details(type, Glsl_type::int_,   Texture_type::texture_1d,        1, 0);
        case Glsl_type::int_sampler_2d:                              return Type_details(type, Glsl_type::int_,   Texture_type::texture_2d,        2, 0);
        case Glsl_type::int_sampler_3d:                              return Type_details(type, Glsl_type::int_,   Texture_type::texture_3d,        3, 0);
        case Glsl_type::int_sampler_cube:                            return Type_details(type, Glsl_type::int_,   Texture_type::texture_cube_map,  2, sampler_flags_cube);
        case Glsl_type::int_sampler_1d_array:                        return Type_details(type, Glsl_type::int_,   Texture_type::texture_1d,        1, sampler_flags_array);
        case Glsl_type::int_sampler_2d_array:                        return Type_details(type, Glsl_type::int_,   Texture_type::texture_2d,        2, sampler_flags_array);
        case Glsl_type::int_sampler_buffer:                          return Type_details(type, Glsl_type::int_,   Texture_type::texture_buffer,    1, 0);

        case Glsl_type::unsigned_int_sampler_1d:                     return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_1d,       1, 0);
        case Glsl_type::unsigned_int_sampler_2d:                     return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_2d,       2, 0);
        case Glsl_type::unsigned_int_sampler_3d:                     return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_3d,       3, 0);
        case Glsl_type::unsigned_int_sampler_cube:                   return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_cube_map, 2, sampler_flags_cube);
        case Glsl_type::unsigned_int_sampler_1d_array:               return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_1d,       1, sampler_flags_array);
        case Glsl_type::unsigned_int_sampler_2d_array:               return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_2d,       2, sampler_flags_array);
        case Glsl_type::unsigned_int_sampler_buffer:                 return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_buffer,   1, 0);

        case Glsl_type::sampler_cube_map_array:                      return Type_details(type, Glsl_type::float_,       Texture_type::texture_cube_map, 2, sampler_flags_cube | sampler_flags_array);
        case Glsl_type::sampler_cube_map_array_shadow:               return Type_details(type, Glsl_type::float_,       Texture_type::texture_cube_map, 2, sampler_flags_cube | sampler_flags_array | sampler_flags_shadow);

        case Glsl_type::int_sampler_cube_map_array:                  return Type_details(type, Glsl_type::int_,         Texture_type::texture_cube_map, 2, sampler_flags_cube | sampler_flags_array);
        case Glsl_type::unsigned_int_sampler_cube_map_array:         return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_cube_map, 2, sampler_flags_cube | sampler_flags_array);

        case Glsl_type::sampler_2d_multisample:                      return Type_details(type, Glsl_type::float_,       Texture_type::texture_2d, 2, sampler_flags_multisample);
        case Glsl_type::int_sampler_2d_multisample:                  return Type_details(type, Glsl_type::int_,         Texture_type::texture_2d, 2, sampler_flags_multisample);
        case Glsl_type::unsigned_int_sampler_2d_multisample:         return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_2d, 2, sampler_flags_multisample);

        case Glsl_type::sampler_2d_multisample_array:                return Type_details(type, Glsl_type::float_,       Texture_type::texture_2d, 2, sampler_flags_multisample | sampler_flags_array);
        case Glsl_type::int_sampler_2d_multisample_array:            return Type_details(type, Glsl_type::int_,         Texture_type::texture_2d, 2, sampler_flags_multisample | sampler_flags_array);
        case Glsl_type::unsigned_int_sampler_2d_multisample_array:   return Type_details(type, Glsl_type::unsigned_int, Texture_type::texture_2d, 2, sampler_flags_multisample | sampler_flags_array);

        default: {
            ERHE_FATAL("Bad GLSL type %u", static_cast<unsigned int>(type));
        }
    }
}

auto get_type_size(const Glsl_type type) -> std::size_t
{
    switch (type) {
        //using enum igl::UniformType;
        case Glsl_type::int_:               return 4;
        case Glsl_type::unsigned_int:       return 4;
        //case Glsl_type::unsigned_int64_arb: return 8;
        case Glsl_type::float_:             return 4;
        //case Glsl_type::double_:            return 8;
        default: {
            auto type_details = get_type_details(type);
            if (type_details.is_sampler()) {
                return 0; // samplers are opaque types and as such do not have size
            }
            return type_details.component_count * get_type_size(type_details.basic_type);
        }
    }
}

auto get_pack_size(const Glsl_type type) -> std::size_t
{
    const auto type_details = get_type_details(type);
    if (type_details.is_sampler()) {
        return 0; // samplers are opaque types and as such do not have size
    }

    return type_details.component_count * get_type_size(type_details.basic_type);
}

} // anonymous namespace

auto Shader_resource::is_basic(const Type type) -> bool
{
    switch (type) {
        //using enum Type;
        case Type::basic                : return true;
        case Type::sampler              : return true;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::samplers             : return false;
        case Type::uniform_block        : return false;
        case Type::shader_storage_block : return false;
        default: {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::is_aggregate(const Shader_resource::Type type) -> bool
{
    switch (type) {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::samplers             : return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default: {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::should_emit_layout(const Type type) -> bool
{
    switch (type) {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return true;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::samplers             : return false;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default: {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::should_emit_members(const Shader_resource::Type type) -> bool
{
    switch (type) {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return true;
        case Type::struct_member        : return false;
        case Type::samplers             : return true;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default: {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::is_block(const Shader_resource::Type type) -> bool
{
    switch (type) {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::samplers             : return false; //// This used to be true for OpenGL
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default: {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::uses_binding_points(const Shader_resource::Type type) -> bool
{
    switch (type) {
        //using enum Type;
        case Type::basic                : return false;
        case Type::sampler              : return false;
        case Type::struct_type          : return false;
        case Type::struct_member        : return false;
        case Type::samplers             : return false;
        case Type::uniform_block        : return true;
        case Type::shader_storage_block : return true;
        default: {
            ERHE_FATAL("bad Shader_resource::Type");
        }
    }
}

auto Shader_resource::c_str(const Shader_resource::Precision precision) -> const char*
{
    switch (precision) {
        //using enum Shader_resource::Precision;
        case Shader_resource::Precision::lowp:    return "lowp";
        case Shader_resource::Precision::mediump: return "mediump";
        case Shader_resource::Precision::highp:   return "highp";
        case Shader_resource::Precision::superp:  return "superp";
        default: {
            ERHE_FATAL("Bad uniform precision");
        }
    }
};

// Struct type
Shader_resource::Shader_resource(Device& device, const std::string_view struct_type_name, Shader_resource* parent)
    : m_device          {device}
    , m_type            {Type::struct_type}
    , m_name            {struct_type_name}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->get_member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->get_next_member_offset() : 0}
{
}

// Struct member
Shader_resource::Shader_resource(
    Device&                          device,
    const std::string_view           struct_member_name,
    Shader_resource*                 struct_type,
    const std::optional<std::size_t> array_size /* = {} */,
    Shader_resource*                 parent /* = nullptr */
)
    : m_device          {device}
    , m_type            {Type::struct_member}
    , m_name            {struct_member_name}
    , m_array_size      {array_size}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->get_member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->get_next_member_offset() : 0}
    , m_struct_type     {struct_type}
{
}

// Block (uniform block or shader storage block)
Shader_resource::Shader_resource(
    Device&                          device,
    const std::string_view           name,
    const int                        binding_point,
    const Type                       type,
    const std::optional<std::size_t> array_size /* = {} */
)
    : m_device       {device}
    , m_type         {type}
    , m_name         {name}
    , m_array_size   {array_size}
    , m_binding_point{binding_point}
{
    //// Would be nice to be able to verify this at the time of construction,
    //// but Instance might not be ready yet.
    ////
    //// if (type == Type::uniform_block) {
    ////     ERHE_VERIFY(binding_point < g_instance->limits.max_uniform_buffer_bindings);
    //// }
    //// if (type == Type::shader_storage_block) {
    ////     ERHE_VERIFY(binding_point < g_instance->limits.max_shader_storage_buffer_bindings);
    //// }
    //// if (type == Type::sampler) {
    ////     // TODO Which limit to use?
    ////     ERHE_VERIFY(binding_point < g_instance->limits.max_combined_texture_image_units);
    //// }
}

// Basic type
Shader_resource::Shader_resource(
    Device&                          device,
    const std::string_view           basic_name,
    const Glsl_type                  basic_type,
    const std::optional<std::size_t> array_size /* = {} */,
    Shader_resource*                 parent /* = nullptr */
)
    : m_device          {device}
    , m_type            {Type::basic}
    , m_name            {basic_name}
    , m_array_size      {array_size}
    , m_parent          {parent}
    , m_index_in_parent {(parent != nullptr) ? parent->get_member_count()       : 0}
    , m_offset_in_parent{(parent != nullptr) ? parent->get_next_member_offset() : 0}
    , m_basic_type      {basic_type}
{
}

// Sampler
Shader_resource::Shader_resource(
    Device&                          device,
    const std::string_view           sampler_name,
    Shader_resource*                 parent,
    const int                        location,
    const Glsl_type                  sampler_type,
    const std::optional<std::size_t> array_size /* = {} */,
    const std::optional<int>         dedicated_texture_unit /* = {} */
)
    : m_device       {device}
    , m_type         {Type::sampler}
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
Shader_resource::Shader_resource(Device& device)
    : m_device  {device}
    , m_location{0} // next location
{
}

Shader_resource::~Shader_resource() noexcept
{
}

Shader_resource::Shader_resource(Shader_resource&& other) = default;

auto Shader_resource::is_array() const -> bool
{
    return m_array_size.has_value();
}

auto Shader_resource::get_type() const -> Shader_resource::Type
{
    return m_type;
}

auto Shader_resource::get_name() const -> const std::string&
{
    return m_name;
}

auto Shader_resource::get_array_size() const -> std::optional<std::size_t>
{
    return m_array_size;
}

auto Shader_resource::get_basic_type() const -> Glsl_type
{
    ERHE_VERIFY(is_basic(m_type));

    return m_basic_type;
}

// Only? for uniforms in default uniform block
// For default uniform block, this is the next available location.
auto Shader_resource::get_location() const -> int
{
    ERHE_VERIFY(m_parent != nullptr);
    ERHE_VERIFY(m_parent->get_type() == Type::samplers);

    return m_location;
}

auto Shader_resource::get_index_in_parent() const -> std::size_t
{
    ERHE_VERIFY(m_parent != nullptr);
    ERHE_VERIFY(is_aggregate(m_parent->get_type()));

    return m_index_in_parent; // TODO
}

auto Shader_resource::get_offset_in_parent() const -> std::size_t
{
    ERHE_VERIFY(m_parent != nullptr);
    ERHE_VERIFY(is_aggregate(m_parent->get_type()));

    return m_offset_in_parent; // TODO
}

auto Shader_resource::get_parent() const -> Shader_resource*
{
    return m_parent;
}

auto Shader_resource::get_member_count() const -> std::size_t
{
    ERHE_VERIFY(is_aggregate(m_type));

    return m_members.size();
}

auto Shader_resource::get_member(const std::string_view name) const -> Shader_resource*
{
    for (const auto& member : m_members) {
        if (member->get_name() == name) {
            return member.get();
        }
    }

    return {};
}

auto Shader_resource::get_binding_point() const -> unsigned int
{
    ERHE_VERIFY(uses_binding_points(m_type));
    ERHE_VERIFY(m_binding_point >= 0);
    return static_cast<unsigned int>(m_binding_point);
}

auto Shader_resource::get_texture_unit() const -> int
{
    ERHE_VERIFY(m_type == Type::sampler);
    ERHE_VERIFY(m_binding_point >= 0);
    return m_binding_point;
}

auto Shader_resource::get_binding_target() const-> Buffer_target
{
    switch (m_type) {
        case Type::uniform_block:        return Buffer_target::uniform;
        case Type::shader_storage_block: return Buffer_target::storage;
        default:
        {
            ERHE_FATAL("not binding target %d", static_cast<unsigned int>(m_type));
            return Buffer_target::index; // TODO
        }
    }
}

auto Shader_resource::get_size_bytes() const -> std::size_t
{
    if (m_type == Type::struct_type) {
        // Assume all members have been added
        return m_offset;
    }

    if (m_type == Type::struct_member) {
        std::size_t struct_size = m_struct_type->get_size_bytes();
        std::size_t array_multiplier{1};
        if (m_array_size.has_value() && (m_array_size.value() > 0)) {
            array_multiplier = m_array_size.value();

            // arrays in uniform block are packed with std140
            if ((m_parent != nullptr) && (m_parent->get_type() == Type::uniform_block)) {
                while ((struct_size % 16) != 0) {
                    ++struct_size;
                }
            }
        }

        return struct_size * array_multiplier;
    }

    // TODO Shader storage buffer alignment?
    if (is_block(m_type)) {
        std::size_t padded_size = m_offset;
        while ((padded_size % m_device.get_info().uniform_buffer_offset_alignment) != 0) {
            ++padded_size;
        }

        return padded_size;
    } else {
        std::size_t element_size = get_pack_size(m_basic_type);
        std::size_t array_multiplier{1};
        if (m_array_size.has_value() && (m_array_size.value() > 0)) {
            array_multiplier = m_array_size.value();

            // arrays in uniform block are packed with std140
            if ((m_parent != nullptr) && (m_parent->get_type() == Type::uniform_block)) {
                while ((element_size % 16) != 0) {
                    ++element_size;
                }
            }
        }

        return element_size * array_multiplier;
    }
}

auto Shader_resource::get_offset() const -> std::size_t
{
    ERHE_VERIFY(is_aggregate(m_type));
    return m_offset;
}

auto Shader_resource::get_next_member_offset() const -> std::size_t
{
    ERHE_VERIFY(is_aggregate(m_type));
    return m_offset;
}

auto Shader_resource::get_type_string() const -> std::string
{
    if ((m_parent != nullptr) && (m_parent->get_type() == Type::samplers)) {
        // TODO Old branch - delete?
        ERHE_VERIFY(m_type != Type::samplers);
        return "uniform ";
    }

    switch (m_type) {
        //using enum Type;
        case Type::basic: {
            return " ";
        }

        case Type::struct_type: {
            return "struct ";
        }

        case Type::struct_member: {
            return m_struct_type->get_name() + " ";
        }

        case Type::samplers: {
            ERHE_VERIFY(m_parent == nullptr);
            return "";
        }

        case Type::uniform_block: {
            ERHE_VERIFY(m_parent == nullptr);
            return "uniform ";
        }

        case Type::shader_storage_block: {
            ERHE_VERIFY(m_parent == nullptr);
            return "buffer ";
        }

        case Type::sampler: {
            ERHE_FATAL("Samplers are only allowed in default uniform block");
            break;
        }

        default: {
            ERHE_FATAL("Bad Shader_resource::Type");
            break;
        }
    }
}

auto Shader_resource::get_layout_string() const -> std::string
{
    if ((m_location == -1) && (m_binding_point == -1)) {
        return {};
    }

    if ((m_type == Type::sampler) && (m_device.get_info().glsl_version < 430)) {
        return {};
    }

    std::stringstream ss;
    bool first{true};

    ss << "layout(";
    if (m_type == Type::uniform_block) {
        ss << "std140";
        first = false;
    } else if (m_type == Type::shader_storage_block) {
        if (m_device.get_info().glsl_version < 430) {
            ss << "std140";
        } else {
            ss << "std430";
        }
        first = false;
    }
    if (m_location != -1) {
        if (!first) {
            ss << ", ";
        }
        ss << "location = " << m_location;
        first = false;
    }
    if ((m_parent != nullptr) && (m_parent->get_type() == Type::struct_type)) {
        if (!first) {
            ss << ", ";
        }
        ss << "offset = " << m_offset_in_parent;
        first = false;
    }
    if (m_device.get_info().glsl_version >= 420) {
        if (m_binding_point != -1) {
            if (!first)
            {
                ss << ", ";
            }
            ss << "binding = " << m_binding_point;
            //first = false;
        }
    }
    ss << ") ";
    if (m_device.get_info().glsl_version >= 420) {
        if (m_readonly) {
            ss << "readonly ";
        }
        if (m_writeonly) {
            ss << "writeonly ";
        }
    }
    return ss.str();
}

auto Shader_resource::get_source(const int indent_level /* = 0 */) const -> std::string
{
    std::stringstream ss;

    indent(ss, indent_level);

    if (should_emit_layout(m_type)) {
        ss << get_layout_string();
    }

    ss << get_type_string();

    if (should_emit_members(m_type)) {
        if (m_type != Type::samplers) {
            ss << get_name();
            if ((m_type == Type::uniform_block) || (m_type == Type::shader_storage_block)) {
                ss << "_block";
            }
            ss << " {\n";
        }
        for (const auto& member : m_members) {
            const int extra_indent = (m_type == Type::samplers) ? 0 : 1;
            ss << member->get_source(indent_level + extra_indent);
        }
        if (m_type != Type::samplers) {
            indent(ss, indent_level);
            ss << "}";
        }
    } else if (m_type == Type::struct_member) {
        // nada ss << m_struct_type->name();
    } else {
        ss << glsl_type_c_str(get_basic_type());
    }

    if ((m_type != Type::samplers) && (m_type != Type::struct_type)) {
        ss << " " << get_name();
        if (get_array_size().has_value()) {
            ss << "[";
            if (get_array_size() != 0) {
                ss << get_array_size().value();
            }
            ss << "]";
        }
    }

    if (m_type != Type::samplers) {
        // default uniform block only lists members
        ss << ";\n";
    }

    return ss.str();
}

void Shader_resource::sanitize(const std::optional<std::size_t>& array_size) const
{
    ERHE_VERIFY((m_type != Type::uniform_block) || !array_size.has_value() || array_size.value() > 0); // no unsized arrays in uniform blocks
}

auto Shader_resource::add_struct(
    const std::string_view           name,
    Shader_resource*                 struct_type,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(struct_type != nullptr);
    sanitize(array_size);
    align_offset_to(4); // align by 4 bytes TODO do what spec says
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(m_device, name, struct_type, array_size, this)
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_sampler(
    const std::string_view           name,
    const Glsl_type                  sampler_type,
    const std::optional<int>         dedicated_texture_unit, /* = {} */
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(m_type == Type::samplers);
    sanitize(array_size);

    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            this,
            dedicated_texture_unit.has_value() ? -1 : m_location,
            sampler_type,
            array_size,
            dedicated_texture_unit
        )
    ).get();
    const int count = array_size.has_value() ? static_cast<int>(array_size.value()) : 1;
    if (!dedicated_texture_unit.has_value()) {
        m_location += count;
    }
    return new_member;
}

auto Shader_resource::add_float(const std::string_view name, const std::optional<std::size_t> array_size) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(m_device, name, Glsl_type::float_, array_size, this)
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_vec2(const std::string_view name, const std::optional<std::size_t> array_size) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(2 * 4); // align by 2 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(m_device, name, Glsl_type::float_vec2, array_size, this)
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_vec3(const std::string_view name, const std::optional<std::size_t> array_size) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(m_device, name, Glsl_type::float_vec3, array_size, this)
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_vec4(const std::string_view name, const std::optional<std::size_t> array_size) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(m_device, name, Glsl_type::float_vec4, array_size, this)
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_mat4(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::float_mat_4x4,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_int(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::int_,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_uint(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4); // align by 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::unsigned_int,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_uvec2(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(2 * 4); // align by 2 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::unsigned_int_vec2,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_uvec3(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::unsigned_int_vec3,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_uvec4(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(4 * 4); // align by 4 * 4 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::unsigned_int_vec4,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

auto Shader_resource::add_uint64(
    const std::string_view           name,
    const std::optional<std::size_t> array_size /* = {} */
) -> Shader_resource*
{
    ERHE_VERIFY(is_aggregate(m_type));
    sanitize(array_size);
    align_offset_to(8); // align by 8 bytes
    auto* const new_member = m_members.emplace_back(
        std::make_unique<Shader_resource>(
            m_device,
            name,
            Glsl_type::unsigned_int64,
            array_size,
            this
        )
    ).get();
    m_offset += new_member->get_size_bytes();
    return new_member;
}

void Shader_resource::set_readonly(bool value)
{
    m_readonly = value;
}

void Shader_resource::set_writeonly(bool value)
{
    m_writeonly = value;
}

auto Shader_resource::get_readonly() const -> bool
{
    return m_readonly;
}

auto Shader_resource::get_writeonly() const -> bool
{
    return m_writeonly;
}

auto Shader_resource::add_attribute(const erhe::dataformat::Vertex_attribute& attribute) -> Shader_resource*
{
    const std::string name = fmt::format(
        "{}_{}",
        erhe::dataformat::c_str(attribute.usage_type),
        attribute.usage_index
    );

    switch (attribute.format) {
        case erhe::dataformat::Format::format_32_scalar_sint:  return add_int(name);
        //case erhe::dataformat::Format::format_32_vec2_sint:  return add_ivec2(name);
        //case erhe::dataformat::Format::format_32_vec3_sint:  return add_ivec3(name);
        //case erhe::dataformat::Format::format_32_vec4_sint:  return add_ivec4(name);
        case erhe::dataformat::Format::format_32_scalar_uint:  return add_uint(name);
        case erhe::dataformat::Format::format_32_vec2_uint:    return add_uvec2(name);
        case erhe::dataformat::Format::format_32_vec3_uint:    return add_uvec3(name);
        case erhe::dataformat::Format::format_32_vec4_uint:    return add_uvec4(name);
        case erhe::dataformat::Format::format_32_scalar_float: return add_float(name);
        case erhe::dataformat::Format::format_32_vec2_float:   return add_vec2(name);
        case erhe::dataformat::Format::format_32_vec3_float:   return add_vec3(name);
        case erhe::dataformat::Format::format_32_vec4_float:   return add_vec4(name);
        default: {
            ERHE_FATAL("Attribute type not supported");
        }
    }

    return nullptr;
}

void Shader_resource::align_offset_to(const unsigned int alignment)
{
    while ((m_offset % alignment) != 0) {
        ++m_offset;
    }
}

void Shader_resource::indent(std::stringstream& ss, const int indent_level) const
{
    for (int i = 0; i < indent_level; ++i) {
        ss << "    ";
    }
}

void add_vertex_stream(const erhe::dataformat::Vertex_stream& vertex_stream, Shader_resource& vertex_struct, Shader_resource& vertices_block)
{
    for (const auto& attribute : vertex_stream.attributes) {
        vertex_struct.add_attribute(attribute);
    }
    vertices_block.add_struct("vertices", &vertex_struct, Shader_resource::unsized_array);
}

} // namespace erhe::graphics
