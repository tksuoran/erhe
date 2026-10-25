#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_hash/hash.hpp"

#include <string>
#include <vector>

namespace erhe::dataformat {

enum class Vertex_attribute_usage : uint32_t {
    none          = 0,
    position      = 1,
    tangent       = 2,
    bitangent     = 3,
    normal        = 4,
    color         = 5,
    joint_indices = 6,
    joint_weights = 7,
    tex_coord     = 8,
    custom        = 9
};

[[nodiscard]] auto c_str(Vertex_attribute_usage usage) -> const char*;

class Vertex_attribute
{
public:
    erhe::dataformat::Format format     {erhe::dataformat::Format::format_undefined};
    Vertex_attribute_usage   usage_type {Vertex_attribute_usage::none};
    std::size_t              usage_index{0};
    std::size_t              offset     {0};

    [[nodiscard]] auto to_string() const -> std::string;

    [[nodiscard]] auto operator==(const Vertex_attribute& other) const -> bool
    {
        return
            (format      == other.format) &&
            (usage_type  == other.usage_type) &&
            (usage_index == other.usage_index) &&
            (offset      == other.offset);
    }
    [[nodiscard]] auto operator!=(const Vertex_attribute& other) const -> bool
    {
        return !(*this == other);
    }
};

static constexpr std::size_t normal_attribute        = 0;
static constexpr std::size_t normal_attribute_smooth = 1;
static constexpr std::size_t normal_attribute_flat   = 2;

static constexpr std::size_t custom_attribute_id                 = 0;
static constexpr std::size_t custom_attribute_aniso_control      = 1; // anisotropy tangent_space
static constexpr std::size_t custom_attribute_valency_edge_count = 2; // uvec2 vertex valency and polygon edge count
static constexpr std::size_t custom_attribute_metallic_roughness = 3; // TODO metallic roughess_x roughness_y
static constexpr std::size_t custom_attribute_wireframe          = 4; // uint: corner index (bits 0..1) + real-edge mask (bits 2..4), expanded solid-wireframe fill

enum class Vertex_step : unsigned int
{
    Step_per_vertex = 0,
    Step_per_instance
};

[[nodiscard]] auto c_str(Vertex_step step) -> const char*;

// Backend-imposed minimums on how a stream is packed. Both default to 1, which
// is exactly no constraint: the resulting offsets and stride are then whatever
// the attribute component sizes alone produce.
//
// They exist because a 2-byte position format takes the stream-0 strides off
// multiples of 4 and moves the skinned stream's joint attributes to offsets 6
// and 10 - and Metal requires MTLVertexBufferLayoutDescriptor.stride to be a
// multiple of 4, while some portability-subset implementations also constrain
// attribute offsets. GL and Vulkan core impose neither.
//
// A stream must be packed with its final values BEFORE it backs any allocation:
// Buffer_pool captures the stride at pool creation and all byte_offset / stride
// arithmetic depends on it, and is_compatible() is pointer identity, so it
// would not catch a stride changed afterwards.
class Vertex_stream_packing
{
public:
    std::size_t min_attribute_alignment{1};
    std::size_t min_stride_alignment   {1};

    [[nodiscard]] auto operator==(const Vertex_stream_packing& other) const -> bool
    {
        return
            (min_attribute_alignment == other.min_attribute_alignment) &&
            (min_stride_alignment    == other.min_stride_alignment);
    }

    [[nodiscard]] auto operator!=(const Vertex_stream_packing& other) const -> bool
    {
        return !(*this == other);
    }
};

class Vertex_stream
{
public:

    static constexpr std::size_t binding_unused_dummy = 0xffff;

    explicit Vertex_stream(std::size_t binding);

    Vertex_stream(std::size_t binding, std::initializer_list<Vertex_attribute> attributes);

    [[nodiscard]] auto find_attribute(Vertex_attribute_usage usage_type, std::size_t index = 0) const -> const Vertex_attribute*;
    auto emplace_back(
        erhe::dataformat::Format format,
        Vertex_attribute_usage   usage_type,
        std::size_t              usage_index = 0
    ) -> Vertex_attribute&;

    // Call after all emplace_back() calls to pad stride for Vulkan alignment
    void finalize_stride();

    // Recompute every attribute offset and the stride under the given packing
    // constraints, from the attributes in declaration order - i.e. exactly what
    // the initializer-list constructor does, but with backend minimums applied.
    // With the default (all-1) packing it reproduces that constructor's layout
    // byte for byte.
    //
    // It always finalizes the stride, so a stream built by emplace_back() without
    // a finalize_stride() call can come back padded. That is the finalized layout
    // either way; only an unfinalized stream sees a difference.
    void repack(const Vertex_stream_packing& packing);

    [[nodiscard]] auto is_buffer_compatible(const Vertex_stream& other) const -> bool;
    [[nodiscard]] auto get_hash() const -> uint64_t;
    [[nodiscard]] auto to_string() const -> std::string;

    [[nodiscard]] auto operator==(const Vertex_stream& other) const -> bool
    {
        return
            (binding    == other.binding   ) &&
            (stride     == other.stride    ) &&
            (step       == other.step      ) &&
            (attributes == other.attributes);
    }
    [[nodiscard]] auto operator!=(const Vertex_stream& other) const -> bool
    {
        return !(*this == other);
    }

    std::vector<Vertex_attribute> attributes;
    std::size_t                   binding      {0};
    std::size_t                   stride       {0};
    std::size_t                   max_alignment{1};
    Vertex_step                   step         {Vertex_step::Step_per_vertex};
};

struct Attribute_stream
{
    const Vertex_attribute* attribute{nullptr};
    const Vertex_stream*    stream   {nullptr};
};

class Vertex_format
{
public:
    Vertex_format();
    Vertex_format(std::initializer_list<Vertex_stream> streams);

    [[nodiscard]] auto get_stream    (std::size_t binding) const -> const Vertex_stream*;
    [[nodiscard]] auto find_attribute(Vertex_attribute_usage usage_type, std::size_t index = 0) const -> Attribute_stream;
    [[nodiscard]] auto get_attributes() const -> std::vector<Attribute_stream>;
    [[nodiscard]] auto get_hash      () const -> uint64_t;
    [[nodiscard]] auto to_string     () const -> std::string;

    // Repack every stream; see Vertex_stream::repack().
    void repack(const Vertex_stream_packing& packing);

    [[nodiscard]] auto operator==(const Vertex_format& other) const -> bool
    {
        return (streams == other.streams);
    }
    [[nodiscard]] auto operator!=(const Vertex_format& other) const -> bool
    {
        return !(*this == other);
    }

    std::vector<Vertex_stream> streams;
};

// How a_position is stored in a vertex format, and therefore how the vertex
// shader must decode it. This is a property of the vertex format, not of any
// one shader, which is why it lives here: both the shader key (which hashes it
// into the variant identity) and Shader_stages_create_info::attributes_source()
// (which emits the matching ERHE_VERTEX_POSITION_ENCODING define) derive it
// from the format with get_vertex_position_encoding() below.
//
// Keep in sync with the ERHE_VERTEX_POSITION_ENCODING_* macros in
// res/shaders/erhe_vertex_position.glsl.
enum class Vertex_position_encoding : uint32_t
{
    passthrough    = 0, // a_position is an object space float3
    snorm16x3_aabb = 1  // a_position is snorm16x3, normalized into the primitive's object space AABB
    // snorm16x4_aabb = 2 // future work: padded, or w carries a payload
};

[[nodiscard]] auto c_str(Vertex_position_encoding encoding) -> const char*;

// passthrough for a null format, or for one whose position attribute is not a
// recognized quantized format (including a format with no position at all).
[[nodiscard]] auto get_vertex_position_encoding(const Vertex_format* vertex_format) -> Vertex_position_encoding;

// How texcoord channels 0 and 1 are stored, and therefore how a shader must
// decode them. Same mechanism as the position encoding above: derived from the
// format, hashed into the shader key, and emitted as
// ERHE_VERTEX_TEXCOORD_ENCODING by attributes_source().
//
// Channel 2 (lightmap UVs) is deliberately NOT covered: it is in [0, 1] by
// construction, so it is stored as plain unorm16 with no affine and is read
// undecoded. Only channels 0 and 1 can carry tiling UVs and therefore need the
// per-primitive range.
//
// Keep in sync with the ERHE_VERTEX_TEXCOORD_ENCODING_* macros in
// res/shaders/erhe_vertex_texcoord.glsl.
enum class Vertex_texcoord_encoding : uint32_t
{
    passthrough      = 0, // a_texcoord_0 / a_texcoord_1 are float2
    unorm16x2_affine = 1  // unorm16x2, normalized into the primitive's per-channel UV range
};

[[nodiscard]] auto c_str(Vertex_texcoord_encoding encoding) -> const char*;

// passthrough for a null format, or for one whose texcoord channel 0 is not a
// recognized quantized format (including a format with no texcoord at all).
[[nodiscard]] auto get_vertex_texcoord_encoding(const Vertex_format* vertex_format) -> Vertex_texcoord_encoding;

// How the tangent frame is stored. Same mechanism as the two encodings above.
//
// quaternion16 replaces BOTH the normal and the tangent attribute with a single
// one carrying the (tangent, bitangent, normal) frame as a quaternion, stored
// under the TANGENT usage as format_16_vec4_sint - signed rather than snorm so
// the component index and the handedness bit in the w lane stay exactly
// readable in GLSL. The normal attribute is then absent from the format, which
// is why Shader_key::derive() has to report normal and tangent presence from
// this encoding rather than from attribute lookup alone.
//
// Keep in sync with the ERHE_VERTEX_TBN_ENCODING_* macros in
// res/shaders/erhe_vertex_tbn.glsl.
enum class Vertex_tbn_encoding : uint32_t
{
    passthrough  = 0, // a_normal is float3 and a_tangent is float4 (xyz + handedness w)
    quaternion16 = 1  // a_tangent is the int16x4 encoded frame quaternion; there is no a_normal
};

[[nodiscard]] auto c_str(Vertex_tbn_encoding encoding) -> const char*;

[[nodiscard]] auto get_vertex_tbn_encoding(const Vertex_format* vertex_format) -> Vertex_tbn_encoding;

// How skinning weights are stored. Same mechanism again.
//
// unorm16x3_implicit_sum drops the smallest of the four weights and lets the
// shader recover it from the fact that the four sum to one; the joint indices
// are permuted in lockstep so the dropped influence is always the last. The
// three that remain get 16 bits each rather than 8, which is why this is a
// precision win at the same stride rather than a size win.
//
// Keep in sync with the ERHE_VERTEX_JOINT_WEIGHTS_ENCODING_* macros in
// res/shaders/erhe_vertex_joint_weights.glsl.
enum class Vertex_joint_weights_encoding : uint32_t
{
    passthrough            = 0, // a_joint_weights_0 is unorm8x4, all four weights stored
    unorm16x3_implicit_sum = 1  // a_joint_weights_0 is unorm16x3; the fourth weight is 1 - their sum
};

[[nodiscard]] auto c_str(Vertex_joint_weights_encoding encoding) -> const char*;

[[nodiscard]] auto get_vertex_joint_weights_encoding(const Vertex_format* vertex_format) -> Vertex_joint_weights_encoding;

// Canonical text of a vertex format's memory layout, the `signature` of the
// ERHE_gpu_vertex_streams glTF extension (doc/gltf_extensions/ERHE_gpu_vertex_streams.md):
// per stream in binding order `s<i>=<stride>:` followed by its attributes in
// declaration order, `<usage>.<index>=<format>@<offset>` separated by `,`;
// streams separated by `|`. <usage> is c_str(Vertex_attribute_usage),
// <format> is c_str(Format) without its `format_` prefix. Example:
// `s0=8:position.0=16_vec3_snorm@0|s1=24:tangent.0=16_vec4_sint@0,...`.
// Two formats with equal signatures have byte-identical vertex layouts.
[[nodiscard]] auto get_vertex_format_signature(const Vertex_format& vertex_format) -> std::string;

} // namespace erhe::dataformat
