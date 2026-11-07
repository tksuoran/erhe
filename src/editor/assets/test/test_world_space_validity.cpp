#include "scene/world_space_validity.hpp"

#include "erhe_item/item_flags.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace {

[[nodiscard]] auto make_primitive() -> std::shared_ptr<erhe::primitive::Primitive>
{
    return std::make_shared<erhe::primitive::Primitive>(erhe::primitive::Buffer_mesh{});
}

[[nodiscard]] auto translated(const glm::vec3& translation) -> erhe::scene::Trs_transform
{
    return erhe::scene::Trs_transform{translation};
}

class World_space_validity_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root      = std::make_shared<erhe::scene::Xform>("root");
        mesh      = std::make_shared<erhe::scene::Mesh>("mesh");
        primitive = make_primitive();
        mesh->enable_flag_bits(erhe::Item_flags::content);
        mesh->add_primitive(primitive, {});
        erhe::scene::set_mesh_parent(mesh, root);
        mesh->set_parent_from_node(translated(glm::vec3{1.0f, 2.0f, 3.0f}));
        validity.reset(
            root.get(),
            std::vector<editor::World_space_validity_entry>{
                editor::World_space_validity_entry{
                    .mesh            = mesh.get(),
                    .world_from_node = glm::inverse(root->world_from_node()) * mesh->world_from_node(),
                    .shapes          = {primitive->render_shape.get()},
                    .complete        = true
                }
            },
            "settings-a"
        );
    }

    std::shared_ptr<erhe::scene::Node>          root;
    std::shared_ptr<erhe::scene::Mesh>          mesh;
    std::shared_ptr<erhe::primitive::Primitive> primitive;
    editor::World_space_validity                validity;
};

} // anonymous namespace

TEST_F(World_space_validity_test, a_baked_node_is_valid)
{
    const editor::World_space_form_status status = validity.get_status();
    EXPECT_TRUE(status.baked);
    EXPECT_EQ(status.admitted_count, 1u);
    EXPECT_EQ(status.valid_count, 1u);
    EXPECT_EQ(status.missing_count, 0u);
    EXPECT_TRUE(status.is_available());
}

// R8.6: the move invalidates at its hook; moving back (an undo) makes it valid again.
TEST_F(World_space_validity_test, a_move_flips_validity_at_the_transform_hook)
{
    mesh->set_parent_from_node(translated(glm::vec3{1.0f, 2.0f, 4.0f}));
    EXPECT_TRUE(validity.get_status().is_available()); // nothing changes until the hook runs
    validity.on_mesh_transform_changed(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 1u);
    EXPECT_FALSE(validity.get_status().is_available());
    mesh->set_parent_from_node(translated(glm::vec3{1.0f, 2.0f, 3.0f}));
    validity.on_mesh_transform_changed(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 0u);
    EXPECT_TRUE(validity.get_status().is_available());
}

// The key covers the world transform's bits (R4.7): a moved scene root moves every node.
TEST_F(World_space_validity_test, a_moved_root_invalidates_and_moving_it_back_restores)
{
    root->set_parent_from_node(translated(glm::vec3{0.0f, 0.0f, 8.0f}));
    validity.on_mesh_transform_changed(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 1u);
    root->set_parent_from_node(erhe::scene::Trs_transform{});
    validity.on_mesh_transform_changed(*mesh);
    EXPECT_TRUE(validity.get_status().is_available());
}

// A mesh edit swaps the primitive; the undo restores the original one.
TEST_F(World_space_validity_test, a_primitive_swap_flips_validity_at_the_primitives_hook)
{
    const std::shared_ptr<erhe::primitive::Primitive> edited = make_primitive();
    mesh->set_primitives({erhe::scene::Mesh_primitive{edited}});
    validity.on_mesh_primitives_changed(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 1u);
    mesh->set_primitives({erhe::scene::Mesh_primitive{primitive}});
    validity.on_mesh_primitives_changed(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 0u);
    EXPECT_TRUE(validity.get_status().is_available());
}

TEST_F(World_space_validity_test, scene_and_settings_changes_end_availability)
{
    const std::shared_ptr<erhe::scene::Mesh> proxy = std::make_shared<erhe::scene::Mesh>("proxy");
    proxy->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::render_proxy);
    validity.on_mesh_registered(*proxy);
    EXPECT_TRUE(validity.get_status().is_available());

    const std::shared_ptr<erhe::scene::Mesh> added = std::make_shared<erhe::scene::Mesh>("added");
    added->enable_flag_bits(erhe::Item_flags::content);
    validity.on_mesh_registered(*added);
    EXPECT_TRUE(validity.get_status().scene_changed);
    EXPECT_FALSE(validity.get_status().is_available());

    validity.on_export_settings_changed("settings-b");
    EXPECT_TRUE(validity.get_status().settings_changed);
    validity.clear();
    EXPECT_FALSE(validity.get_status().baked);
}

// R3.3: undoing a mesh add or remove, or setting a value back, makes the form available again.
TEST_F(World_space_validity_test, undone_scene_and_settings_changes_restore_availability)
{
    const std::shared_ptr<erhe::scene::Mesh> added = std::make_shared<erhe::scene::Mesh>("added");
    added->enable_flag_bits(erhe::Item_flags::content);
    validity.on_mesh_registered(*added);
    EXPECT_FALSE(validity.get_status().is_available());
    validity.on_mesh_unregistered(*added);
    EXPECT_TRUE(validity.get_status().is_available());

    validity.on_mesh_unregistered(*mesh);
    EXPECT_TRUE(validity.get_status().scene_changed);
    // Out of the scene the mesh moves (its hook says so) and comes back with no hook; registering reads it again.
    mesh->set_parent_from_node(translated(glm::vec3{5.0f, 2.0f, 3.0f}));
    validity.on_mesh_transform_changed(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 1u);
    mesh->set_parent_from_node(translated(glm::vec3{1.0f, 2.0f, 3.0f}));
    validity.on_mesh_registered(*mesh);
    EXPECT_EQ(validity.get_status().missing_count, 0u);
    EXPECT_FALSE(validity.get_status().scene_changed);
    EXPECT_TRUE(validity.get_status().is_available());

    validity.on_export_settings_changed("settings-b");
    EXPECT_TRUE(validity.get_status().settings_changed);
    validity.on_export_settings_changed("settings-a");
    EXPECT_FALSE(validity.get_status().settings_changed);
    EXPECT_TRUE(validity.get_status().is_available());
}
