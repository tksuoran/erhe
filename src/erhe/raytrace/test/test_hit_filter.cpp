#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

// Rejects one instance and, with a direction, every face whose normal does not face the ray.
class Test_filter : public IHit_filter
{
public:
    Test_filter(const IInstance* rejected, const glm::vec3 front_of)
        : m_rejected{rejected}
        , m_front_of{front_of}
    {
    }

    auto accept(const Hit& candidate) const -> bool override
    {
        ++m_calls;
        if ((m_rejected != nullptr) && (candidate.instance == m_rejected)) {
            return false;
        }
        return (m_front_of == glm::vec3{0.0f}) || (glm::dot(candidate.normal, m_front_of) < 0.0f);
    }

    mutable int m_calls{0};

private:
    const IInstance* m_rejected;
    glm::vec3        m_front_of;
};

// Square in the y = -0.5 plane facing +y, coincident with the unit cube's bottom face.
auto make_floor() -> Test_geometry
{
    return make_triangle_geometry(
        "floor",
        { {-1.0f, -0.5f, -1.0f}, {-1.0f, -0.5f, 1.0f}, {1.0f, -0.5f, 1.0f}, {1.0f, -0.5f, -1.0f} },
        { {0, 1, 2}, {0, 2, 3} }
    );
}

enum class Attach_order : unsigned int { first_first, second_first };

class Two_instances
{
public:
    Two_instances(Test_geometry& first, Test_geometry& second, const Attach_order order)
        : root{IScene::create_unique("root")}
    {
        std::array<Test_geometry*, 2> geometries{&first, &second};
        for (std::size_t i = 0; i < 2; ++i) {
            children[i]  = IScene::create_unique("child");
            instances[i] = IInstance::create_unique("instance");
            children[i]->attach(geometries[i]->geometry.get());
            children[i]->commit();
            instances[i]->set_scene(children[i].get());
            instances[i]->set_transform(glm::mat4{1.0f});
            instances[i]->set_mask(0xffffffffu);
            instances[i]->commit();
        }
        const bool second_first = (order == Attach_order::second_first);
        root->attach(instances[second_first ? 1 : 0].get());
        root->attach(instances[second_first ? 0 : 1].get());
        root->commit();
    }

    void set_transform(const std::size_t index, const glm::mat4& transform)
    {
        instances[index]->set_transform(transform);
        instances[index]->commit();
        root->commit();
    }

    std::unique_ptr<IScene>                   root;
    std::array<std::unique_ptr<IScene>, 2>    children;
    std::array<std::unique_ptr<IInstance>, 2> instances;
};

TEST(Hit_filter, RejectedInstanceLeavesCoincidentFace)
{
    for (const Attach_order order : {Attach_order::first_first, Attach_order::second_first}) {
        Test_geometry a = make_quad();
        Test_geometry b = make_quad();
        Two_instances scene{a, b, order};
        for (std::size_t rejected = 0; rejected < 2; ++rejected) {
            const Test_filter filter{scene.instances[rejected].get(), glm::vec3{0.0f}};
            Ray ray = make_ray({0.25f, 0.5f, 1.0f}, {0.0f, 0.0f, -1.0f});
            ray.filter = &filter;
            Hit hit{};
            EXPECT_TRUE(scene.root->intersect(ray, hit));
            EXPECT_EQ(hit.instance, scene.instances[1 - rejected].get());
            EXPECT_NEAR(ray.t_far, 1.0f, 1.0e-5f);
            EXPECT_GT(filter.m_calls, 0);
        }
    }
}

TEST(Hit_filter, FrontFaceCoincidentWithBackFace)
{
    // From inside the cube, its bottom face is a back face; the floor's front face lies on it.
    for (const Attach_order order : {Attach_order::first_first, Attach_order::second_first}) {
        Test_geometry cube  = make_cube();
        Test_geometry ground = make_floor();
        Two_instances scene{cube, ground, order};
        const glm::vec3   down{0.0f, -1.0f, 0.0f};
        const Test_filter filter{nullptr, down};
        Ray ray = make_ray({0.1f, 0.0f, 0.2f}, down);
        ray.filter = &filter;
        Hit hit{};
        EXPECT_TRUE(scene.root->intersect(ray, hit));
        EXPECT_EQ(hit.instance, scene.instances[1].get());
        EXPECT_NEAR(ray.t_far, 0.5f, 1.0e-5f);
        EXPECT_GT(hit.normal.y, 0.0f);
    }
}

TEST(Hit_filter, AllRejectedIsMiss)
{
    Test_geometry cube  = make_cube();
    Test_geometry ground = make_floor();
    Two_instances scene{cube, ground, Attach_order::first_first};
    const glm::vec3   up{0.0f, 1.0f, 0.0f};
    const Test_filter filter{scene.instances[0].get(), up};
    // Upward from below: the cube is rejected and the floor faces along the ray.
    Ray ray = make_ray({0.1f, -2.0f, 0.2f}, up, 1.6f);
    ray.filter = &filter;
    Hit hit{};
    EXPECT_FALSE(scene.root->intersect(ray, hit));
    EXPECT_EQ(hit.instance, nullptr);
    EXPECT_FLOAT_EQ(ray.t_far, 1.6f);
}

TEST(Hit_filter, NoFilterKeepsNearestHit)
{
    Test_geometry cube  = make_cube();
    Test_geometry ground = make_floor();
    Two_instances scene{cube, ground, Attach_order::first_first};
    Ray ray = make_ray({0.1f, 0.0f, 0.2f}, {0.0f, -1.0f, 0.0f});
    Hit hit{};
    EXPECT_TRUE(scene.root->intersect(ray, hit));
    EXPECT_NEAR(ray.t_far, 0.5f, 1.0e-5f);
}

// Accepts every candidate and keeps the normal of the last one.
class Recording_filter : public IHit_filter
{
public:
    auto accept(const Hit& candidate) const -> bool override
    {
        m_last_normal = candidate.normal;
        return true;
    }

    mutable glm::vec3 m_last_normal{0.0f};
};

TEST(Hit_filter, MirroredInstanceKeepsOutwardNormal)
{
    // Scale -1 along x mirrors the floor; its top still faces +y, for the filter and the returned hit.
    Test_geometry cube   = make_cube();
    Test_geometry ground = make_floor();
    Two_instances scene{cube, ground, Attach_order::first_first};
    scene.set_transform(0, glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 10.0f, 0.0f}));
    scene.set_transform(1, glm::scale(glm::mat4{1.0f}, glm::vec3{-1.0f, 1.0f, 1.0f}));
    const glm::vec3   down{0.0f, -1.0f, 0.0f};
    const Test_filter filter{nullptr, down};
    Ray ray = make_ray({0.1f, 1.0f, 0.2f}, down);
    ray.filter = &filter;
    Hit hit{};
    EXPECT_TRUE(scene.root->intersect(ray, hit));
    EXPECT_EQ(hit.instance, scene.instances[1].get());
    EXPECT_NEAR(ray.t_far, 1.5f, 1.0e-5f);
    EXPECT_GT(hit.normal.y, 0.0f);

    Ray plain = make_ray({0.1f, 1.0f, 0.2f}, down);
    Hit plain_hit{};
    EXPECT_TRUE(scene.root->intersect(plain, plain_hit));
    EXPECT_GT(plain_hit.normal.y, 0.0f);
}

TEST(Hit_filter, RotatedTranslatedInstanceNormal)
{
    // The quad faces +z; turned 90 degrees about +y it faces +x, then moved to x = 5.
    Test_geometry a = make_quad();
    Test_geometry b = make_quad();
    Two_instances scene{a, b, Attach_order::first_first};
    scene.set_transform(0, glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 50.0f}));
    scene.set_transform(
        1,
        glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}) * glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f})
    );
    const Recording_filter filter{};
    Ray ray = make_ray({7.0f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f});
    ray.filter = &filter;
    Hit hit{};
    EXPECT_TRUE(scene.root->intersect(ray, hit));
    EXPECT_EQ(hit.instance, scene.instances[1].get());
    EXPECT_NEAR(ray.t_far, 2.0f, 1.0e-5f);
    const glm::vec3 candidate_normal = glm::normalize(filter.m_last_normal);
    const glm::vec3 hit_normal       = glm::normalize(hit.normal);
    EXPECT_NEAR(candidate_normal.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(hit_normal.x,       1.0f, 1.0e-5f);
}

TEST(Hit_filter, NearLimitSkipsCloserFaces)
{
    Test_geometry a = make_quad();
    Test_geometry b = make_quad();
    Two_instances scene{a, b, Attach_order::first_first};
    scene.set_transform(1, glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, -1.0f}));
    const Recording_filter filter{};
    Ray ray = make_ray({0.25f, 0.5f, 1.0f}, {0.0f, 0.0f, -1.0f});
    ray.t_near = 1.5f;
    ray.filter = &filter;
    Hit hit{};
    EXPECT_TRUE(scene.root->intersect(ray, hit));
    EXPECT_EQ(hit.instance, scene.instances[1].get());
    EXPECT_NEAR(ray.t_far, 2.0f, 1.0e-5f);
}

} // anonymous namespace
