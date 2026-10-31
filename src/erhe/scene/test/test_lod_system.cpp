// The Lod node system (doc/erhe/scene.md "Level of detail control"): a Scene
// owns one Lod_system, which resolves the effective `Lod` record of every mesh
// from the nearest node at or above it carrying Lod.apply_lod and tells the
// scene host when a mesh's record changes - and only then.

#include "erhe_scene/lod.hpp"
#include "erhe_scene/lod_system.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace {

using erhe::scene::Lod;
using erhe::scene::Lod_data;
using erhe::scene::Lod_mode;
using erhe::scene::Mesh;
using erhe::scene::Xform;

class Recording_scene_host : public erhe::scene::Scene_host
{
public:
    Recording_scene_host() : scene{"test scene", this} {}

    auto get_host_name   () const -> const char*         override { return "Recording_scene_host"; }
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
    void on_mesh_lod_changed           (const std::shared_ptr<erhe::scene::Mesh>& mesh, const Lod_data& lod) override
    {
        lod_changes.emplace_back(mesh.get(), lod);
    }
    void on_mesh_bounds_changed        (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    // The last record the host was told for the mesh, the default record when
    // it was told nothing.
    [[nodiscard]] auto last(const Mesh* mesh) const -> Lod_data
    {
        for (std::size_t i = lod_changes.size(); i > 0; --i) {
            if (lod_changes[i - 1].first == mesh) {
                return lod_changes[i - 1].second;
            }
        }
        return Lod_data{};
    }

    erhe::scene::Scene                         scene;
    std::vector<std::pair<Mesh*, Lod_data>>    lod_changes;
};

[[nodiscard]] auto make_group(Recording_scene_host& host, const char* name) -> std::shared_ptr<Xform>
{
    std::shared_ptr<Xform> node = std::make_shared<Xform>(name);
    node->set_parent(host.scene.get_root_node());
    return node;
}

[[nodiscard]] auto make_mesh(const std::shared_ptr<erhe::scene::Node>& parent, const char* name) -> std::shared_ptr<Mesh>
{
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>(name);
    mesh->set_parent(parent);
    return mesh;
}

} // anonymous namespace

TEST(Lod, forced_level_is_clamped_to_min_and_max)
{
    Lod_data data;
    data.forced_level = 3;
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{0}); // automatic
    data.mode = Lod_mode::off;
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{0});
    data.mode = Lod_mode::forced;
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{3});
    data.max_level = 2;
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{2});
    data.min_level    = 1;
    data.forced_level = 0;
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{1});
    data.min_level    = 5; // max below min counts as min
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{5});
    data.min_level    = -2;
    data.max_level    = 7;
    data.forced_level = -1;
    EXPECT_EQ(erhe::scene::get_forced_lod_level(data), std::size_t{0});
}

TEST(Lod_system, the_key_on_the_mesh_applies_its_values)
{
    Recording_scene_host host;
    const std::shared_ptr<Xform> group = make_group(host, "group");
    const std::shared_ptr<Mesh>  mesh  = make_mesh(group, "mesh");
    EXPECT_TRUE(host.lod_changes.empty());

    // Values without the key on the path apply to nothing.
    mesh->set_value(Lod::mode_property, Lod_mode::forced);
    mesh->set_value(Lod::forced_level_property, 2);
    EXPECT_TRUE(host.lod_changes.empty());
    EXPECT_EQ(host.scene.get_lod_system().find(*mesh), nullptr);

    mesh->set_value(Lod::apply_lod_property, true);
    ASSERT_EQ(host.lod_changes.size(), std::size_t{1});
    EXPECT_EQ(host.last(mesh.get()).mode, Lod_mode::forced);
    EXPECT_EQ(host.last(mesh.get()).forced_level, 2);
    ASSERT_NE(host.scene.get_lod_system().find(*mesh), nullptr);

    // A value change that leaves the record alone is not reported.
    mesh->set_value(Lod::forced_level_property, 2);
    EXPECT_EQ(host.lod_changes.size(), std::size_t{1});

    mesh->set_value(Lod::forced_level_property, 1);
    ASSERT_EQ(host.lod_changes.size(), std::size_t{2});
    EXPECT_EQ(host.last(mesh.get()).forced_level, 1);

    mesh->clear_value(Lod::apply_lod_property);
    ASSERT_EQ(host.lod_changes.size(), std::size_t{3});
    EXPECT_EQ(host.last(mesh.get()), Lod_data{});
    EXPECT_EQ(host.scene.get_lod_system().find(*mesh), nullptr);
}

TEST(Lod_system, values_on_a_group_reach_the_meshes_below)
{
    Recording_scene_host host;
    const std::shared_ptr<Xform> group  = make_group(host, "group");
    const std::shared_ptr<Xform> middle = std::make_shared<Xform>("middle");
    middle->set_parent(group);
    const std::shared_ptr<Mesh>  a      = make_mesh(group,  "a");
    const std::shared_ptr<Mesh>  b      = make_mesh(middle, "b");

    group->set_value(Lod::apply_lod_property, true);
    // Default values: the records stay default, nothing to tell.
    EXPECT_TRUE(host.lod_changes.empty());

    group->set_value(Lod::mode_property, Lod_mode::forced);
    group->set_value(Lod::forced_level_property, 3);
    EXPECT_EQ(host.last(a.get()).mode, Lod_mode::forced);
    EXPECT_EQ(host.last(a.get()).forced_level, 3);
    EXPECT_EQ(host.last(b.get()).mode, Lod_mode::forced);
    EXPECT_EQ(host.last(b.get()).forced_level, 3);

    // A mesh's own local value wins over the inherited one.
    b->set_value(Lod::forced_level_property, 1);
    EXPECT_EQ(host.last(b.get()).forced_level, 1);
    EXPECT_EQ(host.last(a.get()).forced_level, 3);

    // Clearing the key on the group takes every mesh below back to default.
    group->clear_value(Lod::apply_lod_property);
    EXPECT_EQ(host.last(a.get()), Lod_data{});
    EXPECT_EQ(host.last(b.get()), Lod_data{});
    EXPECT_TRUE(host.scene.get_lod_system().get_records().empty());

    // Setting it again brings the values back.
    group->set_value(Lod::apply_lod_property, true);
    EXPECT_EQ(host.last(a.get()).forced_level, 3);
    EXPECT_EQ(host.last(b.get()).forced_level, 1);
}

TEST(Lod_system, a_mesh_entering_the_scene_is_resolved_and_leaving_forgets_it)
{
    Recording_scene_host host;
    const std::shared_ptr<Xform> group = make_group(host, "group");
    group->set_value(Lod::apply_lod_property, true);
    group->set_value(Lod::mode_property, Lod_mode::off);

    const std::shared_ptr<Xform> detached = std::make_shared<Xform>("detached");
    const std::shared_ptr<Mesh>  mesh     = make_mesh(detached, "mesh");
    EXPECT_TRUE(host.lod_changes.empty());

    detached->set_parent(group);
    EXPECT_EQ(host.last(mesh.get()).mode, Lod_mode::off);
    ASSERT_NE(host.scene.get_lod_system().find(*mesh), nullptr);

    detached->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    EXPECT_EQ(host.scene.get_lod_system().find(*mesh), nullptr);
    EXPECT_TRUE(host.scene.get_lod_system().get_records().empty());
}
