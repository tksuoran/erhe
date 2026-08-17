// Tests for the bvh backend scene level acceleration structure. The bvh backend
// is one of several raytrace backends, so the whole file is compiled out when a
// different backend is selected.
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)

#include "test_helpers.hpp"

#include "erhe_raytrace/bvh/bvh_scene.hpp"
#include "erhe_raytrace/raytrace_executor.hpp"

#include <taskflow/taskflow.hpp>

#include <gtest/gtest.h>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

[[nodiscard]] auto as_bvh_scene(IScene* scene) -> Bvh_scene*
{
    return reinterpret_cast<Bvh_scene*>(scene);
}

TEST(Bvh_scene, ChildBecomesStatic)
{
    Test_geometry tg = make_unit_triangle();

    auto  scene     = IScene::create_unique("static_classification");
    auto* bvh_scene = as_bvh_scene(scene.get());
    scene->attach(tg.geometry.get());

    // Freshly attached children are not static yet.
    EXPECT_EQ(bvh_scene->get_static_child_count(4), 0);

    for (int i = 0; i < 4; ++i) {
        scene->commit();
    }
    EXPECT_EQ(bvh_scene->get_tick(), 4);
    EXPECT_EQ(bvh_scene->get_static_child_count(4), 1);
}

TEST(Bvh_scene, InstanceTransformResetsStaticness)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());
    child_scene->commit();

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());

    auto  root_scene     = IScene::create_unique("root");
    auto* bvh_root_scene = as_bvh_scene(root_scene.get());
    root_scene->attach(instance.get());

    for (int i = 0; i < 4; ++i) {
        root_scene->commit();
    }
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 1);

    instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}));
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 0);

    for (int i = 0; i < 4; ++i) {
        root_scene->commit();
    }
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 1);
}

TEST(Bvh_scene, GeometryCommitResetsStaticnessThroughInstance)
{
    // A geometry commit changes the bounds of the scene holding it, which
    // changes the bounds of the instance in the scene above.
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());

    auto  root_scene     = IScene::create_unique("root");
    auto* bvh_root_scene = as_bvh_scene(root_scene.get());
    root_scene->attach(instance.get());

    for (int i = 0; i < 4; ++i) {
        root_scene->commit();
    }
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 1);

    tg.geometry->commit();
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 0);
}

TEST(Bvh_scene, SceneBoundsFollowChildren)
{
    Test_geometry tg = make_unit_triangle();

    auto  scene     = IScene::create_unique("bounds");
    auto* bvh_scene = as_bvh_scene(scene.get());
    EXPECT_FALSE(bvh_scene->get_bbox().is_valid());

    scene->attach(tg.geometry.get());
    const erhe::math::Aabb bbox = bvh_scene->get_bbox();
    ASSERT_TRUE(bbox.is_valid());
    EXPECT_NEAR(bbox.min.x, 0.0f, 0.001f);
    EXPECT_NEAR(bbox.max.x, 1.0f, 0.001f);
    EXPECT_NEAR(bbox.min.y, 0.0f, 0.001f);
    EXPECT_NEAR(bbox.max.y, 1.0f, 0.001f);
}

TEST(Bvh_scene, InstanceBoundsAreTransformed)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());
    instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f}));

    auto  root_scene     = IScene::create_unique("root");
    auto* bvh_root_scene = as_bvh_scene(root_scene.get());
    root_scene->attach(instance.get());

    const erhe::math::Aabb bbox = bvh_root_scene->get_bbox();
    ASSERT_TRUE(bbox.is_valid());
    EXPECT_NEAR(bbox.min.x, 10.0f, 0.001f);
    EXPECT_NEAR(bbox.max.x, 11.0f, 0.001f);
}

TEST(Bvh_scene, DestroyedChildRemovesItselfFromScene)
{
    auto scene = IScene::create_unique("outliving");
    {
        Test_geometry tg = make_unit_triangle();
        scene->attach(tg.geometry.get());
        scene->commit();

        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }

    // The geometry is gone; the scene must not reference it any more.
    scene->commit();
    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};
    EXPECT_FALSE(scene->intersect(ray, hit));
}

TEST(Bvh_scene, InstanceOutlivingItsSceneDoesNotHit)
{
    Test_geometry tg = make_unit_triangle();

    auto instance   = IInstance::create_unique("inst");
    auto root_scene = IScene::create_unique("root");
    root_scene->attach(instance.get());
    {
        auto child_scene = IScene::create_unique("child");
        child_scene->attach(tg.geometry.get());
        instance->set_scene(child_scene.get());
        root_scene->commit();

        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(root_scene->intersect(ray, hit));
    }

    root_scene->commit();
    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};
    EXPECT_FALSE(root_scene->intersect(ray, hit));
}

// ----------------------------------------------------------------------------
// Scene level BVH
// ----------------------------------------------------------------------------

constexpr std::size_t grid_size = 16;

void tick_until_static(IScene* scene)
{
    for (uint64_t i = 0; i < Bvh_scene::k_static_delay_ticks; ++i) {
        scene->commit();
    }
}

// Unit triangle at x = 2 * index, in the z = -index plane, so that both the
// lateral position and the hit distance are unique per child.
[[nodiscard]] auto make_grid_geometry(const int index) -> Test_geometry
{
    const float x = 2.0f * static_cast<float>(index);
    const float z = -static_cast<float>(index);
    return make_triangle_geometry(
        "grid",
        { {x, 0.0f, z}, {x + 1.0f, 0.0f, z}, {x, 1.0f, z} },
        { {0, 1, 2} }
    );
}

[[nodiscard]] auto make_grid_ray(const int index) -> Ray
{
    const float x = 2.0f * static_cast<float>(index) + 0.25f;
    return make_ray({x, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
}

TEST(Bvh_scene, TlasMatchesLinearTraversal)
{
    std::vector<Test_geometry> geometries;
    auto  scene     = IScene::create_unique("grid");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        geometries.push_back(make_grid_geometry(static_cast<int>(i)));
        scene->attach(geometries.back().geometry.get());
    }
    scene->commit();
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), 0);

    // Reference results from the linear path
    std::vector<float>            linear_t;
    std::vector<const IGeometry*> linear_geometry;
    for (std::size_t i = 0; i < grid_size; ++i) {
        Ray ray = make_grid_ray(static_cast<int>(i));
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit)) << "linear miss for " << i;
        linear_t.push_back(ray.t_far);
        linear_geometry.push_back(hit.geometry);
    }

    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    for (std::size_t i = 0; i < grid_size; ++i) {
        Ray ray = make_grid_ray(static_cast<int>(i));
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit)) << "BVH miss for " << i;
        EXPECT_NEAR(ray.t_far, linear_t[i], 0.001f);
        EXPECT_EQ(hit.geometry, linear_geometry[i]);
    }

    // A ray which misses every child
    {
        Ray ray = make_ray({-100.0f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
}

TEST(Bvh_scene, TlasReturnsClosestHit)
{
    // Several parallel triangles along the ray, attached far to near and near
    // to far. The nearest one has to win in both cases.
    for (int order = 0; order < 2; ++order) {
        std::vector<Test_geometry> geometries;
        auto  scene     = IScene::create_unique("closest");
        auto* bvh_scene = as_bvh_scene(scene.get());

        constexpr int count = 8;
        for (int i = 0; i < count; ++i) {
            const int   index = (order == 0) ? i : (count - 1 - i);
            const float z     = -static_cast<float>(index);
            geometries.push_back(
                make_triangle_geometry(
                    "layer",
                    { {0.0f, 0.0f, z}, {1.0f, 0.0f, z}, {0.0f, 1.0f, z} },
                    { {0, 1, 2} }
                )
            );
            scene->attach(geometries.back().geometry.get());
        }
        tick_until_static(scene.get());
        ASSERT_EQ(bvh_scene->get_tlas_member_count(), count);

        Ray ray = make_ray({0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit));
        EXPECT_NEAR(ray.t_far, 10.0f, 0.001f); // the z = 0 layer
    }
}

TEST(Bvh_scene, TlasWithInstances)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    std::vector<std::unique_ptr<IInstance>> instances;
    auto  scene     = IScene::create_unique("instances");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        auto instance = IInstance::create_unique("inst");
        instance->set_scene(child_scene.get());
        instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f * static_cast<float>(i), 0.0f, 0.0f}));
        scene->attach(instance.get());
        instances.push_back(std::move(instance));
    }
    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    for (std::size_t i = 0; i < grid_size; ++i) {
        Ray ray = make_ray({2.0f * static_cast<float>(i) + 0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit)) << "miss for instance " << i;
        EXPECT_EQ(hit.instance, instances[i].get());
        EXPECT_NEAR(ray.t_far, 10.0f, 0.001f);
    }
}

TEST(Bvh_scene, ModifiedMemberLeavesTlasAndFollowsTheMove)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    std::vector<std::unique_ptr<IInstance>> instances;
    auto  scene     = IScene::create_unique("moving");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        auto instance = IInstance::create_unique("inst");
        instance->set_scene(child_scene.get());
        instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f * static_cast<float>(i), 0.0f, 0.0f}));
        scene->attach(instance.get());
        instances.push_back(std::move(instance));
    }
    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    // Move one member sideways, clear of every other instance. It has to leave
    // the scene level BVH, and rays have to follow it immediately, without
    // waiting for a rebuild.
    instances[0]->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{100.0f, 0.0f, 0.0f}));
    instances[0]->commit();
    EXPECT_LT(bvh_scene->get_tlas_member_count(), grid_size);

    const Ray old_position_ray{make_ray({  0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f})};
    const Ray new_position_ray{make_ray({100.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f})};
    {
        Ray ray = old_position_ray;
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
    {
        Ray ray = new_position_ray;
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit));
        EXPECT_NEAR(ray.t_far, 10.0f, 0.001f);
        EXPECT_EQ(hit.instance, instances[0].get());
    }

    // Once it has been static long enough it is taken back into the BVH.
    tick_until_static(scene.get());
    EXPECT_EQ(bvh_scene->get_tlas_member_count(), grid_size);
    {
        Ray ray = old_position_ray;
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
    {
        Ray ray = new_position_ray;
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit));
        EXPECT_EQ(hit.instance, instances[0].get());
    }
}

TEST(Bvh_scene, DetachedMemberIsNotHit)
{
    std::vector<Test_geometry> geometries;
    auto  scene     = IScene::create_unique("detach");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        geometries.push_back(make_grid_geometry(static_cast<int>(i)));
        scene->attach(geometries.back().geometry.get());
    }
    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    scene->detach(geometries[3].geometry.get());
    {
        Ray ray = make_grid_ray(3);
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
    {
        Ray ray = make_grid_ray(4);
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }

    // And after the BVH has been rebuilt without it
    tick_until_static(scene.get());
    {
        Ray ray = make_grid_ray(3);
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
}

TEST(Bvh_scene, ChildAttachedAfterTlasIsHitImmediately)
{
    std::vector<Test_geometry> geometries;
    auto  scene     = IScene::create_unique("late_attach");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        geometries.push_back(make_grid_geometry(static_cast<int>(i)));
        scene->attach(geometries.back().geometry.get());
    }
    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    // The new child is not static yet, so it is traversed linearly.
    Test_geometry late = make_grid_geometry(static_cast<int>(grid_size) + 4);
    scene->attach(late.geometry.get());
    {
        Ray ray = make_grid_ray(static_cast<int>(grid_size) + 4);
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit));
        EXPECT_EQ(hit.geometry, late.geometry.get());
    }
    EXPECT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    // ... and joins the BVH once it has settled.
    tick_until_static(scene.get());
    EXPECT_EQ(bvh_scene->get_tlas_member_count(), grid_size + 1);
    {
        Ray ray = make_grid_ray(static_cast<int>(grid_size) + 4);
        Hit hit{};
        ASSERT_TRUE(scene->intersect(ray, hit));
        EXPECT_EQ(hit.geometry, late.geometry.get());
    }
}

TEST(Bvh_scene, MaskedTraversalThroughTlas)
{
    std::vector<Test_geometry> geometries;
    auto  scene     = IScene::create_unique("masked");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        geometries.push_back(make_grid_geometry(static_cast<int>(i)));
        geometries.back().geometry->set_mask((i == 3) ? 0x2u : 0x1u);
        scene->attach(geometries.back().geometry.get());
    }
    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    {
        Ray ray = make_grid_ray(3);
        ray.mask = 0x1u;
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
    {
        Ray ray = make_grid_ray(3);
        ray.mask = 0x2u;
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }
}

TEST(Bvh_scene, DisabledMemberStaysInTlasButIsNotHit)
{
    std::vector<Test_geometry> geometries;
    auto  scene     = IScene::create_unique("disabled");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        geometries.push_back(make_grid_geometry(static_cast<int>(i)));
        scene->attach(geometries.back().geometry.get());
    }
    tick_until_static(scene.get());
    ASSERT_EQ(bvh_scene->get_tlas_member_count(), grid_size);

    // Visibility toggles are frequent and must not throw the BVH away.
    geometries[3].geometry->disable();
    EXPECT_EQ(bvh_scene->get_tlas_member_count(), grid_size);
    {
        Ray ray = make_grid_ray(3);
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }

    geometries[3].geometry->enable();
    EXPECT_EQ(bvh_scene->get_tlas_member_count(), grid_size);
    {
        Ray ray = make_grid_ray(3);
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }
}

// Sets the raytrace executor for the duration of a test, so that a failing
// assertion cannot leave a dangling executor behind for the tests after it.
class Scoped_executor
{
public:
    explicit Scoped_executor(tf::Executor& executor)
    {
        erhe::raytrace::set_executor(&executor);
    }
    ~Scoped_executor()
    {
        erhe::raytrace::set_executor(nullptr);
    }
};

TEST(Bvh_scene, AsyncTlasBuild)
{
    tf::Executor    executor{2};
    Scoped_executor scoped_executor{executor};

    std::vector<Test_geometry> geometries;
    auto  scene     = IScene::create_unique("async");
    auto* bvh_scene = as_bvh_scene(scene.get());
    for (std::size_t i = 0; i < grid_size; ++i) {
        geometries.push_back(make_grid_geometry(static_cast<int>(i)));
        scene->attach(geometries.back().geometry.get());
    }

    // Traversal has to give the same answers at every commit, whether the
    // build has not started, is running, or has landed.
    const auto check_all_rays = [&]() {
        for (std::size_t i = 0; i < grid_size; ++i) {
            Ray ray = make_grid_ray(static_cast<int>(i));
            Hit hit{};
            ASSERT_TRUE(scene->intersect(ray, hit)) << "miss for " << i;
            EXPECT_EQ(hit.geometry, geometries[i].geometry.get());
            EXPECT_NEAR(ray.t_far, 10.0f + static_cast<float>(i), 0.001f);
        }
    };

    constexpr int max_commits = 10000;
    int           commits     = 0;
    while ((bvh_scene->get_tlas_member_count() != grid_size) && (commits < max_commits)) {
        scene->commit();
        ++commits;
        check_all_rays();
    }
    EXPECT_LT(commits, max_commits) << "scene BVH was never taken into use";
    EXPECT_EQ(bvh_scene->get_tlas_member_count(), grid_size);
    check_all_rays();
}

TEST(Bvh_scene, AsyncTlasBuildWithModificationsInFlight)
{
    tf::Executor    executor{2};
    Scoped_executor scoped_executor{executor};

    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    std::vector<std::unique_ptr<IInstance>> instances;
    auto scene = IScene::create_unique("async_moving");
    for (std::size_t i = 0; i < grid_size; ++i) {
        auto instance = IInstance::create_unique("inst");
        instance->set_scene(child_scene.get());
        instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f * static_cast<float>(i), 0.0f, 0.0f}));
        scene->attach(instance.get());
        instances.push_back(std::move(instance));
    }

    // One instance keeps moving, so builds may be aborted mid flight. Every
    // commit still has to give the correct answer for both the moving and the
    // settled instances.
    for (int commit = 0; commit < 200; ++commit) {
        const float x = 100.0f + static_cast<float>(commit);
        instances[0]->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{x, 0.0f, 0.0f}));
        instances[0]->commit();
        scene->commit();

        {
            Ray ray = make_ray({x + 0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
            Hit hit{};
            ASSERT_TRUE(scene->intersect(ray, hit)) << "moving instance missed at commit " << commit;
            EXPECT_EQ(hit.instance, instances[0].get());
        }
        {
            Ray ray = make_ray({4.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
            Hit hit{};
            ASSERT_TRUE(scene->intersect(ray, hit)) << "settled instance missed at commit " << commit;
            EXPECT_EQ(hit.instance, instances[2].get());
        }
        {
            Ray ray = make_ray({0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
            Hit hit{};
            EXPECT_FALSE(scene->intersect(ray, hit)) << "hit at the vacated position at commit " << commit;
        }
    }
}

} // anonymous namespace

#endif // defined(ERHE_RAYTRACE_LIBRARY_BVH)
