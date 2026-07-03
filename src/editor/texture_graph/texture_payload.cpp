#include "texture_graph/texture_payload.hpp"

namespace editor {

auto value_type_to_pin_key(const erhe::texgen::Value_type type) -> std::size_t
{
    switch (type) {
        case erhe::texgen::Value_type::grayscale: return Texture_pin_key::grayscale;
        case erhe::texgen::Value_type::rgb:       return Texture_pin_key::rgb;
        case erhe::texgen::Value_type::rgba:      return Texture_pin_key::rgba;
        default:                                  return Texture_pin_key::rgba;
    }
}

auto Texture_payload::has_value() const -> bool
{
    return source_node != nullptr;
}

} // namespace editor
