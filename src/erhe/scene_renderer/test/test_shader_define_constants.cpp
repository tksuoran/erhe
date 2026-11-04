// Named define values (doc/erhe/graphics.md "Named define values"): the
// constants Shader_key::get_define_constants() hands to final_source(), and
// the value expression a define is emitted as.

#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_graphics/shader_stages.hpp"
#include "erhe_primitive/enums.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using erhe::graphics::Define_constants_kind;
using erhe::graphics::Shader_define_constants;
using erhe::graphics::get_define_value_expression;
using erhe::scene_renderer::Shader_int;
using erhe::scene_renderer::Shader_key;

auto find(const std::vector<Shader_define_constants>& all, const std::string& define) -> const Shader_define_constants*
{
    for (const Shader_define_constants& entry : all) {
        if (entry.define == define) {
            return &entry;
        }
    }
    return nullptr;
}

TEST(Shader_define_constants, value_expression_names_the_constant)
{
    const Shader_define_constants constants{
        .define    = "AXIS",
        .kind      = Define_constants_kind::value,
        .constants = {{"AXIS_A", 0u}, {"AXIS_B", 2u}}
    };
    EXPECT_EQ(get_define_value_expression(constants, "2"), "AXIS_B");
    EXPECT_EQ(get_define_value_expression(constants, "0"), "AXIS_A");
    EXPECT_EQ(get_define_value_expression(constants, "1"), "");   // no constant: emitted as the number
    EXPECT_EQ(get_define_value_expression(constants, "2u"), "");  // not a plain number
    EXPECT_EQ(get_define_value_expression(constants, ""), "");
}

TEST(Shader_define_constants, bit_mask_expression_ors_the_set_bits)
{
    const Shader_define_constants constants{
        .define    = "MASK",
        .kind      = Define_constants_kind::bit_mask,
        .constants = {{"BIT_0", 1u}, {"BIT_1", 2u}, {"BIT_2", 4u}}
    };
    EXPECT_EQ(get_define_value_expression(constants, "5"), "(BIT_0 | BIT_2)");
    EXPECT_EQ(get_define_value_expression(constants, "2"), "(BIT_1)");
    EXPECT_EQ(get_define_value_expression(constants, "0"), "0");
    EXPECT_EQ(get_define_value_expression(constants, "8"), "");   // a bit no constant names
}

TEST(Shader_define_constants, key_names_every_enum_axis_value)
{
    Shader_key key;
    key.set(Shader_int::BXDF_MODEL,              static_cast<uint32_t>(erhe::primitive::Bxdf_model::anisotropic_brdf));
    key.set(Shader_int::NORMAL_TEXTURE_ENCODING, erhe::primitive::to_uint32(erhe::primitive::Normalmap_encoding::left_handed_two_channel_rg));
    key.set(Shader_int::BASE_COLOR_TEXGEN_MODE,  erhe::primitive::to_uint32(erhe::primitive::Texgen_mode::tangent));
    key.set(Shader_int::SHADOW_FILTER,           4u);
    key.set(Shader_int::FP16_PATHS,              0x1fu);

    const std::vector<Shader_define_constants> all = key.get_define_constants();
    const auto expression = [&](const std::string& define) -> std::string {
        const Shader_define_constants* constants = find(all, define);
        if (constants == nullptr) {
            return "(none)";
        }
        for (const auto& [name, value] : key.get_defines()) {
            if (name == define) {
                return get_define_value_expression(*constants, value);
            }
        }
        return "(not a define)";
    };
    EXPECT_EQ(expression("ERHE_BXDF_MODEL"),              "ERHE_BXDF_MODEL_ANISOTROPIC_BRDF");
    EXPECT_EQ(expression("ERHE_NORMAL_TEXTURE_ENCODING"), "ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG");
    EXPECT_EQ(expression("ERHE_BASE_COLOR_TEXGEN_MODE"),  "ERHE_TEXGEN_MODE_TANGENT");
    EXPECT_EQ(expression("ERHE_SHADOW_FILTER"),           "ERHE_SHADOW_FILTER_PCF_4X4");
    EXPECT_EQ(expression("ERHE_SHADER_DEBUG"),            "ERHE_SHADER_DEBUG_NONE");
    EXPECT_EQ(expression("ERHE_FP16_PATHS"),              "(ERHE_FP16_PATH_MATERIAL | ERHE_FP16_PATH_BRDF | ERHE_FP16_PATH_ACCUMULATE | ERHE_FP16_PATH_ATTENUATION | ERHE_FP16_PATH_VISIBILITY)");
    EXPECT_EQ(find(all, "ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED"), nullptr); // a count axis
    EXPECT_EQ(find(all, "ERHE_SHADOW_DEPTH_BITS"), nullptr);                     // a size axis

    // Every Shader_debug enumerator has its constant.
    const Shader_define_constants* debug = find(all, "ERHE_SHADER_DEBUG");
    ASSERT_NE(debug, nullptr);
    EXPECT_EQ(debug->constants.size(), std::size(erhe::scene_renderer::c_shader_debug_strings));
}

} // namespace
