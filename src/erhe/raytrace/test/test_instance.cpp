#include "test_helpers.hpp"

#include <gtest/gtest.h>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

// Helper to set up an instanced scene: geometry in child scene, instance in root scene
class Instance_fixture
{
public:
    Instance_fixture(Test_geometry& tg, const glm::mat4& transform)
        : child_scene {IScene::create_unique("child")}
        , root_scene  {IScene::create_unique("root")}
        , instance    {IInstance::create_unique("inst")}
    {
        child_scene->attach(tg.geometry.get());
        child_scene->commit();

        instance->set_scene(child_scene.get());
        instance->set_transform(transform);
        instance->set_mask(0xffffffffu);
        instance->commit();

        root_scene->attach(instance.get());
        root_scene->commit();
    }

    std::unique_ptr<IScene>    child_scene;
    std::unique_ptr<IScene>    root_scene;
    std::unique_ptr<IInstance> instance;
};

TEST(Instance, IdentityTransform)
{
    Test_geometry tg = make_unit_triangle();
    Instance_fixture fixture{tg, glm::mat4{1.0f}};

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = fixture.root_scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_NEAR(ray.t_far, 1.0f, 0.01f);
    EXPECT_EQ(hit.instance, fixture.instance.get());
}

TEST(Instance, TranslatedInstance)
{
    Test_geometry tg = make_unit_triangle();
    glm::mat4 transform = glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f});
    Instance_fixture fixture{tg, transform};

    // Ray aimed at translated position
    Ray ray = make_ray({5.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = fixture.root_scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_NEAR(ray.t_far, 1.0f, 0.01f);

    // Ray at original position should miss
    Ray miss_ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit miss_hit{};

    EXPECT_FALSE(fixture.root_scene->intersect(miss_ray, miss_hit));
}

TEST(Instance, ScaledInstance)
{
    Test_geometry tg = make_unit_triangle();
    glm::mat4 transform = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f, 2.0f, 2.0f});
    Instance_fixture fixture{tg, transform};

    // Point (1.5, 0.1, 0) is outside unit triangle but inside 2x scaled triangle
    Ray ray = make_ray({1.5f, 0.1f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = fixture.root_scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
}

TEST(Instance, InstanceMask)
{
    Test_geometry tg = make_unit_triangle();
    Instance_fixture fixture{tg, glm::mat4{1.0f}};
    fixture.instance->set_mask(0x04u);
    fixture.root_scene->commit();

    // Ray mask matches instance mask
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        ray.mask = 0x04u;
        Hit hit{};
        EXPECT_TRUE(fixture.root_scene->intersect(ray, hit));
    }

    // Ray mask does NOT match instance mask
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        ray.mask = 0x08u;
        Hit hit{};
        EXPECT_FALSE(fixture.root_scene->intersect(ray, hit));
    }
}

TEST(Instance, DisabledInstance)
{
    Test_geometry tg = make_unit_triangle();
    Instance_fixture fixture{tg, glm::mat4{1.0f}};
    fixture.instance->disable();
    fixture.root_scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    EXPECT_FALSE(fixture.root_scene->intersect(ray, hit));
}

TEST(Instance, MultipleInstances)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());
    child_scene->commit();

    // Instance at z=0 (near)
    auto near_instance = IInstance::create_unique("near");
    near_instance->set_scene(child_scene.get());
    near_instance->set_transform(glm::mat4{1.0f});
    near_instance->set_mask(0xffffffffu);
    near_instance->commit();

    // Instance at z=-3 (far)
    glm::mat4 far_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, -3.0f});
    auto far_instance = IInstance::create_unique("far");
    far_instance->set_scene(child_scene.get());
    far_instance->set_transform(far_transform);
    far_instance->set_mask(0xffffffffu);
    far_instance->commit();

    auto root_scene = IScene::create_unique("root");
    root_scene->attach(near_instance.get());
    root_scene->attach(far_instance.get());
    root_scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 5.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = root_scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    // Should hit the near instance (at z=0, distance=5) not the far one (at z=-3, distance=8)
    EXPECT_NEAR(ray.t_far, 5.0f, 0.01f);
    EXPECT_EQ(hit.instance, near_instance.get());
}

} // anonymous namespace
