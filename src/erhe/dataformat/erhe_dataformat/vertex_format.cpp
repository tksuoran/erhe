#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_hash/hash.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>

namespace erhe::dataformat {

auto c_str(const Vertex_attribute_usage usage) -> const char*
{
    switch (usage) {
        //using enum Usage_type;
        case Vertex_attribute_usage::none:          return "none";
        case Vertex_attribute_usage::position:      return "position";
        case Vertex_attribute_usage::tangent:       return "tangent";
        case Vertex_attribute_usage::bitangent:     return "bitangent";
        case Vertex_attribute_usage::normal:        return "normal";
        case Vertex_attribute_usage::color:         return "color";
        case Vertex_attribute_usage::joint_weights: return "joint_weights";
        case Vertex_attribute_usage::joint_indices: return "joint_indices";
        case Vertex_attribute_usage::tex_coord:     return "tex_coord";
        case Vertex_attribute_usage::custom:        return "custom";
        default:                                    return "?";
    }
}

auto c_str(Vertex_step step) -> const char*
{
    switch (step) {
        //using enum Vertex_step;
        case Vertex_step::Step_per_vertex:   return "per_vertex";
        case Vertex_step::Step_per_instance: return "per_instance";
        default:                             return "?";
    }
}

Vertex_stream::Vertex_stream(std::size_t in_binding)
    : binding{in_binding}
{
}

Vertex_stream::Vertex_stream(std::size_t in_binding, std::initializer_list<Vertex_attribute> in_attributes)
    : binding{in_binding}
{
    for (auto& in_attribute : in_attributes) {
        const std::size_t alignment = get_component_byte_size(in_attribute.format);
        if (alignment > 1) {
            stride = ((stride + alignment - 1) / alignment) * alignment;
        }
        if (alignment > max_alignment) {
            max_alignment = alignment;
        }
        attributes.push_back(in_attribute);
        attributes.back().offset = stride;
        stride += get_format_size_bytes(in_attribute.format);
    }
    // Pad stride so that the next vertex's first attribute is aligned
    if (max_alignment > 1) {
        stride = ((stride + max_alignment - 1) / max_alignment) * max_alignment;
    }
}

auto Vertex_stream::find_attribute(Vertex_attribute_usage usage_type, std::size_t index) const -> const Vertex_attribute*
{
    for (const auto& i : attributes) {
        if ((i.usage_type == usage_type) && (i.usage_index == index)) {
            return &(i);
        }
    }

    return nullptr;
}

auto Vertex_stream::emplace_back(erhe::dataformat::Format format, Vertex_attribute_usage usage_type, std::size_t usage_index) -> Vertex_attribute&
{
    const std::size_t alignment = get_component_byte_size(format);
    if (alignment > 1) {
        stride = ((stride + alignment - 1) / alignment) * alignment;
    }
    if (alignment > max_alignment) {
        max_alignment = alignment;
    }
    Vertex_attribute& result = attributes.emplace_back(format, usage_type, usage_index, stride);
    stride += get_format_size_bytes(format);
    return result;
}

void Vertex_stream::finalize_stride()
{
    if (max_alignment > 1) {
        stride = ((stride + max_alignment - 1) / max_alignment) * max_alignment;
    }
}

void Vertex_stream::repack(const Vertex_stream_packing& packing)
{
    stride        = 0;
    max_alignment = 1;
    for (Vertex_attribute& attribute : attributes) {
        // lcm, not max: an alignment that satisfies both constraints. For the
        // powers of two every real backend uses this is just the larger of the
        // two, but max() would silently satisfy neither for anything else.
        const std::size_t alignment = std::lcm(
            get_component_byte_size(attribute.format),
            packing.min_attribute_alignment
        );
        if (alignment > 1) {
            stride = ((stride + alignment - 1) / alignment) * alignment;
        }
        if (alignment > max_alignment) {
            max_alignment = alignment;
        }
        attribute.offset = stride;
        stride += get_format_size_bytes(attribute.format);
    }
    // Pad so that the next vertex's first attribute is aligned, and to whatever
    // stride granularity the backend requires on top of that.
    const std::size_t stride_alignment = std::lcm(max_alignment, packing.min_stride_alignment);
    if (stride_alignment > 1) {
        stride = ((stride + stride_alignment - 1) / stride_alignment) * stride_alignment;
    }
}

auto Vertex_stream::is_buffer_compatible(const Vertex_stream& other) const -> bool
{
    if (stride != other.stride) {
        return false;
    }
    if (attributes.size() != other.attributes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const auto& attr = attributes[i];
        const auto& other_attr = other.attributes[i];
        if (attr.format != other_attr.format || attr.offset != other_attr.offset) {
            return false;
        }
    }
    return true;
}

auto Vertex_stream::get_hash() const -> uint64_t
{
    uint64_t result;
    {
        const uint64_t stream_packed =
             static_cast<uint64_t>(binding)       |
            (static_cast<uint64_t>(step   ) << 7) |
            (static_cast<uint64_t>(stride ) << 8);
        result = erhe::hash::hash(stream_packed);
    }
    result = erhe::hash::hash(static_cast<uint64_t>(attributes.size()), result);
    for (const Vertex_attribute& attribute : attributes) {
        const uint64_t attribute_packed =
             static_cast<uint64_t>(attribute.format     )        |
            (static_cast<uint64_t>(attribute.usage_type ) <<  8) |
            (static_cast<uint64_t>(attribute.usage_index) << 16) |
            (static_cast<uint64_t>(attribute.offset     ) << 24);
        result = erhe::hash::hash(attribute_packed, result);
    }
    return result;
}

auto Vertex_attribute::to_string() const -> std::string
{
    std::stringstream ss;
    ss << "format: "      << c_str(format)     << ", ";
    ss << "usage_type: "  << c_str(usage_type) << ", ";
    ss << "usage_index: " << usage_index       << ", ";
    ss << "offset: "      << offset;
    return ss.str();
}

auto Vertex_stream::to_string() const -> std::string
{
    std::stringstream ss;
    ss << "binding: " << binding     << ", ";
    ss << "stride: "  << stride      << ", ";
    ss << "step: "    << c_str(step) << ", ";
    ss << "attributes: {";
    bool first = true;
    for (const auto& attr : attributes) {
        if (!first) {
            ss << ", ";
        }
        ss << attr.to_string();
        first = false;
    }
    ss << "}";
    return ss.str();
}

Vertex_format::Vertex_format()
{
}

Vertex_format::Vertex_format(const std::initializer_list<Vertex_stream> streams)
    : streams{streams}
{
}

auto Vertex_format::get_stream(std::size_t binding) const -> const Vertex_stream*
{
    for (const auto& stream : streams) {
        if (stream.binding == binding) {
            return &stream;
        }
    }
    return nullptr;
}

auto Vertex_format::find_attribute(Vertex_attribute_usage usage_type, std::size_t index) const -> Attribute_stream
{
    Attribute_stream result{};
    for (const auto& stream : streams) {
        result.attribute = stream.find_attribute(usage_type, index);
        if (result.attribute != nullptr) {
            result.stream = &stream;
            return result;
        }
    }
    return result;
}

auto Vertex_format::get_attributes() const -> std::vector<Attribute_stream>
{
    std::vector<Attribute_stream> result{};
    for (const Vertex_stream& stream : streams) {
        for (const Vertex_attribute& attribute : stream.attributes) {
            result.emplace_back(&attribute, &stream);
        }
    }
    return result;
}

auto Vertex_format::get_hash() const -> uint64_t
{
    uint64_t result = 0;
    for (const Vertex_stream& stream : streams) {
        const uint64_t stream_hash = stream.get_hash();
        result = erhe::hash::hash(stream_hash, result);
    }
    return result;
}

void Vertex_format::repack(const Vertex_stream_packing& packing)
{
    for (Vertex_stream& stream : streams) {
        stream.repack(packing);
    }
}

auto Vertex_format::to_string() const -> std::string
{
    std::stringstream ss;
    ss << "streams: {";
    bool first = true;
    for (const auto& stream : streams) {
        if (!first) {
            ss << ", ";
        }
        ss << stream.to_string();
        first = false;
    }
    ss << "}";
    return ss.str();
}

auto c_str(const Vertex_position_encoding encoding) -> const char*
{
    switch (encoding) {
        case Vertex_position_encoding::passthrough:    return "passthrough";
        case Vertex_position_encoding::snorm16x3_aabb: return "snorm16x3_aabb";
        default:                                       return "?";
    }
}

auto get_vertex_position_encoding(const Vertex_format* vertex_format) -> Vertex_position_encoding
{
    if (vertex_format == nullptr) {
        return Vertex_position_encoding::passthrough;
    }
    const Attribute_stream position = vertex_format->find_attribute(Vertex_attribute_usage::position, 0);
    if (position.attribute == nullptr) {
        return Vertex_position_encoding::passthrough;
    }
    switch (position.attribute->format) {
        case Format::format_16_vec3_snorm: return Vertex_position_encoding::snorm16x3_aabb;
        default:                           return Vertex_position_encoding::passthrough;
    }
}

auto c_str(const Vertex_texcoord_encoding encoding) -> const char*
{
    switch (encoding) {
        case Vertex_texcoord_encoding::passthrough:      return "passthrough";
        case Vertex_texcoord_encoding::unorm16x2_affine: return "unorm16x2_affine";
        default:                                         return "?";
    }
}

auto get_vertex_texcoord_encoding(const Vertex_format* vertex_format) -> Vertex_texcoord_encoding
{
    if (vertex_format == nullptr) {
        return Vertex_texcoord_encoding::passthrough;
    }
    // Channel 0 decides for channels 0 and 1 together: both are substituted as a
    // pair, and channel 1 may be absent from a format that has channel 0.
    // Channel 2 is excluded on purpose - see the enum comment.
    const Attribute_stream texcoord = vertex_format->find_attribute(Vertex_attribute_usage::tex_coord, 0);
    if (texcoord.attribute == nullptr) {
        return Vertex_texcoord_encoding::passthrough;
    }
    switch (texcoord.attribute->format) {
        case Format::format_16_vec2_unorm: return Vertex_texcoord_encoding::unorm16x2_affine;
        default:                           return Vertex_texcoord_encoding::passthrough;
    }
}

auto c_str(const Vertex_tbn_encoding encoding) -> const char*
{
    switch (encoding) {
        case Vertex_tbn_encoding::passthrough:  return "passthrough";
        case Vertex_tbn_encoding::quaternion16: return "quaternion16";
        default:                                return "?";
    }
}

auto get_vertex_tbn_encoding(const Vertex_format* vertex_format) -> Vertex_tbn_encoding
{
    if (vertex_format == nullptr) {
        return Vertex_tbn_encoding::passthrough;
    }
    const Attribute_stream tangent = vertex_format->find_attribute(Vertex_attribute_usage::tangent, 0);
    if (tangent.attribute == nullptr) {
        return Vertex_tbn_encoding::passthrough;
    }
    switch (tangent.attribute->format) {
        case Format::format_16_vec4_sint: return Vertex_tbn_encoding::quaternion16;
        default:                          return Vertex_tbn_encoding::passthrough;
    }
}

auto c_str(const Vertex_joint_weights_encoding encoding) -> const char*
{
    switch (encoding) {
        case Vertex_joint_weights_encoding::passthrough:            return "passthrough";
        case Vertex_joint_weights_encoding::unorm16x3_implicit_sum: return "unorm16x3_implicit_sum";
        default:                                                    return "?";
    }
}

auto get_vertex_joint_weights_encoding(const Vertex_format* vertex_format) -> Vertex_joint_weights_encoding
{
    if (vertex_format == nullptr) {
        return Vertex_joint_weights_encoding::passthrough;
    }
    const Attribute_stream joint_weights = vertex_format->find_attribute(Vertex_attribute_usage::joint_weights, 0);
    if (joint_weights.attribute == nullptr) {
        return Vertex_joint_weights_encoding::passthrough;
    }
    switch (joint_weights.attribute->format) {
        case Format::format_16_vec3_unorm: return Vertex_joint_weights_encoding::unorm16x3_implicit_sum;
        default:                           return Vertex_joint_weights_encoding::passthrough;
    }
}

auto get_vertex_format_signature(const Vertex_format& vertex_format) -> std::string
{
    std::string signature;
    for (std::size_t stream_index = 0, stream_end = vertex_format.streams.size(); stream_index < stream_end; ++stream_index) {
        const Vertex_stream& stream = vertex_format.streams[stream_index];
        if (stream_index > 0) {
            signature += '|';
        }
        signature += 's';
        signature += std::to_string(stream_index);
        signature += '=';
        signature += std::to_string(stream.stride);
        signature += ':';
        for (std::size_t attribute_index = 0, attribute_end = stream.attributes.size(); attribute_index < attribute_end; ++attribute_index) {
            const Vertex_attribute& attribute = stream.attributes[attribute_index];
            if (attribute_index > 0) {
                signature += ',';
            }
            std::string_view format_name{c_str(attribute.format)};
            constexpr std::string_view prefix{"format_"};
            if (format_name.starts_with(prefix)) {
                format_name.remove_prefix(prefix.size());
            }
            signature += c_str(attribute.usage_type);
            signature += '.';
            signature += std::to_string(attribute.usage_index);
            signature += '=';
            signature += format_name;
            signature += '@';
            signature += std::to_string(attribute.offset);
        }
    }
    return signature;
}

} // namespace erhe::dataformat
