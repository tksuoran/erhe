#include "erhe_graphics/texture_properties.hpp"

#include "erhe_property/enum_info.hpp"

namespace erhe::graphics {

auto operator==(const Texture_properties& lhs, const Texture_properties& rhs) -> bool
{
    return
        (lhs.transfer_function == rhs.transfer_function) &&
        (lhs.normal_encoding   == rhs.normal_encoding  ) &&
        (lhs.generate_mipmaps  == rhs.generate_mipmaps ) &&
        (lhs.wrap_u            == rhs.wrap_u           ) &&
        (lhs.wrap_v            == rhs.wrap_v           );
}

namespace {

[[nodiscard]] auto is_color_slot(const Texture_use_slot slot) -> bool
{
    return (slot == Texture_use_slot::base_color) || (slot == Texture_use_slot::emissive) || (slot == Texture_use_slot::other_color);
}

[[nodiscard]] auto derive_from_use(const Texture_use& use) -> Texture_properties
{
    return Texture_properties{
        .transfer_function = is_color_slot(use.slot) ? Texture_transfer_function::srgb : Texture_transfer_function::linear,
        .normal_encoding   = (use.slot == Texture_use_slot::normal) ? Texture_normal_encoding::three_component : Texture_normal_encoding::none,
        .generate_mipmaps  = true,
        .wrap_u            = use.wrap_u,
        .wrap_v            = use.wrap_v
    };
}

constexpr erhe::property::Enum_entry c_transfer_function_entries[] = {
    {"sRGB",   static_cast<int32_t>(Texture_transfer_function::srgb)},
    {"Linear", static_cast<int32_t>(Texture_transfer_function::linear)},
};
constexpr erhe::property::Enum_entry c_normal_encoding_entries[] = {
    {"Not A Normal Map", static_cast<int32_t>(Texture_normal_encoding::none)},
    {"Three Component",  static_cast<int32_t>(Texture_normal_encoding::three_component)},
    {"Two Component",    static_cast<int32_t>(Texture_normal_encoding::two_component)},
};
constexpr erhe::property::Enum_entry c_wrap_entries[] = {
    {"Repeat", static_cast<int32_t>(Sampler_address_mode::repeat)},
    {"Clamp",  static_cast<int32_t>(Sampler_address_mode::clamp_to_edge)},
    {"Mirror", static_cast<int32_t>(Sampler_address_mode::mirrored_repeat)},
};

} // anonymous namespace

const erhe::property::Enum_info c_texture_transfer_function_enum_info{"Texture_transfer_function", c_transfer_function_entries};
const erhe::property::Enum_info c_texture_normal_encoding_enum_info  {"Texture_normal_encoding",   c_normal_encoding_entries};
const erhe::property::Enum_info c_texture_wrap_enum_info             {"Texture_wrap",              c_wrap_entries};

auto derive_texture_properties(const std::span<const Texture_use> uses) -> Texture_property_derivation
{
    Texture_property_derivation result{};
    if (uses.empty()) {
        return result;
    }
    result.properties = derive_from_use(uses.front());
    for (std::size_t i = 1, end = uses.size(); i < end; ++i) {
        const Texture_properties other = derive_from_use(uses[i]);
        if (
            (other.transfer_function != result.properties.transfer_function) ||
            (other.normal_encoding   != result.properties.normal_encoding)
        ) {
            ++result.conflicting_use_count;
            if (!result.first_conflict.has_value()) {
                result.first_conflict = i;
            }
        }
    }
    return result;
}

auto c_str(const Texture_transfer_function value) -> const char*
{
    switch (value) {
        case Texture_transfer_function::srgb:   return "srgb";
        case Texture_transfer_function::linear: return "linear";
        default:                                return "?";
    }
}

auto c_str(const Texture_normal_encoding value) -> const char*
{
    switch (value) {
        case Texture_normal_encoding::none:            return "none";
        case Texture_normal_encoding::three_component: return "three_component";
        case Texture_normal_encoding::two_component:   return "two_component";
        default:                                       return "?";
    }
}

auto c_str(const Texture_use_slot value) -> const char*
{
    switch (value) {
        case Texture_use_slot::base_color:         return "base color";
        case Texture_use_slot::emissive:           return "emissive";
        case Texture_use_slot::metallic_roughness: return "metallic roughness";
        case Texture_use_slot::occlusion:          return "occlusion";
        case Texture_use_slot::normal:             return "normal";
        case Texture_use_slot::other_color:        return "color";
        case Texture_use_slot::other_data:         return "data";
        default:                                   return "?";
    }
}

auto c_str_wrap(const Sampler_address_mode value) -> const char*
{
    switch (value) {
        case Sampler_address_mode::repeat:          return "repeat";
        case Sampler_address_mode::clamp_to_edge:   return "clamp_to_edge";
        case Sampler_address_mode::mirrored_repeat: return "mirrored_repeat";
        default:                                    return "?";
    }
}

auto parse_texture_transfer_function(const std::string_view text) -> std::optional<Texture_transfer_function>
{
    for (const Texture_transfer_function value : {Texture_transfer_function::srgb, Texture_transfer_function::linear}) {
        if (text == c_str(value)) {
            return value;
        }
    }
    return std::nullopt;
}

auto parse_texture_normal_encoding(const std::string_view text) -> std::optional<Texture_normal_encoding>
{
    for (const Texture_normal_encoding value : {Texture_normal_encoding::none, Texture_normal_encoding::three_component, Texture_normal_encoding::two_component}) {
        if (text == c_str(value)) {
            return value;
        }
    }
    return std::nullopt;
}

auto parse_wrap_mode(const std::string_view text) -> std::optional<Sampler_address_mode>
{
    for (const Sampler_address_mode value : {Sampler_address_mode::repeat, Sampler_address_mode::clamp_to_edge, Sampler_address_mode::mirrored_repeat}) {
        if (text == c_str_wrap(value)) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace erhe::graphics
