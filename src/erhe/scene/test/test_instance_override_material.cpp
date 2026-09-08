// A material binding as an override of a prefab instance
// (doc/usd-compatibility-plan.md X2): a primitive whose material differs from
// the counterpart's is collected as a binding, and a collected binding put
// back on a fresh instance reaches the primitive it names.

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

// The instance one carrier holds: a clone of a template widget whose mesh
// carries `primitive_count` groups of facets, with the materials of the scene
// beside it.
class Instance_scene final
{
public:
    explicit Instance_scene(const std::size_t primitive_count)
    {
        root = std::make_shared<erhe::scene::Xform>("root");

        materials = std::make_shared<erhe::scene::Xform>("Materials");
        materials->set_parent(root);
        grey = std::make_shared<erhe::primitive::Material>("grey");
        grey->set_parent(materials);
        red = std::make_shared<erhe::primitive::Material>("red");
        red->set_parent(materials);
        base = std::make_shared<erhe::primitive::Material>("base");

        carrier = std::make_shared<erhe::scene::Xform>("Carrier");
        carrier->set_parent(root);

        template_widget = std::make_shared<erhe::scene::Xform>("Widget");
        clone_widget    = std::make_shared<erhe::scene::Xform>("Widget");
        clone_widget->set_reference(template_widget);
        clone_widget->set_parent(carrier);

        template_plate = std::make_shared<erhe::scene::Mesh>("plate");
        clone_plate    = std::make_shared<erhe::scene::Mesh>("plate");
        for (std::size_t index = 0; index < primitive_count; ++index) {
            const std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(
                (primitive_count == 1) ? "plate" : ((index == 0) ? "plate.front" : "plate.back")
            );
            template_plate->add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry), base);
            clone_plate->add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry), base);
        }
        clone_plate->set_reference(template_plate);
        clone_plate->set_parent(clone_widget);
    }

    std::shared_ptr<erhe::scene::Xform>        root;
    std::shared_ptr<erhe::scene::Xform>        materials;
    std::shared_ptr<erhe::scene::Xform>        carrier;
    std::shared_ptr<erhe::scene::Xform>        template_widget;
    std::shared_ptr<erhe::scene::Xform>        clone_widget;
    std::shared_ptr<erhe::scene::Mesh>         template_plate;
    std::shared_ptr<erhe::scene::Mesh>         clone_plate;
    std::shared_ptr<erhe::primitive::Material> grey;
    std::shared_ptr<erhe::primitive::Material> red;
    std::shared_ptr<erhe::primitive::Material> base;
};

[[nodiscard]] auto find_override(
    const std::vector<erhe::scene::Instance_override>& overrides,
    const std::string&                                 relative_path
) -> const erhe::scene::Instance_override*
{
    for (const erhe::scene::Instance_override& entry : overrides) {
        if (entry.relative_path == relative_path) {
            return &entry;
        }
    }
    return nullptr;
}

} // anonymous namespace

TEST(Instance_override_material, a_mesh_that_binds_the_same_material_holds_no_override)
{
    const Instance_scene scene{1};
    EXPECT_TRUE(erhe::scene::collect_instance_overrides(*scene.carrier.get()).empty());
}

TEST(Instance_override_material, a_rebound_mesh_is_collected_by_the_path_of_its_material)
{
    const Instance_scene scene{1};
    scene.clone_plate->set_primitive_material(0, scene.grey);

    const std::vector<erhe::scene::Instance_override> overrides =
        erhe::scene::collect_instance_overrides(*scene.carrier.get());
    const erhe::scene::Instance_override* plate = find_override(overrides, "plate");
    ASSERT_NE(plate, nullptr);
    EXPECT_EQ(plate->material_path, "Materials/grey");
}

TEST(Instance_override_material, a_rebound_group_of_facets_is_collected_below_its_mesh)
{
    const Instance_scene scene{2};
    scene.clone_plate->set_primitive_material(1, scene.red);

    const std::vector<erhe::scene::Instance_override> overrides =
        erhe::scene::collect_instance_overrides(*scene.carrier.get());
    const erhe::scene::Instance_override* back = find_override(overrides, "plate/back");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->material_path, "Materials/red");
    EXPECT_TRUE(back->values.empty());
}

TEST(Instance_override_material, a_collected_binding_reaches_every_primitive_of_the_mesh)
{
    const Instance_scene scene{2};
    const std::vector<erhe::scene::Instance_override> overrides{
        erhe::scene::Instance_override{.relative_path = "plate", .material_path = "Materials/grey"}
    };
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), overrides);
    EXPECT_EQ(scene.clone_plate->get_primitives()[0].material, scene.grey);
    EXPECT_EQ(scene.clone_plate->get_primitives()[1].material, scene.grey);
}

TEST(Instance_override_material, a_binding_that_names_a_group_reaches_that_group_alone)
{
    const Instance_scene scene{2};
    const std::vector<erhe::scene::Instance_override> overrides{
        erhe::scene::Instance_override{.relative_path = "plate/back", .material_path = "Materials/red"}
    };
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), overrides);
    EXPECT_EQ(scene.clone_plate->get_primitives()[0].material, scene.base);
    EXPECT_EQ(scene.clone_plate->get_primitives()[1].material, scene.red);
}

// A path a USD file authors starts at the arc's target prim, which erhe keeps
// as one level below the carrier (X1), so the level is transparent to the
// path a binding names.
TEST(Instance_override_material, a_path_through_a_target_clone_resolves)
{
    const Instance_scene scene{1};
    const std::shared_ptr<erhe::primitive::Material> inside = std::make_shared<erhe::primitive::Material>("inside");
    inside->set_reference(scene.base);
    inside->set_parent(scene.clone_widget);

    const std::vector<erhe::scene::Instance_override> overrides{
        erhe::scene::Instance_override{.relative_path = "plate", .material_path = "/Carrier/inside"}
    };
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), overrides);
    EXPECT_EQ(scene.clone_plate->get_primitives()[0].material, inside);
}

TEST(Instance_override_material, a_binding_that_names_no_material_leaves_the_mesh_alone)
{
    const Instance_scene scene{1};
    const std::vector<erhe::scene::Instance_override> overrides{
        erhe::scene::Instance_override{.relative_path = "plate", .material_path = "Materials/missing"}
    };
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), overrides);
    EXPECT_EQ(scene.clone_plate->get_primitives()[0].material, scene.base);
}
