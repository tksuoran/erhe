#include "erhe_dataformat/vertex_format.hpp"

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

namespace {

constexpr auto mix(uint64_t seed, uint64_t value) -> uint64_t
{
    // Standard 64-bit hash mix (Knuth multiplicative; same shape as
    // boost::hash_combine but adapted to 64-bit so streams + format +
    // offset folding has headroom past a uint32 mask).
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 12) + (seed >> 4);
    return seed;
}

} // anonymous namespace

auto compute_vertex_format_key(const Vertex_format& format) -> uint64_t
{
    uint64_t key = 0;
    key = mix(key, format.streams.size());
    for (std::size_t stream_index = 0; stream_index < format.streams.size(); ++stream_index) {
        const Vertex_stream& stream = format.streams[stream_index];
        key = mix(key, stream_index);
        // GPU-visible identity: two formats whose only difference is
        // the stream binding (e.g. position lives on binding 0 vs 1)
        // produce different vertex-input states and must hash apart.
        key = mix(key, stream.binding);
        key = mix(key, stream.stride);
        key = mix(key, static_cast<uint64_t>(stream.step));
        key = mix(key, stream.attributes.size());
        for (const Vertex_attribute& attribute : stream.attributes) {
            key = mix(key, static_cast<uint64_t>(attribute.usage_type));
            key = mix(key, static_cast<uint64_t>(attribute.usage_index));
            key = mix(key, static_cast<uint64_t>(attribute.format));
            key = mix(key, static_cast<uint64_t>(attribute.offset));
        }
    }
    return key;
}

} // namespace erhe::dataformat
