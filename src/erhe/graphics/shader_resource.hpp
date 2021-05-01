#ifndef shader_resource_hpp_erhe_graphics
#define shader_resource_hpp_erhe_graphics

#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/configuration.hpp"

#include <gsl/pointers>

#include <sstream>
#include <optional>

namespace erhe::graphics
{

// Shader resource represents data or data structure that can
// be made available to shader program.
//
// Uniform and shader storage blocks are shader resources.
// Samplers are shader resources.
//
// Shader resources are typically created once, so that they
// can be shared with multiple shader programs.
//
// Shader resources link C++ code with GLSL shader programs:
//  - C++ code is used to create shader resources
//  - C++ code emits source strings for shader resources
//  - Emitted source strings are injected to shader sources
//
// When shader resources are created with C++ code, there is
// no need for shader reflection.
class Shader_resource
{
public:
    enum class Type : unsigned int
    {
        basic                 = 0, // non sampler basic types
        sampler               = 1,
        struct_type           = 2, // not really a resource, just type declaration
        struct_member         = 3,
        default_uniform_block = 4,
        uniform_block         = 5,
        shader_storage_block  = 6
    };

    static auto is_basic(Type type)
    -> bool;

    static auto is_aggregate(Type type)
    -> bool;

    static auto should_emit_members(Type type)
    -> bool;

    static auto is_block(Type type)
    -> bool;

    static auto uses_binding_points(Type type)
    -> bool;

    enum class Precision : unsigned int
    {
        lowp    = 0,
        mediump = 1,
        highp   = 2,
        superp  = 3 // only reserved - not used
    };

    using Member_collection = std::vector<Shader_resource>;

    static auto c_str(Precision v)
    -> const char*;

    // Struct definition
    Shader_resource(const std::string& struct_type_name,
                    Shader_resource*   parent = nullptr);

    // Struct member
    Shader_resource(const std::string&              struct_member_name,
                    gsl::not_null<Shader_resource*> struct_type,
                    std::optional<size_t>           array_size = {},
                    Shader_resource*                parent = nullptr);

    // Block (uniform block or shader storage block)
    Shader_resource(const std::string&    block_name,
                    int                   binding_point,
                    Type                  block_type,
                    std::optional<size_t> array_size = {});

    // Basic type
    Shader_resource(const std::string&    basic_name,
                    gl::Uniform_type      basic_type,
                    std::optional<size_t> array_size = {},
                    Shader_resource*      parent = nullptr);

    // Sampler
    Shader_resource(const std::string&              sampler_name,
                    gsl::not_null<Shader_resource*> parent,
                    int                             location,
                    gl::Uniform_type                sampler_type,
                    std::optional<size_t>           array_size = {},
                    std::optional<int>              dedicated_texture_unit = {});

    // Constructor with no arguments creates default uniform block
    Shader_resource();

    ~Shader_resource() = default;

    Shader_resource(const Shader_resource& other) = delete;

    Shader_resource(Shader_resource&& other) = default;

    auto is_array() const
    -> bool;

    auto dedicated_texture_unit_index() const
    -> std::optional<int>;

    auto type() const
    -> Type;

    auto name() const
    -> const std::string&;

    auto array_size() const
    -> std::optional<size_t>;

    auto basic_type() const
    -> gl::Uniform_type;

    // Only? for uniforms in default uniform block
    // For default uniform block, this is the next available location.
    auto location() const
    -> int;

    auto index_in_parent() const
    -> size_t;

    auto offset_in_parent() const
    -> size_t;

    auto parent() const
    -> Shader_resource*;

    auto member_count() const
    -> size_t;

    auto members() const
    -> const Member_collection&;

    auto member(const std::string& name) const
    -> const Shader_resource*;

    auto binding_point() const
    -> unsigned int;

    // Returns size of block.
    // For arrays, size of one element is returned.
    auto size_bytes() const
    -> size_t;

    auto offset() const
    -> size_t;

    auto next_member_offset() const
    -> size_t;

    auto type_string() const
    -> std::string;

    auto layout_string() const
    -> std::string;

    auto source(int indent_level = 0) const
    -> std::string;

    auto add_struct(const std::string&              name,
                    gsl::not_null<Shader_resource*> struct_type,
                    std::optional<size_t>           array_size = {})
    -> const Shader_resource&;

    auto add_sampler(const std::string&    name,
                     gl::Uniform_type      sampler_type,
                     std::optional<size_t> array_size = {},
                     std::optional<int>    dedicated_texture_unit = {})
    -> const Shader_resource&;

    auto add_float(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

    auto add_vec2(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

    auto add_vec3(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

    auto add_vec4(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

    auto add_mat4(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

    auto add_int(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

    auto add_uint(const std::string& name, std::optional<size_t> array_size = {})
    -> const Shader_resource&;

private:
    void align_offset_to(unsigned int alignment);

    void indent(std::stringstream& ss, int indent_level) const;

    // Any shader type declaration
    Type                  m_type{Type::default_uniform_block};
    std::string           m_name;
    std::optional<size_t> m_array_size; // 0 means unsized
    Shader_resource*      m_parent          {nullptr};
    size_t                m_index_in_parent {0};
    size_t                m_offset_in_parent{0};

    // Basic type declaration
    //Precision              m_precision{Precision::highp};
    gl::Uniform_type   m_basic_type{gl::Uniform_type::bool_};

    // Uniforms in default uniform block - TODO plus some others?
    // For default uniform block, this is next available location (initialized to 0)
    int                m_location{-1};

    // Struct instance
    Shader_resource*   m_struct_type{nullptr};

    // Samplers, in default uniform block
    std::optional<int> m_dedicated_texture_unit_index;

    // Aggregate type declation
    Member_collection  m_members;
    size_t             m_offset{0}; // where next member will be placed

    // Interface blocks (aggregate type declaration)
    std::string        m_block_name;
    int                m_binding_point{-1};

    // Only used for uniforms in program
};

} // namespace erhe::graphics

#endif // shader_type_declaration_hpp_erhe_graphics
