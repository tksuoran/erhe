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

[[nodiscard]] auto c_str(const Vertex_attribute_usage usage) -> const char*;

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
    void emplace_back(
        erhe::dataformat::Format format,
        Vertex_attribute_usage   usage_type,
        std::size_t              usage_index = 0
    );

    std::vector<Vertex_attribute> attributes;
    std::size_t                   binding{0};
    std::size_t                   stride {0};
    Vertex_step                   step   {Vertex_step::Step_per_vertex};
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

} // namespace erhe::dataformat
