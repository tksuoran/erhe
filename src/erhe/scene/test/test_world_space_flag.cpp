// Item_flags::world_space (doc/erhe/shader_variants.md "World-space vertices"):
// derived on every transform update, set when every node from the scene root
// to the node has the identity local transform.

#include "erhe_item/item_flags.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

class Flag_change
{
public:
    const erhe::scene::Mesh* mesh{nullptr};
    uint64_t                 old_flag_bits{0};
    uint64_t                 new_flag_bits{0};
};

// Records the mesh flag changes the host is told about: the draw lists
// re-key a mesh from exactly this notification.
class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host()
        : scene{"test scene", this}
    {
        scene.add_mesh_layer(std::make_shared<erhe::scene::Mesh_layer>("content", 0, content_layer_id));
    }

    static constexpr erhe::scene::Layer_id content_layer_id{0};

    auto get_host_name   () const -> const char*         override { return "Test_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node) override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node) override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)      override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)      override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&   mesh) override { scene.register_mesh  (mesh); }
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&   mesh) override { scene.unregister_mesh(mesh); }
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)        override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)        override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&)       override {}
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&)       override {}

    void on_mesh_primitives_changed    (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>& mesh, const uint64_t old_flag_bits, const uint64_t new_flag_bits) override
    {
        flag_changes.push_back(Flag_change{.mesh = mesh.get(), .old_flag_bits = old_flag_bits, .new_flag_bits = new_flag_bits});
    }
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_lod_changed           (const std::shared_ptr<erhe::scene::Mesh>&, const erhe::scene::Lod_data&) override {}
    void on_mesh_bounds_changed        (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    // The world_space transitions of `mesh`, in order (true = set).
    [[nodiscard]] auto world_space_transitions(const erhe::scene::Mesh& mesh) const -> std::vector<bool>
    {
        std::vector<bool> transitions;
        for (const Flag_change& change : flag_changes) {
            const uint64_t changed = change.old_flag_bits ^ change.new_flag_bits;
            if ((change.mesh == &mesh) && ((changed & erhe::Item_flags::world_space) != 0)) {
                transitions.push_back((change.new_flag_bits & erhe::Item_flags::world_space) != 0);
            }
        }
        return transitions;
    }

    erhe::scene::Scene       scene;
    std::vector<Flag_change> flag_changes;
};

[[nodiscard]] auto world_space(const erhe::Item_base& item) -> bool
{
    return (item.get_flag_bits() & erhe::Item_flags::world_space) != 0;
}

[[nodiscard]] auto translated(const glm::vec3& translation) -> erhe::scene::Trs_transform
{
    return erhe::scene::Trs_transform{translation};
}

} // namespace

TEST(World_space_flag, a_new_node_without_a_parent_starts_with_it)
{
    const auto xform = std::make_shared<erhe::scene::Xform>("xform");
    const auto mesh  = std::make_shared<erhe::scene::Mesh>("mesh");
    EXPECT_TRUE(world_space(*xform));
    EXPECT_TRUE(world_space(*mesh));

    // An orphan is the root of its own path: its own transform decides.
    mesh->set_parent_from_node(translated(glm::vec3{1.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(world_space(*mesh));
    mesh->set_parent_from_node(erhe::scene::Trs_transform{});
    EXPECT_TRUE(world_space(*mesh));
}

TEST(World_space_flag, a_moved_scene_root_clears_every_descendant_and_restores_them)
{
    Test_scene_host host;
    const std::shared_ptr<erhe::scene::Node> root  = host.scene.get_root_node();
    const auto                               xform = std::make_shared<erhe::scene::Xform>("xform");
    const auto                               mesh  = std::make_shared<erhe::scene::Mesh>("mesh");
    xform->set_parent(root);
    erhe::scene::set_mesh_parent(mesh, xform);
    host.scene.update_node_transforms();
    ASSERT_TRUE(world_space(*root));
    ASSERT_TRUE(world_space(*xform));
    ASSERT_TRUE(world_space(*mesh));
    host.flag_changes.clear();

    root->set_parent_from_node(translated(glm::vec3{0.0f, 0.0f, 3.0f}));
    host.scene.update_node_transforms();
    EXPECT_FALSE(world_space(*root));
    EXPECT_FALSE(world_space(*xform));
    EXPECT_FALSE(world_space(*mesh));

    root->set_parent_from_node(erhe::scene::Trs_transform{});
    host.scene.update_node_transforms();
    EXPECT_TRUE(world_space(*root));
    EXPECT_TRUE(world_space(*xform));
    EXPECT_TRUE(world_space(*mesh));

    // The host heard both transitions of the mesh, once each.
    EXPECT_EQ(host.world_space_transitions(*mesh), (std::vector<bool>{false, true}));
}

TEST(World_space_flag, a_moved_branch_clears_only_its_own_subtree)
{
    Test_scene_host host;
    const std::shared_ptr<erhe::scene::Node> root   = host.scene.get_root_node();
    const auto                               a      = std::make_shared<erhe::scene::Xform>("a");
    const auto                               b      = std::make_shared<erhe::scene::Xform>("b");
    const auto                               c      = std::make_shared<erhe::scene::Xform>("c");
    const auto                               mesh_b = std::make_shared<erhe::scene::Mesh>("mesh b");
    const auto                               mesh_c = std::make_shared<erhe::scene::Mesh>("mesh c");
    a->set_parent(root);
    b->set_parent(a);
    c->set_parent(a);
    erhe::scene::set_mesh_parent(mesh_b, b);
    erhe::scene::set_mesh_parent(mesh_c, c);
    host.scene.update_node_transforms();
    ASSERT_TRUE(world_space(*mesh_b));
    ASSERT_TRUE(world_space(*mesh_c));
    host.flag_changes.clear();

    b->set_parent_from_node(translated(glm::vec3{0.0f, 1.0f, 0.0f}));
    host.scene.update_node_transforms();
    EXPECT_TRUE (world_space(*a));
    EXPECT_FALSE(world_space(*b));
    EXPECT_FALSE(world_space(*mesh_b));
    EXPECT_TRUE (world_space(*c));
    EXPECT_TRUE (world_space(*mesh_c));
    EXPECT_TRUE(host.world_space_transitions(*mesh_c).empty());
}

TEST(World_space_flag, the_path_decides_not_the_world_transform)
{
    Test_scene_host host;
    const auto parent = std::make_shared<erhe::scene::Xform>("parent");
    const auto mesh   = std::make_shared<erhe::scene::Mesh>("mesh");
    parent->set_parent(host.scene.get_root_node());
    erhe::scene::set_mesh_parent(mesh, parent);

    // The child undoes its parent's translation: the world transform is the
    // identity, the path is not.
    parent->set_parent_from_node(translated(glm::vec3{0.0f, 2.0f, 0.0f}));
    mesh->set_parent_from_node(translated(glm::vec3{0.0f, -2.0f, 0.0f}));
    host.scene.update_node_transforms();
    EXPECT_EQ(mesh->world_from_node(), glm::mat4{1.0f});
    EXPECT_FALSE(world_space(*mesh));
}

TEST(World_space_flag, a_scope_in_the_path_passes_the_bit_through)
{
    Test_scene_host host;
    const auto a     = std::make_shared<erhe::scene::Xform>("a");
    const auto scope = std::make_shared<erhe::Scope>("scope");
    const auto mesh  = std::make_shared<erhe::scene::Mesh>("mesh");
    a->set_parent(host.scene.get_root_node());
    scope->set_parent(a);
    mesh->set_parent(scope);
    host.scene.update_node_transforms();
    EXPECT_TRUE(world_space(*mesh));

    a->set_parent_from_node(translated(glm::vec3{1.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    EXPECT_FALSE(world_space(*mesh));

    a->set_parent_from_node(erhe::scene::Trs_transform{});
    host.scene.update_node_transforms();
    EXPECT_TRUE(world_space(*mesh));
}

TEST(World_space_flag, reparenting_follows_the_new_path)
{
    Test_scene_host host;
    const std::shared_ptr<erhe::scene::Node> root  = host.scene.get_root_node();
    const auto                               moved = std::make_shared<erhe::scene::Xform>("moved");
    const auto                               mesh  = std::make_shared<erhe::scene::Mesh>("mesh");
    moved->set_parent(root);
    moved->set_parent_from_node(translated(glm::vec3{5.0f, 0.0f, 0.0f}));
    erhe::scene::set_mesh_parent(mesh, root);
    host.scene.update_node_transforms();
    ASSERT_TRUE(world_space(*mesh));

    // A plain reparent keeps the mesh's own identity transform; the new
    // parent's path is not the identity.
    erhe::scene::set_mesh_parent(mesh, moved);
    host.scene.update_node_transforms();
    EXPECT_FALSE(world_space(*mesh));

    erhe::scene::set_mesh_parent(mesh, root);
    host.scene.update_node_transforms();
    EXPECT_TRUE(world_space(*mesh));
}

TEST(World_space_flag, a_clone_starts_from_its_own_transform)
{
    const auto identity = std::make_shared<erhe::scene::Mesh>("identity");
    const auto moved    = std::make_shared<erhe::scene::Mesh>("moved");
    moved->set_parent_from_node(translated(glm::vec3{0.0f, 0.0f, 1.0f}));

    const auto identity_clone = std::static_pointer_cast<erhe::scene::Mesh>(identity->clone());
    const auto moved_clone    = std::static_pointer_cast<erhe::scene::Mesh>(moved->clone());
    ASSERT_TRUE(identity_clone);
    ASSERT_TRUE(moved_clone);
    EXPECT_TRUE (world_space(*identity_clone));
    EXPECT_FALSE(world_space(*moved_clone));
}

TEST(World_space_flag, is_read_only)
{
    const auto mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->disable_flag_bits(erhe::Item_flags::world_space);
    EXPECT_TRUE(world_space(*mesh));
    EXPECT_TRUE(mesh->get_value(erhe::scene::Xformable::world_space_property));

    const auto moved = std::make_shared<erhe::scene::Mesh>("moved");
    moved->set_parent_from_node(translated(glm::vec3{1.0f, 0.0f, 0.0f}));
    moved->enable_flag_bits(erhe::Item_flags::world_space);
    EXPECT_FALSE(world_space(*moved));
    EXPECT_FALSE(moved->get_value(erhe::scene::Xformable::world_space_property));
}
