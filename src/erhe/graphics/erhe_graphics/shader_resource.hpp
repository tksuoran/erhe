#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gl/wrapper_enums.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace erhe::dataformat {
    class Vertex_attribute;
}

namespace erhe::graphics {

class Instance;

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
class Shader_resource final
{
public:
    enum class Type : unsigned int {
        basic                 = 0, // non sampler basic types
        sampler               = 1,
        struct_type           = 2, // not really a resource, just type declaration
        struct_member         = 3,
        default_uniform_block = 4,
        uniform_block         = 5,
        shader_storage_block  = 6
    };

    static auto is_basic           (const Type type) -> bool;
    static auto is_aggregate       (const Type type) -> bool;
    static auto should_emit_members(const Type type) -> bool;
    static auto is_block           (const Type type) -> bool;
    static auto uses_binding_points(const Type type) -> bool;

    enum class Precision : unsigned int {
        lowp    = 0,
        mediump = 1,
        highp   = 2,
        superp  = 3 // only reserved - not used
    };

    using Member_collection = std::vector<std::unique_ptr<Shader_resource>>;

    [[nodiscard]] static auto c_str(Precision v) -> const char*;

    // Struct definition
    Shader_resource(Instance& instance, const std::string_view struct_type_name, Shader_resource* parent = nullptr);

    // Struct member
    Shader_resource(
        Instance&                        instance,
        const std::string_view           struct_member_name,
        Shader_resource*                 struct_type,
        const std::optional<std::size_t> array_size = {},
        Shader_resource*                 parent = nullptr
    );

    // Block (uniform block or shader storage block)
    Shader_resource(
        Instance&                        instance,
        const std::string_view           block_name,
        int                              binding_point,
        Type                             block_type,
        const std::optional<std::size_t> array_size = {}
    );

    // Basic type
    Shader_resource(
        Instance&                        instance,
        std::string_view                 basic_name,
        gl::Uniform_type                 basic_type,
        const std::optional<std::size_t> array_size = {},
        Shader_resource*                 parent = nullptr
    );

    // Sampler
    Shader_resource(
        Instance&                        instance,
        const std::string_view           sampler_name,
        Shader_resource*                 parent,
        int                              location,
        gl::Uniform_type                 sampler_type,
        const std::optional<std::size_t> array_size = {},
        const std::optional<int>         dedicated_texture_unit = {}
    );

    // Constructor for creating  default uniform block
    explicit Shader_resource(Instance& instance);
    ~Shader_resource() noexcept;
    Shader_resource(const Shader_resource& other) = delete;
    Shader_resource(Shader_resource&& other);

    [[nodiscard]] auto is_array        () const -> bool;
    [[nodiscard]] auto type            () const -> Type;
    [[nodiscard]] auto name            () const -> const std::string&;
    [[nodiscard]] auto array_size      () const -> std::optional<std::size_t>;
    [[nodiscard]] auto basic_type      () const -> gl::Uniform_type;

    // Only? for uniforms in default uniform block
    // For default uniform block, this is the next available location.
    [[nodiscard]] auto location          () const -> int;
    [[nodiscard]] auto index_in_parent   () const -> std::size_t;
    [[nodiscard]] auto offset_in_parent  () const -> std::size_t;
    [[nodiscard]] auto parent            () const -> Shader_resource*;
    [[nodiscard]] auto member_count      () const -> std::size_t;
    [[nodiscard]] auto member            (const std::string_view name) const -> Shader_resource*;
    [[nodiscard]] auto binding_point     () const -> unsigned int;
    [[nodiscard]] auto get_binding_target() const -> gl::Buffer_target;

    // Returns size of block.
    // For arrays, size of one element is returned.
    [[nodiscard]] auto size_bytes        () const -> std::size_t;
    [[nodiscard]] auto offset            () const -> std::size_t;
    [[nodiscard]] auto next_member_offset() const -> std::size_t;
    [[nodiscard]] auto type_string       () const -> std::string;
    [[nodiscard]] auto layout_string     () const -> std::string;

    [[nodiscard]] auto source(int indent_level = 0) const -> std::string;

    static constexpr const std::size_t unsized_array = 0;

    void set_readonly (bool value);
    void set_writeonly(bool value);
    [[nodiscard]] auto get_readonly () const -> bool;
    [[nodiscard]] auto get_writeonly() const -> bool;

    auto add_struct(
        const std::string_view           name,
        Shader_resource*                 struct_type,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_sampler(
        const std::string_view           name,
        gl::Uniform_type                 sampler_type,
        const std::optional<int>         dedicated_texture_unit = {},
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_float(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_vec2(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_vec3(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_vec4(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_mat4(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_int(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uint(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uvec2(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uvec3(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uvec4(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uint64(
        const std::string_view           name,
        const std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_attribute(const erhe::dataformat::Vertex_attribute& attribute) -> Shader_resource*;

private:
    void sanitize(const std::optional<std::size_t>& array_size) const;

    void align_offset_to(const unsigned int alignment);

    void indent(std::stringstream& ss, const int indent_level) const;

    Instance&                  m_instance;

    // Any shader type declaration
    Type                       m_type{Type::default_uniform_block};
    std::string                m_name;
    std::optional<std::size_t> m_array_size; // 0 means unsized
    Shader_resource*           m_parent          {nullptr};
    std::size_t                m_index_in_parent {     0};
    std::size_t                m_offset_in_parent{     0};

    // Basic type declaration
    //Precision              m_precision{Precision::highp};
    gl::Uniform_type  m_basic_type{gl::Uniform_type::bool_};

    // Uniforms in default uniform block - TODO plus some others?
    // For default uniform block, this is next available location (initialized to 0)
    int               m_location{-1};

    // Struct instance
    Shader_resource*  m_struct_type{nullptr};

    // Aggregate type declation
    Member_collection m_members;
    std::size_t       m_offset{0}; // where next member will be placed

    // Interface blocks (aggregate type declaration)
    std::string       m_block_name;

    // Blocks and samplers in default uniform block
    int               m_binding_point{-1};

    bool m_readonly {false};
    bool m_writeonly{false};

    // Only used for uniforms in program
};

void add_vertex_stream(const erhe::dataformat::Vertex_stream& vertex_stream, erhe::graphics::Shader_resource& vertex_struct, erhe::graphics::Shader_resource& vertices_block);

} // namespace erhe::graphics
