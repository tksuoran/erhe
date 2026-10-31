// The SPECULAR_ANTI_ALIASING variant axis (doc/erhe/shader_variants.md
// "Specular anti-aliasing"): set_specular_anti_aliasing_axis() keeps the
// environment's mode only on keys whose shader calls anisotropic_brdf().

#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::primitive::Bxdf_model;
using erhe::primitive::Material;
using erhe::scene_renderer::Shader_bool;
using erhe::scene_renderer::Shader_int;
using erhe::scene_renderer::Shader_key;
using erhe::scene_renderer::Specular_anti_aliasing;
using erhe::scene_renderer::get_specular_anti_aliasing;
using erhe::scene_renderer::set_specular_anti_aliasing_axis;

[[nodiscard]] auto make_format(const bool with_tangent) -> Vertex_format
{
    if (with_tangent) {
        return Vertex_format{
            Vertex_stream{
                0,
                {
                    {Format::format_32_vec3_float, Vertex_attribute_usage::position, 0},
                    {Format::format_32_vec3_float, Vertex_attribute_usage::normal,   erhe::dataformat::normal_attribute},
                    {Format::format_32_vec4_float, Vertex_attribute_usage::tangent,  0}
                }
            }
        };
    }
    return Vertex_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position, 0},
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,   erhe::dataformat::normal_attribute}
            }
        }
    };
}

[[nodiscard]] auto make_material(const Bxdf_model bxdf_model) -> std::shared_ptr<Material>
{
    std::shared_ptr<Material> material = std::make_shared<Material>(std::string_view{"test material"});
    material->set_value(Material::bxdf_model_property, bxdf_model);
    return material;
}

[[nodiscard]] auto make_environment(const Specular_anti_aliasing mode) -> Shader_key
{
    Shader_key environment{};
    environment.set(Shader_int::SPECULAR_ANTI_ALIASING, static_cast<uint32_t>(mode));
    return environment;
}

constexpr Specular_anti_aliasing c_modes[] = {
    Specular_anti_aliasing::off,
    Specular_anti_aliasing::normal_kernel,
    Specular_anti_aliasing::finite_difference
};

} // namespace

TEST(Shader_key_specular_anti_aliasing, anisotropic_with_tangents_keeps_the_mode)
{
    const Vertex_format format = make_format(true);
    for (const Bxdf_model bxdf_model : {Bxdf_model::anisotropic_brdf, Bxdf_model::anisotropic_engine_ready}) {
        const std::shared_ptr<Material> material = make_material(bxdf_model);
        for (const Specular_anti_aliasing mode : c_modes) {
            const Shader_key key = make_environment(mode).derive(material.get(), &format, false);
            EXPECT_TRUE(key.get(Shader_bool::USE_VERTEX_VARYING_TANGENT));
            EXPECT_TRUE(key.get(Shader_bool::USE_VERTEX_VARYING_BITANGENT));
            EXPECT_EQ(get_specular_anti_aliasing(key), mode);
        }
    }
}

TEST(Shader_key_specular_anti_aliasing, anisotropic_without_tangents_is_off)
{
    const Vertex_format             format   = make_format(false);
    const std::shared_ptr<Material> material = make_material(Bxdf_model::anisotropic_brdf);
    for (const Specular_anti_aliasing mode : c_modes) {
        const Shader_key key = make_environment(mode).derive(material.get(), &format, false);
        EXPECT_FALSE(key.get(Shader_bool::USE_VERTEX_VARYING_TANGENT));
        EXPECT_EQ(get_specular_anti_aliasing(key), Specular_anti_aliasing::off);
    }
}

TEST(Shader_key_specular_anti_aliasing, other_bxdf_models_are_off)
{
    const Vertex_format format = make_format(true);
    for (const Bxdf_model bxdf_model : {Bxdf_model::unlit, Bxdf_model::isotropic_brdf, Bxdf_model::anisotropic_slope}) {
        const std::shared_ptr<Material> material = make_material(bxdf_model);
        for (const Specular_anti_aliasing mode : c_modes) {
            const Shader_key key = make_environment(mode).derive(material.get(), &format, false);
            EXPECT_EQ(get_specular_anti_aliasing(key), Specular_anti_aliasing::off);
        }
    }
}

// The draw path derives with the environment axes zero (prepare) and lays
// the environment over the derived key at draw time
// (apply_render_environment(), Draw_list_scene::resolve_color_stages());
// for this axis that is set_specular_anti_aliasing_axis() with the
// environment's mode, which must give the key a derive() with the full
// environment gives.
TEST(Shader_key_specular_anti_aliasing, derive_equals_prepare_then_environment)
{
    for (const bool with_tangent : {false, true}) {
        const Vertex_format format = make_format(with_tangent);
        for (const Bxdf_model bxdf_model : {Bxdf_model::isotropic_brdf, Bxdf_model::anisotropic_brdf, Bxdf_model::anisotropic_slope, Bxdf_model::anisotropic_engine_ready}) {
            const std::shared_ptr<Material> material = make_material(bxdf_model);
            for (const Specular_anti_aliasing mode : c_modes) {
                const Shader_key full     = make_environment(mode).derive(material.get(), &format, false);
                Shader_key       prepared = Shader_key{}.derive(material.get(), &format, false);
                EXPECT_EQ(get_specular_anti_aliasing(prepared), Specular_anti_aliasing::off);
                set_specular_anti_aliasing_axis(prepared, mode);
                EXPECT_EQ(prepared, full);
            }
        }
    }
}

// The FP16_PATHS axis (doc/plans/shader_fp16.md): set_fp16_paths_axis()
// keeps the environment's mask only on lit color variants and sets 0 on
// the unlit, position-pass and debug keys, and derive() applies it the way
// a later set_fp16_paths_axis() with the environment's mask does.
namespace {

using erhe::scene_renderer::fp16_paths_all;
using erhe::scene_renderer::fp16_paths_low_end_set;
using erhe::scene_renderer::get_fp16_paths;
using erhe::scene_renderer::set_fp16_paths_axis;

[[nodiscard]] auto make_fp16_environment(const uint32_t mask) -> Shader_key
{
    Shader_key environment{};
    environment.set(Shader_int::FP16_PATHS, mask);
    return environment;
}

constexpr uint32_t c_fp16_masks[] = {0u, 1u, 2u, 3u, fp16_paths_low_end_set, fp16_paths_all};

} // namespace

TEST(Shader_key_fp16_paths, lit_variants_keep_the_mask)
{
    const Vertex_format format = make_format(true);
    for (const Bxdf_model bxdf_model : {Bxdf_model::isotropic_brdf, Bxdf_model::anisotropic_brdf, Bxdf_model::anisotropic_slope, Bxdf_model::anisotropic_engine_ready}) {
        const std::shared_ptr<Material> material = make_material(bxdf_model);
        for (const uint32_t mask : c_fp16_masks) {
            const Shader_key key = make_fp16_environment(mask).derive(material.get(), &format, false);
            EXPECT_EQ(get_fp16_paths(key), mask);
        }
    }
}

TEST(Shader_key_fp16_paths, unlit_position_pass_and_debug_variants_are_zero)
{
    const Vertex_format format = make_format(true);
    {
        const std::shared_ptr<Material> material = make_material(Bxdf_model::unlit);
        const Shader_key key = make_fp16_environment(fp16_paths_all).derive(material.get(), &format, false);
        EXPECT_EQ(get_fp16_paths(key), 0u);
    }
    const std::shared_ptr<Material> material = make_material(Bxdf_model::isotropic_brdf);
    for (const Shader_bool variant : {Shader_bool::VARIANT_DEPTH_ONLY, Shader_bool::VARIANT_ID_RENDER, Shader_bool::VARIANT_SHADOW_DISTANCE, Shader_bool::VARIANT_SHADOW_CUBE, Shader_bool::VARIANT_POINTS}) {
        Shader_key key = make_fp16_environment(fp16_paths_all).derive(material.get(), &format, false);
        EXPECT_EQ(get_fp16_paths(key), fp16_paths_all);
        key.set(variant, true);
        set_fp16_paths_axis(key, fp16_paths_all);
        EXPECT_EQ(get_fp16_paths(key), 0u);
    }
    {
        Shader_key environment = make_fp16_environment(fp16_paths_all);
        environment.set(Shader_int::SHADER_DEBUG, 1u);
        const Shader_key key = environment.derive(material.get(), &format, false);
        EXPECT_EQ(get_fp16_paths(key), 0u);
    }
}

TEST(Shader_key_fp16_paths, unknown_bits_are_dropped)
{
    const Vertex_format format = make_format(false);
    Shader_key key = make_fp16_environment(0u).derive(make_material(Bxdf_model::isotropic_brdf).get(), &format, false);
    set_fp16_paths_axis(key, 0xffffffffu);
    EXPECT_EQ(get_fp16_paths(key), fp16_paths_all);
}

TEST(Shader_key_fp16_paths, derive_equals_prepare_then_environment)
{
    for (const bool with_tangent : {false, true}) {
        const Vertex_format format = make_format(with_tangent);
        for (const Bxdf_model bxdf_model : {Bxdf_model::unlit, Bxdf_model::isotropic_brdf, Bxdf_model::anisotropic_brdf}) {
            const std::shared_ptr<Material> material = make_material(bxdf_model);
            for (const uint32_t mask : c_fp16_masks) {
                const Shader_key full     = make_fp16_environment(mask).derive(material.get(), &format, false);
                Shader_key       prepared = Shader_key{}.derive(material.get(), &format, false);
                EXPECT_EQ(get_fp16_paths(prepared), 0u);
                set_fp16_paths_axis(prepared, mask);
                EXPECT_EQ(prepared, full);
            }
        }
    }
}
