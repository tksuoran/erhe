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
        attributes.push_back(in_attribute);
        attributes.back().offset = stride;
        stride += get_format_size(in_attribute.format);
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
    Vertex_attribute& result = attributes.emplace_back(format, usage_type, usage_index, stride);
    stride += get_format_size(format);
    return result;
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

} // namespace erhe::dataformat
