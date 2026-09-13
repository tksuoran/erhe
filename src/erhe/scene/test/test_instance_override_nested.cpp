// An override path that crosses a carrier deeper inside an instance
// (doc/usd-compatibility-plan.md C6, X1): erhe keeps the clone of an
// arc's target as one level of its own below the carrier, while a file's own
// path composes the target's content directly under the referencing prim. The
// level is transparent at every carrier the path crosses, not only at the one
// the path starts at.

#include "erhe_item/item.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

// What the editor applies to a prim a composition arc was authored on, as
// erhe::scene sees it: an attachment carrying the `prefab_instance` type bit.
class Test_arc final : public erhe::Item<erhe::Item_base, erhe::scene::Node_attachment, Test_arc>
{
public:
    Test_arc() : Item{"arc"} {}
    Test_arc(const Test_arc& src, erhe::for_clone) : Item{src} {}

    static constexpr std::string_view static_type_name{"Test_arc"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::prefab_instance;
    }
};

// The shape the intent-vfx teapot asset has: a carrier holds the clone of its
// target, that clone holds a plain prim `geo`, and below it `default` is a
// carrier of its own holding the clone `UtahTeapot`, whose child `Body` is
// what the file's path `geo/default/Body` names.
class Nested_instance_scene final
{
public:
    Nested_instance_scene()
    {
        root = std::make_shared<erhe::scene::Xform>("root");

        carrier = std::make_shared<erhe::scene::Xform>("teapot");
        carrier->set_parent(root);
        carrier->attach(std::make_shared<Test_arc>());

        outer_template = std::make_shared<erhe::scene::Xform>("teapot");
        outer_clone    = std::make_shared<erhe::scene::Xform>("teapot");
        outer_clone->set_reference(outer_template);
        outer_clone->set_parent(carrier);

        geo = std::make_shared<erhe::scene::Xform>("geo");
        geo->set_parent(outer_clone);

        inner_carrier = std::make_shared<erhe::scene::Xform>("default");
        inner_carrier->set_parent(geo);
        inner_carrier->attach(std::make_shared<Test_arc>());

        beside = std::make_shared<erhe::scene::Xform>("Beside");
        beside->set_parent(inner_carrier);

        inner_clone = std::make_shared<erhe::scene::Xform>("UtahTeapot");
        inner_clone->set_parent(inner_carrier);

        body = std::make_shared<erhe::scene::Xform>("Body");
        body->set_parent(inner_clone);
    }

    std::shared_ptr<erhe::scene::Xform> root;
    std::shared_ptr<erhe::scene::Xform> carrier;
    std::shared_ptr<erhe::scene::Xform> outer_template;
    std::shared_ptr<erhe::scene::Xform> outer_clone;
    std::shared_ptr<erhe::scene::Xform> geo;
    std::shared_ptr<erhe::scene::Xform> inner_carrier;
    std::shared_ptr<erhe::scene::Xform> inner_clone;
    std::shared_ptr<erhe::scene::Xform> beside;
    std::shared_ptr<erhe::scene::Xform> body;
};

// The shape a chain of files that each reference the next composes: the
// carrier holds the clone of its target, and that clone is a carrier itself -
// the target prim authored an arc of its own - so the prim the file's path
// names sits two clone levels down. The Teapot asset is this: DrawModes
// references Teapot.usd, whose prim payloads Teapot_Payload.usd, whose prim
// references Teapot_Geometry.usd, whose selected variant references
// geo/UtahTeapot.usd, and the path `Geometry` names the mesh inside the last
// of them.
class Chained_carrier_scene final
{
public:
    Chained_carrier_scene()
    {
        root = std::make_shared<erhe::scene::Xform>("root");

        carrier = std::make_shared<erhe::scene::Xform>("Teapot");
        carrier->set_parent(root);
        carrier->attach(std::make_shared<Test_arc>());

        outer_template = std::make_shared<erhe::scene::Xform>("Teapot");
        outer_clone    = std::make_shared<erhe::scene::Xform>("Teapot");
        outer_clone->set_reference(outer_template);
        outer_clone->set_parent(carrier);
        outer_clone->attach(std::make_shared<Test_arc>());

        inner_template = std::make_shared<erhe::scene::Xform>("UtahTeapot");
        inner_clone    = std::make_shared<erhe::scene::Xform>("UtahTeapot");
        inner_clone->set_reference(inner_template);
        inner_clone->set_parent(outer_clone);

        geometry = std::make_shared<erhe::scene::Xform>("Geometry");
        geometry->set_parent(inner_clone);
    }

    std::shared_ptr<erhe::scene::Xform> root;
    std::shared_ptr<erhe::scene::Xform> carrier;
    std::shared_ptr<erhe::scene::Xform> outer_template;
    std::shared_ptr<erhe::scene::Xform> outer_clone;
    std::shared_ptr<erhe::scene::Xform> inner_template;
    std::shared_ptr<erhe::scene::Xform> inner_clone;
    std::shared_ptr<erhe::scene::Xform> geometry;
};

[[nodiscard]] auto visibility_override(const std::string& relative_path) -> std::vector<erhe::scene::Instance_override>
{
    return std::vector<erhe::scene::Instance_override>{
        erhe::scene::Instance_override{
            .relative_path = relative_path,
            .values        = {erhe::scene::Instance_override_value{.name = "visible", .text = "false"}}
        }
    };
}

} // anonymous namespace

TEST(Instance_override_nested, a_path_that_crosses_a_nested_carrier_resolves)
{
    const Nested_instance_scene scene;
    ASSERT_TRUE(scene.body->is_visible());

    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("geo/default/Body"));
    EXPECT_FALSE(scene.body->is_visible());
}

TEST(Instance_override_nested, a_path_that_crosses_no_nested_carrier_still_resolves)
{
    const Nested_instance_scene scene;
    ASSERT_TRUE(scene.geo->is_visible());

    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("geo"));
    EXPECT_FALSE(scene.geo->is_visible());
}

// The referencing prim and the clone of its target are one prim in the
// composed stage, so a prim authored beside the clone has the same composed
// path as one inside it and the file's path reaches it too.
TEST(Instance_override_nested, a_path_naming_a_child_of_the_nested_carrier_itself_resolves)
{
    const Nested_instance_scene scene;
    ASSERT_TRUE(scene.beside->is_visible());

    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("geo/default/Beside"));
    EXPECT_FALSE(scene.beside->is_visible());
}

TEST(Instance_override_nested, a_path_naming_nothing_reaches_no_item)
{
    const Nested_instance_scene scene;
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("geo/default/Missing"));
    EXPECT_TRUE(scene.body->is_visible());
    EXPECT_TRUE(scene.beside->is_visible());
    EXPECT_TRUE(scene.inner_clone->is_visible());
}

// The collector spells the path the item tree has, which names the nested
// clone level; that form reaches the item too, so an override erhe wrote
// itself is applied back.
TEST(Instance_override_nested, a_path_that_spells_the_nested_clone_resolves)
{
    const Nested_instance_scene scene;
    ASSERT_TRUE(scene.body->is_visible());

    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("geo/default/UtahTeapot/Body"));
    EXPECT_FALSE(scene.body->is_visible());
}

TEST(Instance_override_nested, a_path_that_crosses_a_carrier_clone_resolves)
{
    const Chained_carrier_scene scene;
    ASSERT_TRUE(scene.geometry->is_visible());

    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("Geometry"));
    EXPECT_FALSE(scene.geometry->is_visible());
}

TEST(Instance_override_nested, a_path_that_spells_both_clone_levels_resolves)
{
    const Chained_carrier_scene scene;
    ASSERT_TRUE(scene.geometry->is_visible());

    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("Teapot/UtahTeapot/Geometry"));
    EXPECT_FALSE(scene.geometry->is_visible());
}

TEST(Instance_override_nested, a_path_naming_nothing_below_a_carrier_clone_reaches_no_item)
{
    const Chained_carrier_scene scene;
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override("Missing"));
    EXPECT_TRUE(scene.geometry->is_visible());
    EXPECT_TRUE(scene.inner_clone->is_visible());
    EXPECT_TRUE(scene.outer_clone->is_visible());
}

// An empty path is the carrier's own clone, unchanged.
TEST(Instance_override_nested, an_empty_path_is_the_carriers_own_clone)
{
    const Nested_instance_scene scene;
    erhe::scene::apply_instance_overrides(*scene.carrier.get(), visibility_override(""));
    EXPECT_FALSE(scene.outer_clone->is_visible());
    EXPECT_TRUE(scene.carrier->is_visible());
}
