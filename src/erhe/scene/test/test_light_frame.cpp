// Light node scale must not leak into the light frame / shadow projection.
//
// glTF KHR_lights_punctual: a light inherits the orientation of its node;
// position and scale are ignored "except for their effect on the inherited node
// orientation". So scale participates in orienting the node axes, but the
// resulting light frame must be orthonormal - otherwise the directional shadow
// frustum fit works in scaled units and misplaces the shadow map.

#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <memory>

namespace {

class Light_test_scene
{
public:
    Light_test_scene(const erhe::scene::Light_type light_type, const glm::mat4& world_from_light_node)
    {
        camera_node = std::make_shared<erhe::scene::Node>("camera node");
        camera      = std::make_shared<erhe::scene::Camera>("camera");
        camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        camera->projection()->z_near = 0.1f;
        camera->projection()->z_far  = 100.0f;
        camera->projection()->fov_y  = glm::pi<float>() / 3.0f;
        camera->set_shadow_range(20.0f);
        camera_node->attach(camera);
        camera_node->set_parent_from_node(
            glm::inverse(glm::lookAt(glm::vec3{3.0f, 4.0f, 5.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}))
        );

        light_node = std::make_shared<erhe::scene::Node>("light node");
        light      = std::make_shared<erhe::scene::Light>("light");
        light->type  = light_type;
        light->range = 30.0f;
        light_node->attach(light);
        light_node->set_parent_from_node(world_from_light_node);
    }

    [[nodiscard]] auto make_parameters(const erhe::scene::Shadow_frustum_fit_settings* fit_settings) const -> erhe::scene::Light_projection_parameters
    {
        erhe::scene::Light_projection_parameters parameters{};
        parameters.view_camera          = camera.get();
        parameters.main_camera_viewport = erhe::math::Viewport{0, 0, 1920, 1080};
        parameters.shadow_map_viewport  = erhe::math::Viewport{0, 0, 2048, 2048};
        parameters.reverse_depth        = true;
        parameters.depth_range          = erhe::math::Depth_range::zero_to_one;
        parameters.fit_settings         = fit_settings;
        return parameters;
    }

    std::shared_ptr<erhe::scene::Node>   camera_node;
    std::shared_ptr<erhe::scene::Camera> camera;
    std::shared_ptr<erhe::scene::Node>   light_node;
    std::shared_ptr<erhe::scene::Light>  light;
};

// Light node orientation used by the invariance tests: a plain rotation, no
// scale. The scaled variants below apply a *uniform* scale on top of it, which
// per the spec cannot change the inherited orientation at all.
[[nodiscard]] auto rotation_only_light_node_transform() -> glm::mat4
{
    return
        glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 10.0f, -3.0f}) *
        glm::rotate(glm::mat4{1.0f}, 0.7f, glm::normalize(glm::vec3{0.3f, 1.0f, 0.2f}));
}

void expect_matrices_near(const glm::mat4& a, const glm::mat4& b, const float epsilon, const char* what)
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(a[column][row], b[column][row], epsilon)
                << what << " differs at column " << column << " row " << row;
        }
    }
}

[[nodiscard]] auto tight_fit_settings() -> erhe::scene::Shadow_frustum_fit_settings
{
    erhe::scene::Shadow_frustum_fit_settings settings{};
    settings.fit_to_view_frustum = true;
    settings.fit_to_casters      = false;
    settings.texel_snap          = true;
    return settings;
}

} // anonymous namespace

TEST(light_frame, frame_is_orthonormal_for_scaled_node)
{
    const glm::mat4 world_from_node =
        glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}) *
        glm::rotate(glm::mat4{1.0f}, 0.9f, glm::normalize(glm::vec3{1.0f, 2.0f, -0.5f})) *
        glm::scale(glm::mat4{1.0f}, glm::vec3{5.0f, 0.25f, 3.0f});

    Light_test_scene scene{erhe::scene::Light_type::directional, world_from_node};
    const erhe::scene::Light_frame frame = scene.light->get_light_frame();

    EXPECT_NEAR(glm::length(frame.direction), 1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(frame.up),        1.0f, 1e-5f);
    EXPECT_NEAR(glm::length(frame.right),     1.0f, 1e-5f);
    EXPECT_NEAR(glm::dot(frame.direction, frame.up),    0.0f, 1e-5f);
    EXPECT_NEAR(glm::dot(frame.direction, frame.right), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::dot(frame.up,        frame.right), 0.0f, 1e-5f);

    // Right-handed even though the node has no mirroring here.
    const glm::vec3 expected_right = glm::cross(frame.up, frame.direction);
    EXPECT_NEAR(glm::length(frame.right - expected_right), 0.0f, 1e-5f);

    // Position is the node world position.
    EXPECT_NEAR(glm::length(frame.position - glm::vec3{world_from_node[3]}), 0.0f, 1e-5f);

    // world_from_light and light_from_world are inverses.
    expect_matrices_near(frame.world_from_light * frame.light_from_world, glm::mat4{1.0f}, 1e-5f, "world_from_light * light_from_world");
}

// Scale is not ignored when it reorients the node axes: the light direction is
// the normalized transformed node +Z, non-uniform scale included. That happens
// when the scale sits above the rotation in the transform (an ancestor scale
// applied to a rotated light node); a node-local scale after the rotation only
// stretches the axes along themselves and cannot reorient them.
TEST(light_frame, direction_follows_scaled_node_axis)
{
    const glm::mat4 rotation = glm::rotate(glm::mat4{1.0f}, glm::quarter_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    const glm::mat4 world_from_node = glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 4.0f, 1.0f}) * rotation;

    Light_test_scene scene{erhe::scene::Light_type::directional, world_from_node};
    const erhe::scene::Light_frame frame = scene.light->get_light_frame();

    const glm::vec3 expected = glm::normalize(glm::vec3{world_from_node * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}});
    EXPECT_NEAR(glm::length(frame.direction - expected), 0.0f, 1e-5f);

    // The non-uniform scale really does reorient +Z here, so this is not the
    // rotation-only direction.
    const glm::vec3 rotation_only = glm::normalize(glm::vec3{rotation * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}});
    EXPECT_GT(glm::length(frame.direction - rotation_only), 0.1f);
}

// Negative scale mirrors the node axes; the light direction follows, and the
// frame stays a proper (right-handed) rotation.
TEST(light_frame, negative_scale_mirrors_direction_and_keeps_right_handed_frame)
{
    const glm::mat4 rotation = glm::rotate(glm::mat4{1.0f}, 0.4f, glm::normalize(glm::vec3{0.2f, 1.0f, 0.1f}));
    const glm::mat4 world_from_node = rotation * glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.0f, -2.0f});

    Light_test_scene scene{erhe::scene::Light_type::directional, world_from_node};
    const erhe::scene::Light_frame frame = scene.light->get_light_frame();

    const glm::vec3 unmirrored = glm::normalize(glm::vec3{rotation * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}});
    EXPECT_NEAR(glm::length(frame.direction + unmirrored), 0.0f, 1e-5f);
    EXPECT_GT(glm::determinant(glm::mat3{frame.world_from_light}), 0.0f);
}

// The regression this fixes: a uniformly scaled directional light node must
// produce exactly the same stable shadow fit as an unscaled one.
TEST(light_frame, stable_directional_fit_is_scale_invariant)
{
    const glm::mat4 base = rotation_only_light_node_transform();

    Light_test_scene unscaled{erhe::scene::Light_type::directional, base};
    Light_test_scene scaled  {erhe::scene::Light_type::directional, base * glm::scale(glm::mat4{1.0f}, glm::vec3{7.0f})};

    const erhe::scene::Light_projection_transforms a = unscaled.light->projection_transforms(unscaled.make_parameters(nullptr));
    const erhe::scene::Light_projection_transforms b = scaled  .light->projection_transforms(scaled  .make_parameters(nullptr));

    EXPECT_FLOAT_EQ(a.projection.z_near,       b.projection.z_near);
    EXPECT_FLOAT_EQ(a.projection.z_far,        b.projection.z_far);
    EXPECT_FLOAT_EQ(a.projection.ortho_width,  b.projection.ortho_width);
    EXPECT_FLOAT_EQ(a.projection.ortho_height, b.projection.ortho_height);
    expect_matrices_near(a.clip_from_world.get_matrix(),         b.clip_from_world.get_matrix(),         1e-4f, "clip_from_world");
    expect_matrices_near(a.texture_from_world.get_matrix(),      b.texture_from_world.get_matrix(),      1e-4f, "texture_from_world");
    expect_matrices_near(a.world_from_light_camera.get_matrix(), b.world_from_light_camera.get_matrix(), 1e-4f, "world_from_light_camera");
}

TEST(light_frame, tight_directional_fit_is_scale_invariant)
{
    const glm::mat4 base = rotation_only_light_node_transform();
    const erhe::scene::Shadow_frustum_fit_settings settings = tight_fit_settings();

    Light_test_scene unscaled{erhe::scene::Light_type::directional, base};
    Light_test_scene scaled  {erhe::scene::Light_type::directional, base * glm::scale(glm::mat4{1.0f}, glm::vec3{0.05f})};

    const erhe::scene::Light_projection_transforms a = unscaled.light->projection_transforms(unscaled.make_parameters(&settings));
    const erhe::scene::Light_projection_transforms b = scaled  .light->projection_transforms(scaled  .make_parameters(&settings));

    EXPECT_NEAR(a.projection.z_near,       b.projection.z_near,       1e-4f);
    EXPECT_NEAR(a.projection.z_far,        b.projection.z_far,        1e-4f);
    EXPECT_NEAR(a.projection.ortho_width,  b.projection.ortho_width,  1e-4f);
    EXPECT_NEAR(a.projection.ortho_height, b.projection.ortho_height, 1e-4f);
    expect_matrices_near(a.clip_from_world.get_matrix(),         b.clip_from_world.get_matrix(),         1e-4f, "clip_from_world");
    expect_matrices_near(a.world_from_light_camera.get_matrix(), b.world_from_light_camera.get_matrix(), 1e-4f, "world_from_light_camera");
}

TEST(light_frame, spot_projection_is_scale_invariant)
{
    const glm::mat4 base = rotation_only_light_node_transform();

    Light_test_scene unscaled{erhe::scene::Light_type::spot, base};
    Light_test_scene scaled  {erhe::scene::Light_type::spot, base * glm::scale(glm::mat4{1.0f}, glm::vec3{3.0f})};

    const erhe::scene::Light_projection_transforms a = unscaled.light->projection_transforms(unscaled.make_parameters(nullptr));
    const erhe::scene::Light_projection_transforms b = scaled  .light->projection_transforms(scaled  .make_parameters(nullptr));

    expect_matrices_near(a.clip_from_world.get_matrix(),         b.clip_from_world.get_matrix(),         1e-4f, "clip_from_world");
    expect_matrices_near(a.texture_from_world.get_matrix(),      b.texture_from_world.get_matrix(),      1e-4f, "texture_from_world");
    expect_matrices_near(a.world_from_light_camera.get_matrix(), b.world_from_light_camera.get_matrix(), 1e-4f, "world_from_light_camera");
}

TEST(light_frame, point_light_pose_is_scale_invariant)
{
    const glm::mat4 base = rotation_only_light_node_transform();

    Light_test_scene unscaled{erhe::scene::Light_type::point, base};
    Light_test_scene scaled  {erhe::scene::Light_type::point, base * glm::scale(glm::mat4{1.0f}, glm::vec3{9.0f})};

    const erhe::scene::Light_projection_transforms a = unscaled.light->projection_transforms(unscaled.make_parameters(nullptr));
    const erhe::scene::Light_projection_transforms b = scaled  .light->projection_transforms(scaled  .make_parameters(nullptr));

    expect_matrices_near(a.world_from_light_camera.get_matrix(), b.world_from_light_camera.get_matrix(), 1e-4f, "world_from_light_camera");
}

// The shadow map texel size derived from the light-space extents must be in
// world units: light-space distances between world points must equal the world
// distance, whatever the light node scale.
TEST(light_frame, light_space_preserves_world_distances)
{
    const glm::mat4 world_from_node =
        rotation_only_light_node_transform() *
        glm::scale(glm::mat4{1.0f}, glm::vec3{12.0f, 0.1f, 4.0f});

    Light_test_scene scene{erhe::scene::Light_type::directional, world_from_node};
    const erhe::scene::Light_frame frame = scene.light->get_light_frame();

    const glm::vec3 p0{-4.0f, 2.0f, 7.0f};
    const glm::vec3 p1{ 6.0f, -3.0f, 1.5f};
    const glm::vec3 p0_in_light = glm::vec3{frame.light_from_world * glm::vec4{p0, 1.0f}};
    const glm::vec3 p1_in_light = glm::vec3{frame.light_from_world * glm::vec4{p1, 1.0f}};

    EXPECT_NEAR(glm::distance(p0_in_light, p1_in_light), glm::distance(p0, p1), 1e-3f);
}
