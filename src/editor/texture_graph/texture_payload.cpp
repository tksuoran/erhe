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

auto pin_key_to_value_type(const std::size_t key) -> erhe::texgen::Value_type
{
    switch (key) {
        case Texture_pin_key::grayscale: return erhe::texgen::Value_type::grayscale;
        case Texture_pin_key::rgb:       return erhe::texgen::Value_type::rgb;
        case Texture_pin_key::rgba:      return erhe::texgen::Value_type::rgba;
        default:                         return erhe::texgen::Value_type::rgba;
    }
}

auto Texture_payload::has_value() const -> bool
{
    return source_node != nullptr;
}

} // namespace editor
