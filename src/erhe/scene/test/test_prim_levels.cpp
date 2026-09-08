// The prim class hierarchy of doc/usd-compatibility-plan.md C5 as erhe sees
// it: a level's static type is the OR of its chain, so the subset test
// answers for every level above a concrete class, and a clone of a concrete
// prim is that concrete class.

#include "erhe_scene/boundable.hpp"
#include "erhe_scene/gprim.hpp"
#include "erhe_scene/imageable.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/point_instancer.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"

#include <gtest/gtest.h>

#include <memory>

TEST(Prim_levels, static_types_compose_along_the_chain)
{
    EXPECT_EQ(erhe::scene::Imageable::get_static_type(), erhe::Item_type::typed | erhe::Item_type::imageable);
    EXPECT_EQ(
        erhe::scene::Xformable::get_static_type(),
        erhe::Item_type::typed | erhe::Item_type::imageable | erhe::Item_type::xformable
    );
    EXPECT_EQ(
        erhe::scene::Xform::get_static_type(),
        erhe::scene::Xformable::get_static_type() | erhe::Item_type::xform
    );
    EXPECT_EQ(
        erhe::scene::Boundable::get_static_type(),
        erhe::scene::Xformable::get_static_type() | erhe::Item_type::boundable
    );
    EXPECT_EQ(
        erhe::scene::Gprim::get_static_type(),
        erhe::scene::Boundable::get_static_type() | erhe::Item_type::gprim
    );
}

TEST(Prim_levels, an_xform_is_every_level_above_it)
{
    std::shared_ptr<erhe::scene::Xform> xform = std::make_shared<erhe::scene::Xform>("x");
    EXPECT_TRUE (erhe::is<erhe::scene::Xform>    (xform));
    EXPECT_TRUE (erhe::is<erhe::scene::Xformable>(xform));
    EXPECT_TRUE (erhe::is<erhe::scene::Imageable>(xform));
    EXPECT_TRUE (erhe::is<erhe::Typed>           (xform));
    EXPECT_FALSE(erhe::is<erhe::Scope>           (xform));
    EXPECT_FALSE(erhe::is<erhe::scene::Boundable>(xform));
    EXPECT_EQ(xform->get_type_name(), "Xform");
}

TEST(Prim_levels, type_name_token_is_the_class_token)
{
    std::shared_ptr<erhe::scene::Xform> xform = std::make_shared<erhe::scene::Xform>("x");
    EXPECT_EQ(xform->get_class_type_name(), "Xform");
    EXPECT_EQ(xform->get_prim_type_name(), "Xform");
    xform->set_prim_type_name("Cube");
    EXPECT_EQ(xform->get_prim_type_name(), "Xform");
}

TEST(Prim_levels, cloning_an_xform_produces_an_xform)
{
    std::shared_ptr<erhe::scene::Xform> xform = std::make_shared<erhe::scene::Xform>("x");
    std::shared_ptr<erhe::scene::Xform> child = std::make_shared<erhe::scene::Xform>("child");
    child->set_parent(xform);

    std::shared_ptr<erhe::Item_base>    clone_item = xform->clone();
    std::shared_ptr<erhe::scene::Xform> clone      = std::dynamic_pointer_cast<erhe::scene::Xform>(clone_item);
    ASSERT_TRUE(clone.operator bool());
    EXPECT_EQ(clone->get_type(), erhe::scene::Xform::get_static_type());
    EXPECT_EQ(clone->get_name(), "x");
    ASSERT_EQ(clone->get_child_count(), 1);
    EXPECT_TRUE(std::dynamic_pointer_cast<erhe::scene::Xform>(clone->get_children().front()).operator bool());
}

// A point instancer is a boundable prim (doc/usd-compatibility-plan.md S1):
// it carries a transform, it is written as its USD schema token, and the one
// array it holds is the prototype each instance uses.
TEST(Prim_levels, a_point_instancer_is_a_boundable_prim)
{
    std::shared_ptr<erhe::scene::Point_instancer> instancer =
        std::make_shared<erhe::scene::Point_instancer>("scatter");
    EXPECT_EQ(
        erhe::scene::Point_instancer::get_static_type(),
        erhe::scene::Boundable::get_static_type() | erhe::Item_type::point_instancer
    );
    EXPECT_TRUE (erhe::is<erhe::scene::Boundable>(instancer));
    EXPECT_TRUE (erhe::is<erhe::scene::Xformable>(instancer));
    EXPECT_FALSE(erhe::is<erhe::scene::Gprim>    (instancer));
    EXPECT_EQ(instancer->get_type_name(), "Point_instancer");
    EXPECT_EQ(instancer->get_class_type_name(), "PointInstancer");
}

TEST(Prim_levels, cloning_a_point_instancer_produces_a_point_instancer)
{
    std::shared_ptr<erhe::scene::Point_instancer> instancer =
        std::make_shared<erhe::scene::Point_instancer>("scatter");
    std::shared_ptr<erhe::Item_base> clone = instancer->clone();
    ASSERT_TRUE(std::dynamic_pointer_cast<erhe::scene::Point_instancer>(clone).operator bool());
    EXPECT_EQ(clone->get_name(), "scatter");
}
