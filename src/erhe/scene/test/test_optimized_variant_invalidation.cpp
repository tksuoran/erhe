// A live GPU edit must make the optimized mesh variant go away, and must
// re-register EVERY mesh that shares the edited Primitive.
//
// Live edits (vertex drag, paint, weight paint) write the ORIGINAL buffer mesh,
// because Element_mappings describe only that build and the optimized build's
// welded corners cannot express a per-corner edit at all. A mesh left drawing
// the optimized variant would show nothing change until commit rebuilt the
// primitive - silently, and only when mesh optimization is enabled.
//
// Re-registering only the edited mesh is the subtler half: a Primitive is shared
// (glTF instances, brush instances), draw list records bake the drawn variant's
// base_vertex and index ranges, and a sharer left unregistered keeps drawing
// from ranges the drop retires and the pool later reuses.

#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

class Recording_scene_host : public erhe::scene::Scene_host
{
public:
    Recording_scene_host()
        : scene{"test scene", this}
    {
        // A bare Scene has no mesh layers; the sharer scan walks them, so the
        // fixture has to provide one the way the editor's Scene_root does.
        scene.add_mesh_layer(std::make_shared<erhe::scene::Mesh_layer>("content", 0u, erhe::scene::Layer_id{0}));
    }

    auto get_host_name   () const -> const char*        override { return "Recording_scene_host"; }
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
    void register_layout  (const std::shared_ptr<erhe::scene::Layout>&)        override {}
    void unregister_layout(const std::shared_ptr<erhe::scene::Layout>&)        override {}

    void on_mesh_primitives_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh) override
    {
        reregistered.push_back(mesh.get());
    }
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>&, uint64_t, uint64_t) override {}
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene                scene;
    std::vector<erhe::scene::Mesh*>   reregistered;
};

// A Primitive carrying both builds. Neither Buffer_mesh has GPU allocations -
// nothing here touches a device - which is enough: the contract under test is
// about which shape the Primitive offers, not about its contents.
[[nodiscard]] auto make_optimized_primitive() -> std::shared_ptr<erhe::primitive::Primitive>
{
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(
        erhe::primitive::Buffer_mesh{}
    );
    primitive->optimized_render_shape = std::make_shared<erhe::primitive::Primitive_render_shape>(
        erhe::primitive::Buffer_mesh{}
    );
    return primitive;
}

// A mesh hosted by `host`, carrying `primitive`, attached to a node so that the
// item host resolves.
[[nodiscard]] auto make_hosted_mesh(
    Recording_scene_host&                              host,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive
) -> std::shared_ptr<erhe::scene::Mesh>
{
    std::shared_ptr<erhe::scene::Node> node = std::make_shared<erhe::scene::Xform>("node");
    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->add_primitive(primitive, {});
    erhe::scene::set_mesh_parent(mesh, node);
    node->set_parent(host.scene.get_root_node());
    host.scene.get_mesh_layers().front()->add(mesh);
    return mesh;
}

} // anonymous namespace

TEST(optimized_variant_invalidation, drops_the_optimized_shape)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh>          mesh      = make_hosted_mesh(host, primitive);
    ASSERT_TRUE(primitive->optimized_render_shape);

    host.reregistered.clear();
    mesh->invalidate_optimized_primitive_variant(0);

    EXPECT_FALSE(primitive->optimized_render_shape);
    EXPECT_TRUE (primitive->render_shape); // the source build is never touched
}

TEST(optimized_variant_invalidation, re_registers_every_mesh_sharing_the_primitive)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> shared_primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh> edited  = make_hosted_mesh(host, shared_primitive);
    const std::shared_ptr<erhe::scene::Mesh> sharer  = make_hosted_mesh(host, shared_primitive);
    // A mesh with its own primitive must NOT be re-registered: its draw list
    // records still describe a shape nothing dropped.
    const std::shared_ptr<erhe::scene::Mesh> unrelated = make_hosted_mesh(host, make_optimized_primitive());

    host.reregistered.clear();
    edited->invalidate_optimized_primitive_variant(0);

    EXPECT_EQ(std::count(host.reregistered.begin(), host.reregistered.end(), edited.get()),  1);
    EXPECT_EQ(std::count(host.reregistered.begin(), host.reregistered.end(), sharer.get()),  1);
    EXPECT_EQ(std::count(host.reregistered.begin(), host.reregistered.end(), unrelated.get()), 0);
}

TEST(optimized_variant_invalidation, is_idempotent_and_silent_without_a_variant)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh>          mesh      = make_hosted_mesh(host, primitive);

    mesh->invalidate_optimized_primitive_variant(0);
    host.reregistered.clear();

    // Every later write of the same edit stroke lands here, and so does every
    // call at all while mesh optimization is off. It must cost nothing and must
    // not churn the draw lists.
    mesh->invalidate_optimized_primitive_variant(0);
    EXPECT_TRUE(host.reregistered.empty());
}

TEST(optimized_variant_invalidation, ignores_an_out_of_range_primitive_index)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh>          mesh      = make_hosted_mesh(host, primitive);

    host.reregistered.clear();
    mesh->invalidate_optimized_primitive_variant(7);

    EXPECT_TRUE(primitive->optimized_render_shape); // untouched
    EXPECT_TRUE(host.reregistered.empty());
}

// Requirement 11: the edit bracket. begin drops the variant, takes an
// optimization hold and hands back the held Primitive; while held,
// publish_optimized_render_shape() refuses, so a build finishing mid-edit (a
// deferred finalize) cannot put a pre-edit variant beside the in-progress
// edit. release_optimization_hold() on the held object re-enables publish.
TEST(optimized_variant_edit, begin_drops_and_blocks_publish_until_release)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh>          mesh      = make_hosted_mesh(host, primitive);
    ASSERT_TRUE(primitive->optimized_render_shape);

    host.reregistered.clear();
    const std::shared_ptr<erhe::primitive::Primitive> held = mesh->begin_optimized_variant_edit(0);
    ASSERT_EQ(held, primitive);
    EXPECT_FALSE(primitive->optimized_render_shape);
    EXPECT_EQ(std::count(host.reregistered.begin(), host.reregistered.end(), mesh.get()), 1);

    // A build landing mid-edit is refused and dropped.
    primitive->publish_optimized_render_shape(
        std::make_shared<erhe::primitive::Primitive_render_shape>(erhe::primitive::Buffer_mesh{})
    );
    EXPECT_FALSE(primitive->optimized_render_shape);

    held->release_optimization_hold();
    primitive->publish_optimized_render_shape(
        std::make_shared<erhe::primitive::Primitive_render_shape>(erhe::primitive::Buffer_mesh{})
    );
    EXPECT_TRUE(primitive->optimized_render_shape);
}

TEST(optimized_variant_edit, begin_holds_even_without_a_live_variant)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh>          mesh      = make_hosted_mesh(host, primitive);
    primitive->optimized_render_shape.reset();

    // The hold is what blocks a concurrent finalize, so it must be taken even
    // when there is nothing to drop.
    const std::shared_ptr<erhe::primitive::Primitive> held = mesh->begin_optimized_variant_edit(0);
    ASSERT_EQ(held, primitive);
    primitive->publish_optimized_render_shape(
        std::make_shared<erhe::primitive::Primitive_render_shape>(erhe::primitive::Buffer_mesh{})
    );
    EXPECT_FALSE(primitive->optimized_render_shape);
    held->release_optimization_hold();
}

TEST(optimized_variant_edit, nested_holds_release_in_pairs)
{
    Recording_scene_host host;
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    const std::shared_ptr<erhe::scene::Mesh>          mesh      = make_hosted_mesh(host, primitive);

    // Instances share Primitives, so two tools can bracket the same one; the
    // publish stays refused until the LAST hold is released.
    const std::shared_ptr<erhe::primitive::Primitive> held_a = mesh->begin_optimized_variant_edit(0);
    const std::shared_ptr<erhe::primitive::Primitive> held_b = mesh->begin_optimized_variant_edit(0);
    held_a->release_optimization_hold();
    primitive->publish_optimized_render_shape(
        std::make_shared<erhe::primitive::Primitive_render_shape>(erhe::primitive::Buffer_mesh{})
    );
    EXPECT_FALSE(primitive->optimized_render_shape);

    held_b->release_optimization_hold();
    primitive->publish_optimized_render_shape(
        std::make_shared<erhe::primitive::Primitive_render_shape>(erhe::primitive::Buffer_mesh{})
    );
    EXPECT_TRUE(primitive->optimized_render_shape);
}

TEST(optimized_variant_edit, publish_null_clears_a_stale_variant)
{
    // The geometry-path commit clears a stale soup-path variant by publishing
    // null; that must keep working outside a hold.
    const std::shared_ptr<erhe::primitive::Primitive> primitive = make_optimized_primitive();
    ASSERT_TRUE(primitive->optimized_render_shape);
    primitive->publish_optimized_render_shape(nullptr);
    EXPECT_FALSE(primitive->optimized_render_shape);
}
