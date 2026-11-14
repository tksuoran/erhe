// A Curve is a prim class of its own (doc/editor/curves.md): one curve path
// in its frame, its baked table, and a change notification to the scene host.

#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host()
        : scene{"test scene", this}
    {
    }

    auto get_host_name   () const -> const char*         override { return "Test_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&)         override {}
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&)         override {}

    void on_mesh_primitives_changed    (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>&, uint64_t, uint64_t) override {}
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_lod_changed           (const std::shared_ptr<erhe::scene::Mesh>&, const erhe::scene::Lod_data&) override {}
    void on_mesh_bounds_changed        (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    void on_curve_changed(const std::shared_ptr<erhe::scene::Curve>&, const erhe::scene::Curve_change change) override
    {
        if (change == erhe::scene::Curve_change::shape) {
            ++shape_count;
        } else {
            ++transform_count;
        }
    }

    erhe::scene::Scene scene;
    int                shape_count    {0};
    int                transform_count{0};
};

auto make_polyline() -> erhe::math::Curve_path
{
    erhe::math::Curve_path path{};
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{0.0f, 0.0f, 0.0f}});
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{3.0f, 0.0f, 0.0f}});
    path.points.push_back(erhe::math::Curve_point{.position = glm::vec3{3.0f, 0.0f, 4.0f}});
    return path;
}

} // namespace

TEST(Curve_prims, curve_is_a_gprim_of_its_own_class)
{
    auto curve = std::make_shared<erhe::scene::Curve>("curve");
    EXPECT_TRUE (erhe::is<erhe::scene::Node> (curve.get()));
    EXPECT_TRUE (erhe::is<erhe::scene::Gprim>(curve.get()));
    EXPECT_TRUE (erhe::is<erhe::scene::Curve>(curve.get()));
    EXPECT_FALSE(erhe::is<erhe::scene::Mesh> (curve.get()));
    EXPECT_FALSE(erhe::is<erhe::scene::Xform>(curve.get()));
    EXPECT_EQ   (curve->get_class_type_name(), "Curve");
    EXPECT_EQ   (curve->get_type_name(), "Curve");
}

TEST(Curve_prims, set_path_rebuilds_the_table_and_the_draw_points)
{
    auto curve = std::make_shared<erhe::scene::Curve>("curve");
    EXPECT_EQ(curve->get_revision(), 0u);
    EXPECT_FLOAT_EQ(curve->get_table().get_length(), 0.0f);
    curve->set_path(make_polyline());
    EXPECT_EQ(curve->get_revision(), 1u);
    EXPECT_FLOAT_EQ(curve->get_table().get_length(), 7.0f);
    // Line spans add their end point only.
    ASSERT_EQ(curve->get_draw_points().size(), 3u);
    EXPECT_EQ(curve->get_draw_points()[2], (glm::vec3{3.0f, 0.0f, 4.0f}));

    curve->set_path(erhe::scene::make_default_curve_path());
    EXPECT_EQ(curve->get_draw_points().size(), 1u + erhe::scene::Curve::draw_points_per_cubic_span);
    EXPECT_GT(curve->get_table().get_length(), 1.0f);
}

TEST(Curve_prims, clone_copies_the_path_and_its_table)
{
    auto curve = std::make_shared<erhe::scene::Curve>("curve");
    erhe::math::Curve_path path = make_polyline();
    path.closure = erhe::math::Curve_closure::closed;
    curve->set_path(path);
    const std::shared_ptr<erhe::Item_base> clone_item = curve->clone();
    const std::shared_ptr<erhe::scene::Curve> clone = std::dynamic_pointer_cast<erhe::scene::Curve>(clone_item);
    ASSERT_TRUE(clone);
    ASSERT_EQ(clone->get_path().points.size(), 3u);
    EXPECT_EQ(clone->get_path().closure, erhe::math::Curve_closure::closed);
    EXPECT_FLOAT_EQ(clone->get_table().get_length(), 12.0f);
}

TEST(Curve_prims, the_host_hears_shape_and_transform_changes)
{
    Test_scene_host host;
    auto parent = std::make_shared<erhe::scene::Xform>("parent");
    auto curve  = std::make_shared<erhe::scene::Curve>("curve");
    parent->set_parent(host.scene.get_root_node());
    erhe::scene::set_prim_parent(curve, parent);
    host.shape_count     = 0;
    host.transform_count = 0;

    curve->set_path(make_polyline());
    EXPECT_EQ(host.shape_count, 1);

    curve->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{0.0f, 1.0f, 0.0f}});
    EXPECT_GE(host.transform_count, 1);

    // A move of the parent moves the curve in world space.
    const int before = host.transform_count;
    parent->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{2.0f, 0.0f, 0.0f}});
    host.scene.update_node_transforms();
    EXPECT_GT(host.transform_count, before);

    // A curve outside a scene tells nobody.
    auto loose = std::make_shared<erhe::scene::Curve>("loose");
    loose->set_path(make_polyline());
    EXPECT_EQ(host.shape_count, 1);
}
