// derive_texture_properties() (R4.3, doc/erhe/graphics.md "Texture properties").

#include "erhe_graphics/texture_properties.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using erhe::graphics::Sampler_address_mode;
using erhe::graphics::Texture_normal_encoding;
using erhe::graphics::Texture_transfer_function;
using erhe::graphics::Texture_use;
using erhe::graphics::Texture_use_slot;

TEST(Texture_properties, no_use_gives_the_defaults)
{
    const erhe::graphics::Texture_property_derivation derivation = erhe::graphics::derive_texture_properties({});
    EXPECT_TRUE(derivation.properties == erhe::graphics::Texture_properties{});
    EXPECT_EQ(derivation.conflicting_use_count, 0u);
    EXPECT_FALSE(derivation.first_conflict.has_value());
}

TEST(Texture_properties, slots_decide_transfer_function_and_normal_encoding)
{
    class Case { public: Texture_use_slot slot; Texture_transfer_function transfer; Texture_normal_encoding normal; };
    const Case cases[] = {
        {Texture_use_slot::base_color,         Texture_transfer_function::srgb,   Texture_normal_encoding::none},
        {Texture_use_slot::emissive,           Texture_transfer_function::srgb,   Texture_normal_encoding::none},
        {Texture_use_slot::other_color,        Texture_transfer_function::srgb,   Texture_normal_encoding::none},
        {Texture_use_slot::metallic_roughness, Texture_transfer_function::linear, Texture_normal_encoding::none},
        {Texture_use_slot::occlusion,          Texture_transfer_function::linear, Texture_normal_encoding::none},
        {Texture_use_slot::other_data,         Texture_transfer_function::linear, Texture_normal_encoding::none},
        {Texture_use_slot::normal,             Texture_transfer_function::linear, Texture_normal_encoding::three_component},
    };
    for (const Case& c : cases) {
        const std::vector<Texture_use> uses{Texture_use{.slot = c.slot}};
        const erhe::graphics::Texture_property_derivation derivation = erhe::graphics::derive_texture_properties(uses);
        EXPECT_EQ(derivation.properties.transfer_function, c.transfer) << erhe::graphics::c_str(c.slot);
        EXPECT_EQ(derivation.properties.normal_encoding,   c.normal)   << erhe::graphics::c_str(c.slot);
        EXPECT_TRUE(derivation.properties.generate_mipmaps);
    }
}

TEST(Texture_properties, first_use_wins_and_conflicts_are_counted)
{
    const std::vector<Texture_use> uses{
        Texture_use{.slot = Texture_use_slot::normal,     .wrap_u = Sampler_address_mode::clamp_to_edge, .wrap_v = Sampler_address_mode::mirrored_repeat},
        Texture_use{.slot = Texture_use_slot::occlusion}, // linear, but not a normal map
        Texture_use{.slot = Texture_use_slot::normal},    // agrees
        Texture_use{.slot = Texture_use_slot::base_color} // sRGB
    };
    const erhe::graphics::Texture_property_derivation derivation = erhe::graphics::derive_texture_properties(uses);
    EXPECT_EQ(derivation.properties.transfer_function, Texture_transfer_function::linear);
    EXPECT_EQ(derivation.properties.normal_encoding,   Texture_normal_encoding::three_component);
    EXPECT_EQ(derivation.properties.wrap_u, Sampler_address_mode::clamp_to_edge);
    EXPECT_EQ(derivation.properties.wrap_v, Sampler_address_mode::mirrored_repeat);
    EXPECT_EQ(derivation.conflicting_use_count, 2u);
    ASSERT_TRUE(derivation.first_conflict.has_value());
    EXPECT_EQ(derivation.first_conflict.value(), 1u);
}

TEST(Texture_properties, text_forms_round_trip)
{
    for (const Texture_transfer_function value : {Texture_transfer_function::srgb, Texture_transfer_function::linear}) {
        EXPECT_EQ(erhe::graphics::parse_texture_transfer_function(erhe::graphics::c_str(value)), value);
    }
    for (const Texture_normal_encoding value : {Texture_normal_encoding::none, Texture_normal_encoding::three_component, Texture_normal_encoding::two_component}) {
        EXPECT_EQ(erhe::graphics::parse_texture_normal_encoding(erhe::graphics::c_str(value)), value);
    }
    for (const Sampler_address_mode value : {Sampler_address_mode::repeat, Sampler_address_mode::clamp_to_edge, Sampler_address_mode::mirrored_repeat}) {
        EXPECT_EQ(erhe::graphics::parse_wrap_mode(erhe::graphics::c_str_wrap(value)), value);
    }
    EXPECT_FALSE(erhe::graphics::parse_wrap_mode("clamp").has_value());
}

} // anonymous namespace
