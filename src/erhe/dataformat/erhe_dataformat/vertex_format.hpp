#pragma once

#include "erhe_dataformat/dataformat.hpp"

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
};

static constexpr std::size_t normal_attribute        = 0;
static constexpr std::size_t normal_attribute_smooth = 1;
static constexpr std::size_t normal_attribute_flat   = 2;

static constexpr std::size_t custom_attribute_id                 = 0;
static constexpr std::size_t custom_attribute_aniso_control      = 1; // anisotropy tangent_space
static constexpr std::size_t custom_attribute_valency_edge_count = 2; // uvec2 vertex valency and polygon edge count
static constexpr std::size_t custom_attribute_metallic_roughness = 3; // TODO metallic roughess_x roughness_y

enum class Vertex_step : unsigned int
{
    Step_per_vertex = 0,
    Step_per_instance
};

class Vertex_stream
{
public:
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

    std::vector<Vertex_stream> streams;
};

// Compact identity for a Vertex_format -- one bit per (usage, index) attribute
// slot. Two formats are considered equal by Mesh_memory's format pool registry
// iff their keys match. The key assumes canonical element types per usage
// (e.g. position = vec3 float, joint_indices = vec4 u8); if a future format
// diverges from the canonical type for a usage, it needs a dedicated bit (or
// a wider key) rather than a silent collision against the same key.
//
// Bit layout (uint32_t):
//   0       position
//   1       tangent
//   2       bitangent
//   4..7    normal[0..3]      (face, smooth, flat, ...)
//   8       color
//   9       joint_indices
//   10      joint_weights
//   12..15  tex_coord[0..3]
//   16..19  custom[0..3]
//
// Bits with no current attribute meaning (3, 11, 20..31) are reserved.
[[nodiscard]] auto compute_vertex_format_key(const Vertex_format& format) -> uint32_t;

namespace Vertex_format_key {

constexpr uint32_t position      = 1u <<  0;
constexpr uint32_t tangent       = 1u <<  1;
constexpr uint32_t bitangent     = 1u <<  2;
constexpr uint32_t normal_0      = 1u <<  4;
constexpr uint32_t normal_1      = 1u <<  5;
constexpr uint32_t normal_2      = 1u <<  6;
constexpr uint32_t normal_3      = 1u <<  7;
constexpr uint32_t color         = 1u <<  8;
constexpr uint32_t joint_indices = 1u <<  9;
constexpr uint32_t joint_weights = 1u << 10;
constexpr uint32_t tex_coord_0   = 1u << 12;
constexpr uint32_t tex_coord_1   = 1u << 13;
constexpr uint32_t tex_coord_2   = 1u << 14;
constexpr uint32_t tex_coord_3   = 1u << 15;
constexpr uint32_t custom_0      = 1u << 16;
constexpr uint32_t custom_1      = 1u << 17;
constexpr uint32_t custom_2      = 1u << 18;
constexpr uint32_t custom_3      = 1u << 19;

} // namespace Vertex_format_key

} // namespace erhe::dataformat
