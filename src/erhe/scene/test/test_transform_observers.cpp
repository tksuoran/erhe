// Transform observers (doc/erhe/scene.md "Transform observers"): a part that
// follows one prim's world transform without being an item in the scene
// subscribes with Xformable::add_transform_observer and keeps the token.

#include "erhe_scene/node.hpp"
#include "erhe_scene/transform_observer.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <memory>

namespace {

using erhe::scene::Node;
using erhe::scene::Transform_observer_token;
using erhe::scene::Xform;

[[nodiscard]] auto make_node(const char* name) -> std::shared_ptr<Xform>
{
    return std::make_shared<Xform>(name);
}

void move(const std::shared_ptr<Xform>& node, const float x)
{
    node->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{x, 0.0f, 0.0f}));
}

} // anonymous namespace

TEST(Transform_observers, callback_runs_on_every_transform_write)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int        count       = 0;
    Node*      last_node   = nullptr;
    Transform_observer_token token = node->add_transform_observer(
        [&](Node& observed) {
            ++count;
            last_node = &observed;
        }
    );
    EXPECT_TRUE(token.is_active());
    move(node, 1.0f);
    move(node, 2.0f);
    EXPECT_EQ(count, 2);
    EXPECT_EQ(last_node, node.get());
}

TEST(Transform_observers, token_destruction_and_release_unsubscribe)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int count = 0;
    {
        Transform_observer_token token = node->add_transform_observer([&](Node&) { ++count; });
        move(node, 1.0f);
    }
    move(node, 2.0f);
    EXPECT_EQ(count, 1);

    Transform_observer_token token = node->add_transform_observer([&](Node&) { ++count; });
    move(node, 3.0f);
    token.release();
    EXPECT_FALSE(token.is_active());
    move(node, 4.0f);
    EXPECT_EQ(count, 2);
}

TEST(Transform_observers, token_moves)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int count = 0;
    Transform_observer_token a = node->add_transform_observer([&](Node&) { ++count; });
    Transform_observer_token b = std::move(a);
    EXPECT_FALSE(a.is_active());
    EXPECT_TRUE(b.is_active());
    move(node, 1.0f);
    EXPECT_EQ(count, 1);
}

TEST(Transform_observers, several_observers_on_one_node)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int first = 0;
    int second = 0;
    Transform_observer_token token_a = node->add_transform_observer([&](Node&) { ++first; });
    Transform_observer_token token_b = node->add_transform_observer([&](Node&) { ++second; });
    move(node, 1.0f);
    token_a.release();
    move(node, 2.0f);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
}

TEST(Transform_observers, token_outliving_its_node_is_a_no_op)
{
    Transform_observer_token token;
    {
        const std::shared_ptr<Xform> node = make_node("node");
        token = node->add_transform_observer([](Node&) {});
        EXPECT_TRUE(token.is_active());
    }
    EXPECT_FALSE(token.is_active());
    token.release(); // must not touch the freed list
}

TEST(Transform_observers, a_callback_may_release_its_own_token)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int count = 0;
    std::shared_ptr<Transform_observer_token> token = std::make_shared<Transform_observer_token>();
    *token = node->add_transform_observer(
        [&count, token](Node&) {
            ++count;
            token->release();
        }
    );
    move(node, 1.0f);
    move(node, 2.0f);
    EXPECT_EQ(count, 1);
}

TEST(Transform_observers, an_observer_added_by_a_callback_is_called_by_the_next_notification)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int outer = 0;
    int inner = 0;
    Transform_observer_token inner_token;
    Transform_observer_token outer_token = node->add_transform_observer(
        [&](Node& observed) {
            ++outer;
            if (!inner_token.is_active()) {
                inner_token = observed.add_transform_observer([&](Node&) { ++inner; });
            }
        }
    );
    move(node, 1.0f);
    EXPECT_EQ(outer, 1);
    EXPECT_EQ(inner, 0);
    move(node, 2.0f);
    EXPECT_EQ(outer, 2);
    EXPECT_EQ(inner, 1);
}

// The propagation pass recomputes each descendant's world transform with
// update_transform(); the observer of a descendant runs from there, which is
// how a subscriber follows a prim its ancestor moved.
TEST(Transform_observers, the_propagation_pass_reaches_a_descendant_observer)
{
    const std::shared_ptr<Xform> parent = make_node("parent");
    const std::shared_ptr<Xform> child  = make_node("child");
    child->set_parent(parent);
    int count = 0;
    Transform_observer_token token = child->add_transform_observer([&](Node&) { ++count; });
    move(parent, 1.0f);
    child->update_transform(0);
    EXPECT_EQ(count, 1);
}

TEST(Transform_observers, a_clone_gets_no_observers)
{
    const std::shared_ptr<Xform> node = make_node("node");
    int count = 0;
    Transform_observer_token token = node->add_transform_observer([&](Node&) { ++count; });
    const std::shared_ptr<erhe::Item_base> clone_item = node->clone();
    const std::shared_ptr<Xform> clone = std::dynamic_pointer_cast<Xform>(clone_item);
    ASSERT_TRUE(static_cast<bool>(clone));
    move(clone, 1.0f);
    EXPECT_EQ(count, 0);
    move(node, 2.0f);
    EXPECT_EQ(count, 1);
}
