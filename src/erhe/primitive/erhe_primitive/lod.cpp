#include "erhe_primitive/lod.hpp"

#include "erhe_primitive/primitive_log.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <meshoptimizer.h>

#include <glm/glm.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace erhe::primitive {

namespace {

// A level that keeps this fraction of its input triangles or more is not worth
// its index memory; generation stops there (Godot's rule).
constexpr float       min_reduction_ratio = 0.75f;
// Generation stops once a level has this many indices or fewer (Godot's rule).
constexpr std::size_t min_index_count     = 24;
// Each level's error is at least this factor times the previous level's.
constexpr float       error_growth        = 1.5f;
// Smallest relative error a level > 0 records. A step over coplanar triangles
// reports an error of exactly 0; without a floor such a level would tie with
// level 0, and the selection could not tell the two apart.
constexpr float       min_relative_error  = 1.0e-6f;

constexpr uint32_t    no_vertex           = 0xffffffffu;

[[nodiscard]] auto read_as_vec4(
    std::span<const std::span<const uint8_t>> streams,
    const erhe::dataformat::Vertex_format&    format,
    const erhe::dataformat::Attribute_stream& attribute_stream,
    const std::size_t                         vertex,
    float                                     out_value[4]
) -> bool
{
    const std::size_t stream_index = static_cast<std::size_t>(attribute_stream.stream - format.streams.data());
    if (stream_index >= streams.size()) {
        return false;
    }
    const std::span<const uint8_t> stream_data = streams[stream_index];
    const std::size_t              stride      = attribute_stream.stream->stride;
    const std::size_t              offset      = (vertex * stride) + attribute_stream.attribute->offset;
    const std::size_t              size        = erhe::dataformat::get_format_size_bytes(attribute_stream.attribute->format);
    if ((offset + size) > stream_data.size()) {
        return false;
    }
    out_value[0] = 0.0f;
    out_value[1] = 0.0f;
    out_value[2] = 0.0f;
    out_value[3] = 0.0f;
    erhe::dataformat::convert(
        stream_data.data() + offset,
        attribute_stream.attribute->format,
        out_value,
        erhe::dataformat::Format::format_32_vec4_float,
        1.0f
    );
    return true;
}

// Bitwise hash over a run of floats.
class Float_run_hash
{
public:
    [[nodiscard]] auto operator()(const std::vector<float>& values) const -> std::size_t
    {
        uint64_t hash = 14695981039346656037ull;
        for (const float value : values) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hash = (hash ^ bits) * 1099511628211ull;
        }
        return static_cast<std::size_t>(hash);
    }
};

class Float_run_equal
{
public:
    [[nodiscard]] auto operator()(const std::vector<float>& lhs, const std::vector<float>& rhs) const -> bool
    {
        return
            (lhs.size() == rhs.size()) &&
            (std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(float)) == 0);
    }
};

// The joints a vertex is influenced by (non-zero weight), sorted, as a key.
[[nodiscard]] auto get_joint_key(const Lod_vertex_input& vertices, const std::size_t vertex) -> std::array<uint32_t, 4>
{
    std::array<uint32_t, 4> key{no_vertex, no_vertex, no_vertex, no_vertex};
    std::size_t             count = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const float weight = vertices.joint_weights.empty() ? ((i == 0) ? 1.0f : 0.0f) : vertices.joint_weights[(4 * vertex) + i];
        if (weight > 0.0f) {
            key[count] = static_cast<uint32_t>(vertices.joint_indices[(4 * vertex) + i]);
            ++count;
        }
    }
    std::sort(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(count));
    return key;
}

[[nodiscard]] auto get_vec3(std::span<const float> values, const std::size_t vertex) -> glm::vec3
{
    return glm::vec3{values[(3 * vertex) + 0], values[(3 * vertex) + 1], values[(3 * vertex) + 2]};
}

[[nodiscard]] auto safe_normalize(const glm::vec3 v) -> glm::vec3
{
    const float length = glm::length(v);
    return (length > 0.0f) ? (v / length) : glm::vec3{0.0f};
}

std::mutex s_totals_mutex;
Lod_totals s_totals;

} // anonymous namespace

auto Lod_vertex_data::get_input() const -> Lod_vertex_input
{
    return Lod_vertex_input{
        .vertex_count          = vertex_count,
        .positions             = positions,
        .normals               = normals,
        .colors                = colors,
        .seam_attributes       = seam_attributes,
        .seam_attribute_stride = seam_attribute_stride,
        .joint_indices         = joint_indices,
        .joint_weights         = joint_weights
    };
}

auto extract_lod_vertex_data(
    const std::span<const std::span<const uint8_t>> streams,
    const erhe::dataformat::Vertex_format&          source_format,
    const erhe::dataformat::Vertex_format&          key_format,
    const std::size_t                               vertex_count
) -> Lod_vertex_data
{
    ERHE_PROFILE_FUNCTION();

    using erhe::dataformat::Attribute_stream;
    using erhe::dataformat::Vertex_attribute_usage;

    Lod_vertex_data result{};
    const Attribute_stream position = source_format.find_attribute(Vertex_attribute_usage::position, 0);
    if ((position.attribute == nullptr) || (vertex_count == 0)) {
        return result;
    }
    const Attribute_stream normal  = source_format.find_attribute(Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute);
    const Attribute_stream color   = source_format.find_attribute(Vertex_attribute_usage::color, 0);
    const Attribute_stream tangent = source_format.find_attribute(Vertex_attribute_usage::tangent, 0);
    const Attribute_stream joints  = source_format.find_attribute(Vertex_attribute_usage::joint_indices, 0);
    const Attribute_stream weights = source_format.find_attribute(Vertex_attribute_usage::joint_weights, 0);

    // The seam attributes: what the drawn format carries besides the
    // simplification metric inputs, read from where the source stores it.
    class Seam_attribute
    {
    public:
        Attribute_stream source;
        std::size_t      component_count{0};
        bool             tangent_sign   {false};
    };
    std::vector<Seam_attribute> seam_attributes;
    std::size_t                 seam_stride = 0;
    for (const erhe::dataformat::Vertex_stream& key_stream : key_format.streams) {
        for (const erhe::dataformat::Vertex_attribute& key_attribute : key_stream.attributes) {
            const Vertex_attribute_usage usage = key_attribute.usage_type;
            if (
                ((usage == Vertex_attribute_usage::position) && (key_attribute.usage_index == 0)) ||
                (usage == Vertex_attribute_usage::normal)    ||
                (usage == Vertex_attribute_usage::bitangent)
            ) {
                continue;
            }
            const Attribute_stream source = source_format.find_attribute(usage, key_attribute.usage_index);
            if (source.attribute == nullptr) {
                continue;
            }
            if (usage == Vertex_attribute_usage::tangent) {
                seam_attributes.push_back(Seam_attribute{.source = source, .component_count = 1, .tangent_sign = true});
                seam_stride += 1;
                continue;
            }
            const std::size_t component_count = std::min<std::size_t>(4, erhe::dataformat::get_component_count(source.attribute->format));
            seam_attributes.push_back(Seam_attribute{.source = source, .component_count = component_count, .tangent_sign = false});
            seam_stride += component_count;
        }
    }

    result.vertex_count          = vertex_count;
    result.seam_attribute_stride = seam_stride;
    result.positions      .resize(3 * vertex_count);
    result.seam_attributes.resize(seam_stride * vertex_count);
    if (normal.attribute != nullptr) {
        result.normals.resize(3 * vertex_count);
    }
    if (color.attribute != nullptr) {
        result.colors.resize(4 * vertex_count);
    }
    const bool skinned = (joints.attribute != nullptr);
    if (skinned) {
        result.joint_indices.resize(4 * vertex_count);
        if (weights.attribute != nullptr) {
            result.joint_weights.resize(4 * vertex_count);
        }
    }
    static_cast<void>(tangent);

    float value[4];
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        if (!read_as_vec4(streams, source_format, position, vertex, value)) {
            return Lod_vertex_data{};
        }
        std::copy(value, value + 3, result.positions.begin() + static_cast<std::ptrdiff_t>(3 * vertex));
        if ((normal.attribute != nullptr) && read_as_vec4(streams, source_format, normal, vertex, value)) {
            std::copy(value, value + 3, result.normals.begin() + static_cast<std::ptrdiff_t>(3 * vertex));
        }
        if ((color.attribute != nullptr) && read_as_vec4(streams, source_format, color, vertex, value)) {
            std::copy(value, value + 4, result.colors.begin() + static_cast<std::ptrdiff_t>(4 * vertex));
        }
        if (skinned && read_as_vec4(streams, source_format, joints, vertex, value)) {
            std::copy(value, value + 4, result.joint_indices.begin() + static_cast<std::ptrdiff_t>(4 * vertex));
        }
        if (skinned && (weights.attribute != nullptr) && read_as_vec4(streams, source_format, weights, vertex, value)) {
            std::copy(value, value + 4, result.joint_weights.begin() + static_cast<std::ptrdiff_t>(4 * vertex));
        }
        std::size_t seam_offset = seam_stride * vertex;
        for (const Seam_attribute& seam_attribute : seam_attributes) {
            if (!read_as_vec4(streams, source_format, seam_attribute.source, vertex, value)) {
                value[0] = 0.0f;
                value[1] = 0.0f;
                value[2] = 0.0f;
                value[3] = 0.0f;
            }
            if (seam_attribute.tangent_sign) {
                result.seam_attributes[seam_offset] = (value[3] < 0.0f) ? -1.0f : 1.0f;
            } else {
                for (std::size_t component = 0; component < seam_attribute.component_count; ++component) {
                    result.seam_attributes[seam_offset + component] = value[component];
                }
            }
            seam_offset += seam_attribute.component_count;
        }
    }
    return result;
}

auto remap_lod_vertex_data(
    const Lod_vertex_data&          source,
    const std::span<const uint32_t> forward_remap,
    const std::size_t               output_vertex_count
) -> Lod_vertex_data
{
    ERHE_PROFILE_FUNCTION();

    Lod_vertex_data result{};
    if ((source.vertex_count == 0) || (forward_remap.size() != source.vertex_count)) {
        return result;
    }
    result.vertex_count          = output_vertex_count;
    result.seam_attribute_stride = source.seam_attribute_stride;
    result.positions      .resize(3 * output_vertex_count);
    result.normals        .resize(source.normals.empty()       ? 0 : 3 * output_vertex_count);
    result.colors         .resize(source.colors.empty()        ? 0 : 4 * output_vertex_count);
    result.seam_attributes.resize(source.seam_attribute_stride * output_vertex_count);
    result.joint_indices  .resize(source.joint_indices.empty() ? 0 : 4 * output_vertex_count);
    result.joint_weights  .resize(source.joint_weights.empty() ? 0 : 4 * output_vertex_count);

    const auto copy_run = [](const std::vector<float>& from, std::vector<float>& to, const std::size_t stride, const std::size_t from_vertex, const std::size_t to_vertex) {
        if (from.empty() || (stride == 0)) {
            return;
        }
        std::copy(
            from.begin() + static_cast<std::ptrdiff_t>(stride * from_vertex),
            from.begin() + static_cast<std::ptrdiff_t>(stride * (from_vertex + 1)),
            to.begin()   + static_cast<std::ptrdiff_t>(stride * to_vertex)
        );
    };

    std::vector<bool> written(output_vertex_count, false);
    for (std::size_t vertex = 0; vertex < source.vertex_count; ++vertex) {
        const uint32_t output_vertex = forward_remap[vertex];
        if ((output_vertex == no_vertex) || (output_vertex >= output_vertex_count) || written[output_vertex]) {
            continue;
        }
        written[output_vertex] = true;
        copy_run(source.positions,       result.positions,       3,                            vertex, output_vertex);
        copy_run(source.normals,         result.normals,         3,                            vertex, output_vertex);
        copy_run(source.colors,          result.colors,          4,                            vertex, output_vertex);
        copy_run(source.seam_attributes, result.seam_attributes, source.seam_attribute_stride, vertex, output_vertex);
        copy_run(source.joint_indices,   result.joint_indices,   4,                            vertex, output_vertex);
        copy_run(source.joint_weights,   result.joint_weights,   4,                            vertex, output_vertex);
    }
    return result;
}

auto generate_lod_chain(
    const Lod_vertex_input&         vertices,
    const std::span<const uint32_t> indices,
    const Lod_settings&             settings
) -> Lod_chain_indices
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    Lod_chain_indices result{};
    const std::size_t vertex_count = vertices.vertex_count;
    const std::size_t level_count  = std::clamp<std::size_t>(settings.level_count, 1, max_lod_levels);
    if (
        (vertex_count == 0) ||
        (indices.size() < 3) ||
        ((indices.size() % 3) != 0) ||
        (vertices.positions.size() != 3 * vertex_count) ||
        (!vertices.normals.empty() && (vertices.normals.size() != 3 * vertex_count)) ||
        (!vertices.colors.empty()  && (vertices.colors.size()  != 4 * vertex_count)) ||
        (vertices.seam_attributes.size() != vertices.seam_attribute_stride * vertex_count) ||
        (!vertices.joint_indices.empty() && (vertices.joint_indices.size() != 4 * vertex_count)) ||
        (!vertices.joint_weights.empty() && (vertices.joint_weights.size() != 4 * vertex_count)) ||
        (level_count < 2)
    ) {
        return result;
    }
    for (const uint32_t index : indices) {
        if (index >= vertex_count) {
            return result;
        }
    }
    const bool skinned = !vertices.joint_indices.empty();

    // 1. The simplification-only vertex set. Vertices that share their position
    //    and every seam attribute (and joints) bitwise, and whose normals lie
    //    within normal_merge_angle, become one simplification vertex. Only
    //    referenced vertices take part: an unreferenced duplicate would be a
    //    wedge with no edges to the simplifier.
    std::vector<bool> referenced(vertex_count, false);
    for (const uint32_t index : indices) {
        referenced[index] = true;
    }
    const float cos_merge_angle = std::cos(glm::radians(std::clamp(settings.normal_merge_angle, 0.0f, 180.0f)));
    std::vector<uint32_t> weld_of_vertex(vertex_count, no_vertex); // vertex -> simplification vertex
    std::vector<uint32_t> weld_vertices;                             // simplification vertex -> representative vertex
    {
        std::unordered_map<std::vector<float>, std::vector<uint32_t>, Float_run_hash, Float_run_equal> buckets;
        std::vector<float> key;
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            if (!referenced[vertex]) {
                continue;
            }
            key.clear();
            key.insert(key.end(), vertices.positions.begin() + static_cast<std::ptrdiff_t>(3 * vertex), vertices.positions.begin() + static_cast<std::ptrdiff_t>(3 * (vertex + 1)));
            const std::size_t seam_stride = vertices.seam_attribute_stride;
            key.insert(
                key.end(),
                vertices.seam_attributes.begin() + static_cast<std::ptrdiff_t>(seam_stride * vertex),
                vertices.seam_attributes.begin() + static_cast<std::ptrdiff_t>(seam_stride * (vertex + 1))
            );
            if (skinned) {
                key.insert(key.end(), vertices.joint_indices.begin() + static_cast<std::ptrdiff_t>(4 * vertex), vertices.joint_indices.begin() + static_cast<std::ptrdiff_t>(4 * (vertex + 1)));
                if (!vertices.joint_weights.empty()) {
                    key.insert(key.end(), vertices.joint_weights.begin() + static_cast<std::ptrdiff_t>(4 * vertex), vertices.joint_weights.begin() + static_cast<std::ptrdiff_t>(4 * (vertex + 1)));
                }
            }
            std::vector<uint32_t>& bucket = buckets[key];
            uint32_t weld = no_vertex;
            for (const uint32_t candidate : bucket) {
                if (vertices.normals.empty()) {
                    weld = candidate;
                    break;
                }
                const glm::vec3 a = safe_normalize(get_vec3(vertices.normals, vertex));
                const glm::vec3 b = safe_normalize(get_vec3(vertices.normals, weld_vertices[candidate]));
                if (glm::dot(a, b) >= cos_merge_angle) {
                    weld = candidate;
                    break;
                }
            }
            if (weld == no_vertex) {
                weld = static_cast<uint32_t>(weld_vertices.size());
                weld_vertices.push_back(static_cast<uint32_t>(vertex));
                bucket.push_back(weld);
            }
            weld_of_vertex[vertex] = weld;
        }
    }
    const std::size_t weld_count = weld_vertices.size();

    // Members of each simplification vertex (CSR), for the mapping back.
    std::vector<uint32_t> member_offsets(weld_count + 1, 0);
    std::vector<uint32_t> members(vertex_count, 0);
    {
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            if (weld_of_vertex[vertex] != no_vertex) {
                ++member_offsets[weld_of_vertex[vertex] + 1];
            }
        }
        for (std::size_t weld = 0; weld < weld_count; ++weld) {
            member_offsets[weld + 1] += member_offsets[weld];
        }
        std::vector<uint32_t> cursor(member_offsets.begin(), member_offsets.end() - 1);
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            const uint32_t weld = weld_of_vertex[vertex];
            if (weld != no_vertex) {
                members[cursor[weld]] = static_cast<uint32_t>(vertex);
                ++cursor[weld];
            }
        }
    }

    // 2. Simplification vertex streams: positions, and the weighted metric
    //    attributes of the representative.
    std::vector<float> weld_positions(3 * weld_count);
    for (std::size_t weld = 0; weld < weld_count; ++weld) {
        const std::size_t vertex = weld_vertices[weld];
        for (std::size_t i = 0; i < 3; ++i) {
            weld_positions[(3 * weld) + i] = vertices.positions[(3 * vertex) + i];
        }
    }
    const bool  use_attributes  = (settings.attributes == Lod_attributes::normals_and_colors);
    const bool  use_normals     = use_attributes && !vertices.normals.empty();
    const bool  use_colors      = use_attributes && !vertices.colors.empty();
    const std::size_t attribute_count = (use_normals ? 3 : 0) + (use_colors ? 4 : 0);
    std::vector<float> weld_attributes(attribute_count * weld_count);
    std::vector<float> attribute_weights;
    if (use_normals) {
        attribute_weights.insert(attribute_weights.end(), 3, settings.normal_weight);
    }
    if (use_colors) {
        attribute_weights.insert(attribute_weights.end(), 4, settings.color_weight);
    }
    for (std::size_t weld = 0; weld < weld_count; ++weld) {
        const std::size_t vertex = weld_vertices[weld];
        std::size_t offset = attribute_count * weld;
        if (use_normals) {
            const glm::vec3 n = safe_normalize(get_vec3(vertices.normals, vertex));
            weld_attributes[offset + 0] = n.x;
            weld_attributes[offset + 1] = n.y;
            weld_attributes[offset + 2] = n.z;
            offset += 3;
        }
        if (use_colors) {
            for (std::size_t i = 0; i < 4; ++i) {
                weld_attributes[offset + i] = vertices.colors[(4 * vertex) + i];
            }
        }
    }

    // 3. Welded triangles.
    std::vector<uint32_t> current(indices.size());
    for (std::size_t i = 0, end = indices.size(); i < end; ++i) {
        current[i] = weld_of_vertex[indices[i]];
    }

    // 4. Skinned: lock every simplification vertex with a neighbour of a
    //    different joint set, so a collapse never moves a vertex onto one with
    //    other influences. meshoptimizer locks every wedge of a locked vertex.
    std::vector<unsigned char> vertex_lock;
    if (skinned) {
        std::vector<std::array<uint32_t, 4>> joint_keys(weld_count);
        for (std::size_t weld = 0; weld < weld_count; ++weld) {
            joint_keys[weld] = get_joint_key(vertices, weld_vertices[weld]);
        }
        vertex_lock.assign(weld_count, 0);
        for (std::size_t triangle = 0, end = current.size() / 3; triangle < end; ++triangle) {
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const uint32_t a = current[(3 * triangle) + corner];
                const uint32_t b = current[(3 * triangle) + ((corner + 1) % 3)];
                if (joint_keys[a] != joint_keys[b]) {
                    vertex_lock[a] = meshopt_SimplifyVertex_Lock;
                    vertex_lock[b] = meshopt_SimplifyVertex_Lock;
                }
            }
        }
    }

    unsigned int options = 0;
    if (settings.border == Lod_border::locked) {
        options |= meshopt_SimplifyLockBorder;
    }
    if (settings.seams == Lod_seams::permissive) {
        options |= meshopt_SimplifyPermissive;
    }

    const float scale = meshopt_simplifyScale(weld_positions.data(), weld_count, 3 * sizeof(float));

    // 5. The chain, each level from the previous one.
    std::vector<uint32_t> destination;
    float previous_relative_error = 0.0f;
    for (std::size_t level = 1; level < level_count; ++level) {
        const std::size_t input_index_count = current.size();
        if (input_index_count <= min_index_count) {
            break;
        }
        const std::size_t input_triangle_count = input_index_count / 3;
        const std::size_t target_index_count   = 3 * static_cast<std::size_t>(static_cast<float>(input_triangle_count) * std::clamp(settings.ratio, 0.0f, 1.0f));
        destination.resize(input_index_count);
        float step_error = 0.0f;
        std::size_t result_index_count = meshopt_simplifyWithAttributes(
            destination.data(),
            current.data(),
            input_index_count,
            weld_positions.data(),
            weld_count,
            3 * sizeof(float),
            weld_attributes.empty() ? nullptr : weld_attributes.data(),
            attribute_count * sizeof(float),
            attribute_weights.empty() ? nullptr : attribute_weights.data(),
            attribute_count,
            vertex_lock.empty() ? nullptr : vertex_lock.data(),
            target_index_count,
            std::numeric_limits<float>::max(),
            options,
            &step_error
        );
        if ((settings.fallback == Lod_fallback::sloppy) && (result_index_count > target_index_count)) {
            std::vector<uint32_t> sloppy(input_index_count);
            float sloppy_error = 0.0f;
            const std::size_t sloppy_index_count = meshopt_simplifySloppy(
                sloppy.data(),
                current.data(),
                input_index_count,
                weld_positions.data(),
                weld_count,
                3 * sizeof(float),
                vertex_lock.empty() ? nullptr : vertex_lock.data(),
                target_index_count,
                1.0f,
                &sloppy_error
            );
            if ((sloppy_index_count > 0) && (sloppy_index_count < result_index_count)) {
                destination        = std::move(sloppy);
                result_index_count = sloppy_index_count;
                step_error         = sloppy_error;
            }
        }
        if (
            (result_index_count == 0) ||
            (static_cast<float>(result_index_count) >= (min_reduction_ratio * static_cast<float>(input_index_count)))
        ) {
            break;
        }
        destination.resize(result_index_count);

        const float relative_error = std::max({previous_relative_error * error_growth, step_error, min_relative_error});

        // Map back to the real vertices: per triangle corner, the member of
        // the simplification vertex whose normal is closest to the triangle's
        // own normal (members differ in their normals only).
        const std::size_t first_index = result.indices.size();
        for (std::size_t triangle = 0, end = result_index_count / 3; triangle < end; ++triangle) {
            const uint32_t  w0 = destination[(3 * triangle) + 0];
            const uint32_t  w1 = destination[(3 * triangle) + 1];
            const uint32_t  w2 = destination[(3 * triangle) + 2];
            const glm::vec3 p0 = get_vec3(weld_positions, w0);
            const glm::vec3 p1 = get_vec3(weld_positions, w1);
            const glm::vec3 p2 = get_vec3(weld_positions, w2);
            const glm::vec3 face_normal = safe_normalize(glm::cross(p1 - p0, p2 - p0));
            for (const uint32_t weld : {w0, w1, w2}) {
                uint32_t best_vertex = weld_vertices[weld];
                if (!vertices.normals.empty()) {
                    float best_dot = -std::numeric_limits<float>::max();
                    for (uint32_t m = member_offsets[weld], m_end = member_offsets[weld + 1]; m < m_end; ++m) {
                        const uint32_t member = members[m];
                        const float    d      = glm::dot(safe_normalize(get_vec3(vertices.normals, member)), face_normal);
                        if (d > best_dot) {
                            best_dot    = d;
                            best_vertex = member;
                        }
                    }
                }
                result.indices.push_back(best_vertex);
            }
        }
        result.levels.push_back(
            Lod_chain_indices::Level{
                .first_index = first_index,
                .index_count = result_index_count,
                .error       = relative_error * scale
            }
        );
        previous_relative_error = relative_error;
        std::swap(current, destination);
    }

    const std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<float>(end_time - start_time).count();
    return result;
}

auto make_lod_chain(const Index_range& level_0, const Lod_chain_indices& lod_indices) -> Lod_chain
{
    Lod_chain chain{};
    if (lod_indices.is_empty()) {
        return chain;
    }
    ERHE_VERIFY(lod_indices.levels.size() < max_lod_levels);
    chain.levels[0]          = Lod_level{.index_range = level_0, .error = 0.0f};
    chain.level_count        = 1;
    chain.generation_seconds = lod_indices.elapsed_seconds;
    const std::size_t lod_first_index = level_0.first_index + level_0.index_count;
    for (const Lod_chain_indices::Level& level : lod_indices.levels) {
        chain.levels[chain.level_count] = Lod_level{
            .index_range = Index_range{
                .primitive_type = Primitive_type::triangles,
                .first_index    = lod_first_index + level.first_index,
                .index_count    = level.index_count
            },
            .error = level.error
        };
        ++chain.level_count;
    }
    return chain;
}

auto get_lod_totals() -> Lod_totals
{
    const std::lock_guard<std::mutex> lock{s_totals_mutex};
    return s_totals;
}

void reset_lod_totals()
{
    const std::lock_guard<std::mutex> lock{s_totals_mutex};
    s_totals = Lod_totals{};
}

void log_lod_chain(const std::string_view name, const Lod_chain& chain)
{
    if (chain.is_empty()) {
        return;
    }
    std::string levels;
    std::size_t lod_index_count = 0;
    for (std::size_t level = 0; level < chain.level_count; ++level) {
        const Lod_level& lod_level = chain.levels[level];
        if (level > 0) {
            levels += ", ";
            lod_index_count += lod_level.index_range.index_count;
        }
        levels += fmt::format("{}: {} tris err {:.3g}", level, lod_level.index_range.get_triangle_count(), lod_level.error);
    }
    log_primitive->info(
        "mesh lod {}: {} levels [{}] in {:.1f} ms",
        name, chain.level_count, levels, 1000.0f * chain.generation_seconds
    );
    const std::lock_guard<std::mutex> lock{s_totals_mutex};
    ++s_totals.primitive_count;
    s_totals.level_count     += chain.level_count - 1;
    s_totals.index_count     += lod_index_count;
    s_totals.elapsed_seconds += chain.generation_seconds;
}

} // namespace erhe::primitive
