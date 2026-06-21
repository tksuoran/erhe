#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/enums.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace erhe::dataformat { class Vertex_attribute; }

namespace erhe::graphics {

class Device;

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
        basic                = 0, // non sampler basic types
        sampler              = 1,
        struct_type          = 2, // not really a resource, just type declaration
        struct_member        = 3,
        samplers             = 4, // was: default_uniform_block
        uniform_block        = 5,
        shader_storage_block = 6,
        image                = 7  // load/store storage image (compute)
    };

    // GLSL interface block memory layout. Picks the alignment / padding
    // rule set the GLSL spec defines:
    //   std140 - rule 9 rounds the struct base alignment up to vec4 (16);
    //            arrays of scalars / vectors round element stride up to
    //            vec4. Required for uniform blocks. Used by shader storage
    //            blocks when GLSL < 4.30.
    //   std430 - rule 9 does NOT round to vec4; array element strides
    //            track the array element's natural alignment. Available
    //            for shader storage blocks at GLSL 4.30 and later.
    enum class Layout : unsigned int {
        std140 = 0,
        std430 = 1
    };

    [[nodiscard]] static auto is_basic           (Type type) -> bool;
    [[nodiscard]] static auto is_aggregate       (Type type) -> bool;
    [[nodiscard]] static auto should_emit_layout (Type type) -> bool;
    [[nodiscard]] static auto should_emit_members(Type type) -> bool;
    [[nodiscard]] static auto is_block           (Type type) -> bool;
    [[nodiscard]] static auto uses_binding_points(Type type) -> bool;

    enum class Precision : unsigned int {
        lowp    = 0,
        mediump = 1,
        highp   = 2,
        superp  = 3 // only reserved - not used
    };

    using Member_collection = std::vector<std::unique_ptr<Shader_resource>>;

    [[nodiscard]] static auto c_str(Precision v) -> const char*;

    // Struct definition
    Shader_resource(
        Device&          device,
        std::string_view struct_type_name,
        Shader_resource* parent = nullptr
    );

    // Struct member
    Shader_resource(
        Device&                    device,
        std::string_view           struct_member_name,
        Shader_resource*           struct_type,
        std::optional<std::size_t> array_size = {},
        Shader_resource*           parent = nullptr
    );

    // Block (uniform block or shader storage block)
    class Block_create_info
    {
    public:
        std::string_view           name;
        int                        binding_point;
        Type                       type;
        std::optional<std::size_t> array_size{};
        bool                       readonly {false};
        bool                       writeonly{false};
    };
    Shader_resource(Device& device, const Block_create_info& create_info);

    // Block (uniform block or shader storage block) -- legacy positional arguments
    Shader_resource(
        Device&                    device,
        std::string_view           block_name,
        int                        binding_point,
        Type                       block_type,
        std::optional<std::size_t> array_size = {}
    );

    // Basic type
    Shader_resource(
        Device&                    device,
        std::string_view           basic_name,
        Glsl_type                  basic_type,
        std::optional<std::size_t> array_size = {},
        Shader_resource*           parent = nullptr
    );

    // Sampler
    Shader_resource(
        Device&                    device,
        std::string_view           sampler_name,
        Shader_resource*           parent,
        int                        location,
        Glsl_type                  sampler_type,
        Sampler_aspect             sampler_aspect,
        bool                       is_texture_heap,
        std::optional<std::size_t> array_size = {},
        std::optional<int>         dedicated_texture_unit = {}
    );

    // Storage image (load/store image, e.g. compute shader output).
    // Lives in the default uniform block like a sampler, but emits
    // "layout(binding = N, <format>) uniform image2D name;". The format
    // layout qualifier (e.g. "rgba16f") is required so both imageLoad and
    // imageStore are valid without readonly/writeonly qualifiers.
    Shader_resource(
        Device&          device,
        std::string_view image_name,
        Shader_resource* parent,
        int              binding_point,
        Glsl_type        image_type,
        std::string_view image_format
    );

    // Constructor for creating  default uniform block
    explicit Shader_resource(Device& device);
    ~Shader_resource() noexcept;
    Shader_resource(const Shader_resource& other) = delete;
    Shader_resource(Shader_resource&& other) noexcept;

    [[nodiscard]] auto is_array        () const -> bool;
    [[nodiscard]] auto get_type        () const -> Type;
    [[nodiscard]] auto get_name        () const -> const std::string&;
    [[nodiscard]] auto get_array_size  () const -> std::optional<std::size_t>;
    [[nodiscard]] auto get_basic_type  () const -> Glsl_type;

    // Only? for uniforms in default uniform block
    // For default uniform block, this is the next available location.
    [[nodiscard]] auto get_location        () const -> int;
    [[nodiscard]] auto get_index_in_parent () const -> std::size_t;
    [[nodiscard]] auto get_offset_in_parent() const -> std::size_t;
    [[nodiscard]] auto get_parent          () const -> Shader_resource*;
    [[nodiscard]] auto get_member_count    () const -> std::size_t;
    [[nodiscard]] auto get_member          (std::string_view name) const -> Shader_resource*;
    [[nodiscard]] auto get_members         () const -> const Member_collection&;
    [[nodiscard]] auto get_binding_point   () const -> unsigned int;
    [[nodiscard]] auto get_binding_target  () const -> Buffer_target;
    [[nodiscard]] auto get_texture_unit    () const -> int;
    [[nodiscard]] auto get_sampler_aspect  () const -> Sampler_aspect;
    [[nodiscard]] auto get_is_texture_heap () const -> bool;

    // Returns size of block.
    // For arrays, size of one element is returned.
    //
    // Layout chooses std140 vs std430 alignment rules (see GLSL spec rule
    // 9 -- struct size is rounded up to the struct's base alignment; in
    // std140 the base alignment is further rounded up to vec4). Defaults
    // to std140 because every uniform block uses it and SSBOs at GLSL <
    // 4.30 fall back to std140 as well. Pass std430 explicitly when
    // sizing an SSBO that the device declares with std430 (GLSL >= 4.30).
    [[nodiscard]] auto get_size_bytes        (Layout layout = Layout::std140) const -> std::size_t;

    // Base alignment of this resource under the requested layout. For
    // aggregate types, this is the maximum base alignment of any member
    // (rounded up to vec4 in std140). For Type::basic, it is the natural
    // GLSL alignment of the type.
    [[nodiscard]] auto get_base_alignment    (Layout layout = Layout::std140) const -> std::size_t;

    [[nodiscard]] auto get_offset            () const -> std::size_t;
    [[nodiscard]] auto get_next_member_offset() const -> std::size_t;
    [[nodiscard]] auto get_type_string       () const -> std::string;
    [[nodiscard]] auto get_layout_string     (uint32_t sampler_binding_offset = 0) const -> std::string;

    [[nodiscard]] auto get_source(int indent_level = 0, uint32_t sampler_binding_offset = 0) const -> std::string;

    static constexpr const std::size_t unsized_array = 0;

    void set_readonly (bool value);
    void set_writeonly(bool value);
    [[nodiscard]] auto get_readonly () const -> bool;
    [[nodiscard]] auto get_writeonly() const -> bool;

    auto add_struct(
        std::string_view           name,
        Shader_resource*           struct_type,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_sampler(
        std::string_view           name,
        Glsl_type                  sampler_type,
        Sampler_aspect             sampler_aspect,
        bool                       is_texture_heap,
        std::optional<uint32_t>    dedicated_texture_unit = {},
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    // Add a load/store storage image to the default uniform block. Emits
    // "layout(binding = binding_point, image_format) uniform <image_type> name;".
    auto add_image(
        std::string_view name,
        Glsl_type        image_type,
        std::string_view image_format,
        int              binding_point
    ) -> Shader_resource*;

    auto add_float(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_vec2(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_vec3(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_vec4(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_mat4(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_int(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uint(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uvec2(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uvec3(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uvec4(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_uint64(
        std::string_view           name,
        std::optional<std::size_t> array_size = {}
    ) -> Shader_resource*;

    auto add_attribute(const erhe::dataformat::Vertex_attribute& attribute) -> Shader_resource*;

private:
    void sanitize(const std::optional<std::size_t>& array_size) const;

    void align_offset_to(unsigned int alignment);

    // Walks up m_parent to the nearest block (uniform / shader storage)
    // and returns the GLSL layout that block uses. Standalone struct
    // types (no block ancestor) default to std140 (the conservative
    // choice -- it's a valid base alignment for any later use site).
    [[nodiscard]] auto get_block_layout() const -> Layout;

    void indent(std::stringstream& ss, int indent_level) const;

    Device&                    m_device;

    // Any shader type declaration
    Type                       m_type{Type::samplers};
    std::string                m_name;
    std::optional<std::size_t> m_array_size; // 0 means unsized
    Shader_resource*           m_parent          {nullptr};
    std::size_t                m_index_in_parent {     0};
    std::size_t                m_offset_in_parent{     0};

    // Basic type declaration
    //Precision              m_precision{Precision::highp};
    Glsl_type         m_basic_type{Glsl_type::bool_};

    // Uniforms in default uniform block - TODO plus some others?
    // For default uniform block, this is next available location (initialized to 0)
    int               m_location{-1};

    // Struct instance
    Shader_resource*  m_struct_type{nullptr};

    // Aggregate type declation
    Member_collection m_members;
    std::size_t       m_offset{0}; // where next member will be placed

    // Largest GLSL base alignment among the members added so far. Drives
    // the struct's own base alignment per GLSL std140/std430 rule 9.
    // Updated by every add_* method on an aggregate (struct_type, block).
    std::size_t       m_member_max_alignment{1};

    // Interface blocks (aggregate type declaration)
    std::string       m_block_name;

    // For default uniform block hosted blocks and texture samplers (texture unit)
    int               m_binding_point{-1};

    // Only used when m_type == Type::sampler. Annotates which image aspect
    // (color/depth/stencil) the sampler reads, so the texture heap can pick
    // the right VkImageView aspect mask without inspecting texture formats.
    Sampler_aspect    m_sampler_aspect{Sampler_aspect::color};

    // Only used when m_type == Type::sampler. true means this sampler is
    // bound through the bindless Texture_heap (argument buffer on Metal,
    // descriptor indexing array on Vulkan). false means it is bound directly
    // by Render_command_encoder::set_sampled_image() at draw time, with a
    // dedicated descriptor / [[texture(N)]] binding. The two paths are
    // mutually exclusive: a texture_heap sampler must NOT also have a
    // dedicated_texture_unit.
    // TODO: Maybe is_texture_heap is redundant once all callers settle on
    // the convention "dedicated_texture_unit set <=> is_texture_heap == false";
    // see the assert in add_sampler. If the assert holds for every callsite
    // we can drop this flag and derive it from the dedicated_texture_unit
    // optional.
    bool              m_is_texture_heap{false};

    bool              m_readonly {false};
    bool              m_writeonly{false};

    // Only used when m_type == Type::image. GLSL format layout qualifier
    // (e.g. "rgba16f") emitted inside the layout(...) of the image
    // declaration. Required so imageLoad/imageStore are valid.
    std::string       m_image_format;

    // Only used for uniforms in program
};

void add_vertex_stream(const erhe::dataformat::Vertex_stream& vertex_stream, Shader_resource& vertex_struct,
    Shader_resource& vertices_block);

// GLSL std140/std430 struct base alignment (spec rule 9):
//   std140: max(member_max_alignment, 16)
//   std430: member_max_alignment
[[nodiscard]] auto glsl_struct_base_alignment(
    std::size_t             member_max_alignment,
    Shader_resource::Layout layout
) -> std::size_t;

// Round raw_offset up to the next multiple of base_alignment. The size
// of a GLSL struct is its raw offset (offset of the next-to-be-placed
// member, i.e. one past the last member) rounded up to the struct's
// base alignment. base_alignment must be > 0.
[[nodiscard]] auto glsl_round_up_size(
    std::size_t raw_offset,
    std::size_t base_alignment
) -> std::size_t;

} // namespace erhe::graphics
