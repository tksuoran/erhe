#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"
#include "erhe_texgen/shader_code.hpp"

#include "test_descriptors.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace erhe::texgen::test {

// A lone sampler-source node (Phase 5 buffer cut point) contributes exactly one
// sampler binding and its output samples that texture.
TEST(Sampler_source, single_source_declares_sampler_and_samples_it)
{
    const Compose_node source{1, 0, Value_type::rgba};
    const Composer     composer{};
    const Shader_code  shader_code = composer.compose(source, 0);

    ASSERT_EQ(shader_code.get_samplers().size(), 1u);
    EXPECT_EQ(shader_code.get_samplers()[0].name,    "tex_0");
    EXPECT_EQ(shader_code.get_samplers()[0].binding, 0);
    EXPECT_EQ(shader_code.get_output_type(), Value_type::rgba);

    // rgba buffer: the sample is used unconverted.
    EXPECT_NE(shader_code.get_code().find("= texture(tex_0, (uv));"), std::string::npos)
        << shader_code.get_code();
}

// The assembled fragment (plain mode) declares the sampler and calls texture().
TEST(Sampler_source, assembled_fragment_declares_sampler2d)
{
    const Compose_node source{7, 3, Value_type::rgba};
    const Composer     composer{};
    const std::string  fragment = composer.assemble_fragment(composer.compose(source, 0));

    EXPECT_NE(fragment.find("uniform sampler2D tex_3;"),  std::string::npos) << fragment;
    EXPECT_NE(fragment.find("texture(tex_3, (uv))"),      std::string::npos) << fragment;
    EXPECT_NE(fragment.find("out_color = "),              std::string::npos) << fragment;
}

// A grayscale buffer stores rgba8; sampling converts vec4 -> f.
TEST(Sampler_source, grayscale_buffer_converts_rgba_sample_to_float)
{
    const Compose_node source{2, 1, Value_type::grayscale};
    const Composer     composer{};
    const Shader_code  shader_code = composer.compose(source, 0);

    EXPECT_EQ(shader_code.get_output_type(), Value_type::grayscale);
    // rgba -> f conversion template wraps the texture() call.
    EXPECT_NE(shader_code.get_code().find("dot((texture(tex_1, (uv))).rgb, vec3(1.0))/3.0"), std::string::npos)
        << shader_code.get_code();
    // Grayscale sink is wrapped back to vec4 at the entry-point output.
    const std::string fragment = composer.assemble_fragment(shader_code);
    EXPECT_NE(fragment.find("out_color = vec4(vec3("), std::string::npos) << fragment;
}

// A downstream node fed by a sampler source: the walk stops at the buffer (no
// upstream inlining), the sampler binding propagates up to the sink's
// Shader_code, and the sink samples the buffer where its input is read.
TEST(Sampler_source, downstream_filter_samples_buffer_not_inlined_subtree)
{
    const Node_descriptor blend_descriptor = make_blend_multiply_descriptor();

    // Two sampler sources standing in for two buffers, wired into a blend.
    const Compose_node buffer_a{10, 0, Value_type::rgba};
    const Compose_node buffer_b{11, 1, Value_type::rgba};
    Compose_node       blend{blend_descriptor, 12};
    blend.set_input(blend.get_input_index("in1"), &buffer_a, 0);
    blend.set_input(blend.get_input_index("in2"), &buffer_b, 0);

    const Composer    composer{};
    const Shader_code shader_code = composer.compose(blend, 0);

    // Both buffer bindings reached the sink (registration order follows the
    // right-to-left substitution of the blend expression, so check membership).
    ASSERT_EQ(shader_code.get_samplers().size(), 2u);
    bool has_tex_0 = false;
    bool has_tex_1 = false;
    for (const Sampler_binding& sampler : shader_code.get_samplers()) {
        has_tex_0 = has_tex_0 || (sampler.name == "tex_0");
        has_tex_1 = has_tex_1 || (sampler.name == "tex_1");
    }
    EXPECT_TRUE(has_tex_0);
    EXPECT_TRUE(has_tex_1);

    const std::string fragment = composer.assemble_fragment(shader_code);
    EXPECT_NE(fragment.find("uniform sampler2D tex_0;"), std::string::npos) << fragment;
    EXPECT_NE(fragment.find("uniform sampler2D tex_1;"), std::string::npos) << fragment;
    EXPECT_NE(fragment.find("texture(tex_0,"),           std::string::npos) << fragment;
    EXPECT_NE(fragment.find("texture(tex_1,"),           std::string::npos) << fragment;
}

// A sampler source multiply-sampled through a function-form input still yields
// one binding, and the sampled texture appears inside the emitted helper.
TEST(Sampler_source, function_input_over_buffer_declares_one_binding)
{
    const Node_descriptor blur_descriptor = make_fake_blur_descriptor();

    const Compose_node buffer{20, 5, Value_type::grayscale};
    Compose_node       blur{blur_descriptor, 21};
    blur.set_input(blur.get_input_index("source"), &buffer, 0);

    const Composer    composer{};
    const Shader_code shader_code = composer.compose(blur, 0);

    ASSERT_EQ(shader_code.get_samplers().size(), 1u);
    EXPECT_EQ(shader_code.get_samplers()[0].name,    "tex_5");
    EXPECT_EQ(shader_code.get_samplers()[0].binding, 5);

    // The buffer is sampled inside the generated input helper function body.
    EXPECT_NE(shader_code.get_defs().find("o21_input_source(vec2 uv)"), std::string::npos)
        << shader_code.get_defs();
    EXPECT_NE(shader_code.get_defs().find("texture(tex_5, (uv))"), std::string::npos)
        << shader_code.get_defs();
}

} // namespace erhe::texgen::test
