#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace erhe::property { class Enum_info; }

// The authoring properties a source texture's conversion reads
// (doc/plans/prepared_assets.md R4.2, R4.3; doc/erhe/graphics.md "Texture properties").
namespace erhe::graphics {

enum class Texture_transfer_function : unsigned int
{
    srgb   = 0,
    linear = 1
};

enum class Texture_normal_encoding : unsigned int
{
    none            = 0, // not a normal map
    three_component = 1, // X, Y and Z stored
    two_component   = 2  // X and Y stored, Z reconstructed
};

class Texture_properties
{
public:
    Texture_transfer_function transfer_function{Texture_transfer_function::srgb};
    Texture_normal_encoding   normal_encoding  {Texture_normal_encoding::none};
    bool                      generate_mipmaps {true};
    Sampler_address_mode      wrap_u           {Sampler_address_mode::repeat};
    Sampler_address_mode      wrap_v           {Sampler_address_mode::repeat};
};

[[nodiscard]] auto operator==(const Texture_properties& lhs, const Texture_properties& rhs) -> bool;

// The material slot a texture is used in; the slot decides the transfer
// function and whether the image is a normal map.
enum class Texture_use_slot : unsigned int
{
    base_color = 0,
    emissive,
    metallic_roughness,
    occlusion,
    normal,
    other_color, // a color slot of another material model (diffuse)
    other_data   // any other data slot
};

class Texture_use
{
public:
    Texture_use_slot     slot  {Texture_use_slot::base_color};
    Sampler_address_mode wrap_u{Sampler_address_mode::repeat};
    Sampler_address_mode wrap_v{Sampler_address_mode::repeat};
};

class Texture_property_derivation
{
public:
    Texture_properties         properties;
    std::size_t                conflicting_use_count{0}; // later uses that would derive other properties
    std::optional<std::size_t> first_conflict;           // index of the first such use
};

// R4.3: the properties of a texture that states none, from the first use;
// three_component for a normal map (the image's own two component marker
// refines it when the image is opened). No uses: the defaults.
[[nodiscard]] auto derive_texture_properties(std::span<const Texture_use> uses) -> Texture_property_derivation;

[[nodiscard]] auto c_str(Texture_transfer_function value) -> const char*; // "srgb", "linear"
[[nodiscard]] auto c_str(Texture_normal_encoding value) -> const char*;   // "none", "three_component", "two_component"
[[nodiscard]] auto c_str(Texture_use_slot value) -> const char*;
[[nodiscard]] auto c_str_wrap(Sampler_address_mode value) -> const char*; // "repeat", "clamp_to_edge", "mirrored_repeat"
[[nodiscard]] auto parse_texture_transfer_function(std::string_view text) -> std::optional<Texture_transfer_function>;
[[nodiscard]] auto parse_texture_normal_encoding  (std::string_view text) -> std::optional<Texture_normal_encoding>;
[[nodiscard]] auto parse_wrap_mode                (std::string_view text) -> std::optional<Sampler_address_mode>;

extern const erhe::property::Enum_info c_texture_transfer_function_enum_info;
extern const erhe::property::Enum_info c_texture_normal_encoding_enum_info;
extern const erhe::property::Enum_info c_texture_wrap_enum_info; // Sampler_address_mode labels of the wrap properties

} // namespace erhe::graphics
