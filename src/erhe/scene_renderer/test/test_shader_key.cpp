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
