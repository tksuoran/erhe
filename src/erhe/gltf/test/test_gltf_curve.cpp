// ERHE_curve (doc/gltf_extensions/ERHE_curve.md): the JSON form of a curve
// path, and a Curve prim's save -> load round trip, bit for bit.

#include "gltf_test_util.hpp"

#include "erhe_gltf/gltf_curve.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace {

[[nodiscard]] auto bits_equal(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return std::memcmp(&a, &b, sizeof(glm::vec3)) == 0;
}

[[nodiscard]] auto paths_bit_equal(const erhe::math::Curve_path& a, const erhe::math::Curve_path& b) -> bool
{
    if ((a.closure != b.closure) || (a.points.size() != b.points.size())) {
        return false;
    }
    for (std::size_t i = 0, end = a.points.size(); i < end; ++i) {
        const erhe::math::Curve_point& p = a.points[i];
        const erhe::math::Curve_point& q = b.points[i];
        if (!bits_equal(p.position, q.position) || !bits_equal(p.in_handle, q.in_handle) || !bits_equal(p.out_handle, q.out_handle) || (p.kind != q.kind)) {
            return false;
        }
    }
    return true;
}

// Values whose shortest decimal forms are long, tiny, huge, negative zero and subnormal.
[[nodiscard]] auto make_awkward_path() -> erhe::math::Curve_path
{
    erhe::math::Curve_path path{};
    path.closure = erhe::math::Curve_closure::closed;
    path.points.push_back(erhe::math::Curve_point{
        .position   = glm::vec3{0.1f, -0.0f, 1.0f / 3.0f},
        .in_handle  = glm::vec3{std::nextafter(1.0f, 2.0f), 1.0e-30f, -3.4e38f},
        .out_handle = glm::vec3{std::numeric_limits<float>::denorm_min(), 16777217.0f, -2.5f},
        .kind       = erhe::math::Curve_point_kind::free
    });
    path.points.push_back(erhe::math::Curve_point{
        .position = glm::vec3{7.0f, 0.0f, -0.7f},
        .kind     = erhe::math::Curve_point_kind::corner
    });
    path.points.push_back(erhe::math::Curve_point{
        .position   = glm::vec3{2.0f, 1.0f, 3.0f},
        .in_handle  = glm::vec3{-0.5f, 0.0f, 0.0f},
        .out_handle = glm::vec3{0.5f, 0.0f, 0.0f},
        .kind       = erhe::math::Curve_point_kind::smooth
    });
    return path;
}

} // namespace

TEST(Gltf_curve, json_form_reads_back_bit_exact)
{
    const erhe::math::Curve_path path = make_awkward_path();
    const std::string json = erhe::gltf::curve_path_to_json(path);
    erhe::math::Curve_path read{};
    std::string error;
    ASSERT_TRUE(erhe::gltf::curve_path_from_json(json, read, error)) << error << "\n" << json;
    EXPECT_TRUE(paths_bit_equal(path, read)) << json;
    EXPECT_EQ(erhe::gltf::curve_path_to_json(read), json);
}

TEST(Gltf_curve, json_form_refuses_malformed_values)
{
    const char* const cases[] = {
        "[]",
        "{\"points\":[]}",
        "{\"closed\":false}",
        "{\"closed\":false,\"points\":[],\"extra\":1}",
        "{\"closed\":false,\"points\":[{\"position\":[0,0],\"in\":[0,0,0],\"out\":[0,0,0],\"kind\":\"corner\"}]}",
        "{\"closed\":false,\"points\":[{\"position\":[0,0,0],\"in\":[0,0,0],\"out\":[0,0,0],\"kind\":\"pointy\"}]}",
        "{\"closed\":false,\"points\":[{\"position\":[0,0,0],\"in\":[0,0,0],\"out\":[0,0,0]}]}",
        "{\"closed\":false,\"points\":[{\"position\":[0,0,1e39],\"in\":[0,0,0],\"out\":[0,0,0],\"kind\":\"corner\"}]}"
    };
    for (const char* const text : cases) {
        erhe::math::Curve_path read{};
        std::string error;
        EXPECT_FALSE(erhe::gltf::curve_path_from_json(text, read, error)) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
}

TEST(Gltf_curve, curve_prim_round_trips_bit_exact)
{
    erhe::scene::Scene scene{"source", nullptr};
    const std::shared_ptr<erhe::scene::Xform> parent = std::make_shared<erhe::scene::Xform>("Parent");
    parent->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    parent->set_parent(scene.get_root_node());
    const std::shared_ptr<erhe::scene::Curve> curve = std::make_shared<erhe::scene::Curve>("Track");
    curve->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    curve->set_path(make_awkward_path());
    erhe::scene::set_prim_parent(curve, parent);
    curve->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{1.0f, 2.0f, 3.0f}});
    const std::shared_ptr<erhe::scene::Xform> child = std::make_shared<erhe::scene::Xform>("Below");
    child->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    child->set_parent(curve);

    const std::string text = erhe::gltf::export_gltf(*scene.get_root_node(), true);
    ASSERT_FALSE(text.empty());
    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = erhe_gltf_test::parse_file(erhe_gltf_test::write_temporary_file("curve.glb", text), loaded);

    std::shared_ptr<erhe::scene::Curve> loaded_curve;
    std::shared_ptr<erhe::scene::Node>  loaded_child;
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == "Track")) {
            loaded_curve = std::dynamic_pointer_cast<erhe::scene::Curve>(node);
        }
        if (node && (node->get_name() == "Below")) {
            loaded_child = node;
        }
    }
    ASSERT_TRUE(loaded_curve);
    ASSERT_TRUE(loaded_child);
    EXPECT_TRUE(paths_bit_equal(loaded_curve->get_path(), curve->get_path()));
    EXPECT_EQ(loaded_curve->get_parent_node()->get_name(), "Parent");
    EXPECT_EQ(loaded_child->get_parent_node(), loaded_curve);
    EXPECT_EQ(glm::vec3{loaded_curve->parent_from_node()[3]}, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(loaded_curve->get_table().get_length(), curve->get_table().get_length());
}
