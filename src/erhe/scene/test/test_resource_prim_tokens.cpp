// The scene-owned content-library kinds are typed prims
// (doc/usd-compatibility-plan.md U4): each reports the token its class fixes
// and each composes a path once it is parented, though nothing places them
// in a tree yet.

#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/skin.hpp"

#include <gtest/gtest.h>

#include <memory>

using erhe::scene::Animation;
using erhe::scene::Skin;

TEST(Resource_prim_tokens, animation_and_skin_report_their_tokens)
{
    const std::shared_ptr<Animation> animation = std::make_shared<Animation>("Walk");
    const std::shared_ptr<Skin>      skin      = std::make_shared<Skin>("Body");

    EXPECT_TRUE(erhe::is<erhe::Typed>(animation));
    EXPECT_TRUE(erhe::is<erhe::Typed>(skin));
    EXPECT_TRUE(erhe::is<Animation>(animation));
    EXPECT_TRUE(erhe::is<Skin>(skin));
    EXPECT_EQ(animation->get_prim_type_name(), "Animation");
    EXPECT_EQ(skin     ->get_prim_type_name(), "Skin");
}

TEST(Resource_prim_tokens, an_animation_composes_a_path_under_a_scope)
{
    const std::shared_ptr<erhe::Scope> root      = std::make_shared<erhe::Scope>("root");
    const std::shared_ptr<erhe::Scope> scope     = std::make_shared<erhe::Scope>("Animations");
    const std::shared_ptr<Animation>   animation = std::make_shared<Animation>("Walk");
    scope->set_parent(root);
    animation->set_parent(scope);

    EXPECT_EQ(animation->get_path(), "Animations/Walk");
    EXPECT_EQ(erhe::find_by_path(*root, "Animations/Walk"), animation.get());
}
