#include "transform/ik_solver.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

using namespace glm;

constexpr float c_pi = pi<float>();

// A straight two-segment chain along +Y: root at origin, "elbow" at (0,1,0),
// effector at (0,2,0). Local rotations identity, child offsets +Y, so the
// derived twist axis is Y (1) and the swing axes are X (0) and Z (2).
[[nodiscard]] auto make_straight_chain() -> editor::Ik_chain
{
    editor::Ik_chain chain;
    chain.positions       = { vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
    chain.lengths         = { 1.0f, 1.0f };
    chain.local_rotations = { quat{1.0f, 0.0f, 0.0f, 0.0f}, quat{1.0f, 0.0f, 0.0f, 0.0f}, quat{1.0f, 0.0f, 0.0f, 0.0f} };
    chain.child_dir_local = { vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f} };
    chain.constraints.resize(3);
    return chain;
}

// Angle of the joint's solved rotation about the given local axis, in the
// rest-relative frame, extracted through the same swing-twist convention
// the solver clamps in (sin(half-angle) component).
[[nodiscard]] auto swing_angle_about(const quat& rest, const quat& local, const int axis, const int twist_axis) -> float
{
    const quat rel = normalize(inverse(rest) * local);
    // swing-twist decomposition about twist_axis
    vec3 twist_dir{0.0f};
    twist_dir[twist_axis] = 1.0f;
    const vec3  r{rel.x, rel.y, rel.z};
    const float proj = dot(r, twist_dir);
    quat twist{rel.w, proj * twist_dir.x, proj * twist_dir.y, proj * twist_dir.z};
    const float len2 = (rel.w * rel.w) + (proj * proj);
    quat swing;
    if (len2 < 1.0e-12f) {
        twist = quat{1.0f, 0.0f, 0.0f, 0.0f};
        swing = rel;
    } else {
        twist = normalize(twist);
        swing = rel * inverse(twist);
    }
    if (swing.w < 0.0f) {
        swing = quat{-swing.w, -swing.x, -swing.y, -swing.z};
    }
    const float s = (axis == 0) ? swing.x : (axis == 1) ? swing.y : swing.z;
    return 2.0f * std::asin(std::clamp(s, -1.0f, 1.0f));
}

TEST(Ik_solver, unconstrained_matches_phase1_fabrik)
{
    editor::Ik_chain chain = make_straight_chain();
    chain.target = vec3{0.8f, 1.2f, 0.3f};

    std::vector<vec3> reference = { vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
    editor::fabrik_solve(reference, chain.lengths, chain.target, chain.tolerance, chain.max_iterations);

    editor::Fabrik_solver solver;
    solver.solve(chain);

    ASSERT_FALSE(chain.has_constraints());
    for (std::size_t i = 0; i < reference.size(); ++i) {
        EXPECT_EQ(chain.positions[i], reference[i]) << "joint " << i;
    }
    // local_rotations untouched on the unconstrained path
    for (const quat& q : chain.local_rotations) {
        EXPECT_EQ(q, (quat{1.0f, 0.0f, 0.0f, 0.0f}));
    }
}

TEST(Ik_solver, constrained_preserves_segment_lengths)
{
    editor::Ik_chain chain = make_straight_chain();
    chain.constraints[1].enabled    = true;
    chain.constraints[1].twist_axis = 1;
    chain.constraints[1].limit[0]   = true;
    chain.constraints[1].limit_min  = vec3{-2.618f, -c_pi, -c_pi};
    chain.constraints[1].limit_max  = vec3{0.0f, c_pi, c_pi};
    chain.target = vec3{0.3f, 1.4f, 0.7f};

    editor::Fabrik_solver solver;
    solver.solve(chain);

    EXPECT_NEAR(distance(chain.positions[0], chain.positions[1]), 1.0f, 1.0e-4f);
    EXPECT_NEAR(distance(chain.positions[1], chain.positions[2]), 1.0f, 1.0e-4f);
    EXPECT_EQ(chain.positions[0], (vec3{0.0f, 0.0f, 0.0f})); // root fixed
}

TEST(Ik_solver, fully_locked_joint_is_rigid)
{
    editor::Ik_chain chain = make_straight_chain();
    chain.constraints[1].enabled    = true;
    chain.constraints[1].twist_axis = 1;
    chain.constraints[1].lock       = {true, true, true};
    chain.target = vec3{0.6f, 1.0f, 0.4f};

    editor::Fabrik_solver solver;
    solver.solve(chain);

    // The locked elbow's local rotation never changes: the two segments
    // stay collinear (one rigid link), whatever the root does.
    EXPECT_NEAR(dot(
        normalize(chain.positions[1] - chain.positions[0]),
        normalize(chain.positions[2] - chain.positions[1])), 1.0f, 1.0e-3f);
    const float x_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, chain.local_rotations[1], 0, 1);
    const float z_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, chain.local_rotations[1], 2, 1);
    EXPECT_NEAR(x_angle, 0.0f, 1.0e-4f);
    EXPECT_NEAR(z_angle, 0.0f, 1.0e-4f);
}

TEST(Ik_solver, hinge_bends_only_about_free_axis)
{
    editor::Ik_chain chain = make_straight_chain();
    // Hinge about X on both joints: Z swing locked, X limited wide open.
    for (int j = 0; j < 2; ++j) {
        chain.constraints[j].enabled    = true;
        chain.constraints[j].twist_axis = 1;
        chain.constraints[j].lock[2]    = true;
        chain.constraints[j].limit[0]   = true;
        chain.constraints[j].limit_min  = vec3{-c_pi, -c_pi, -c_pi};
        chain.constraints[j].limit_max  = vec3{c_pi, c_pi, c_pi};
    }
    chain.target = vec3{0.0f, 1.2f, 0.9f}; // in the hinge plane (YZ)

    editor::Fabrik_solver solver;
    solver.solve(chain);

    for (int j = 0; j < 2; ++j) {
        const float z_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, chain.local_rotations[j], 2, 1);
        EXPECT_NEAR(z_angle, 0.0f, 1.0e-3f) << "joint " << j;
    }
    // A target in the hinge plane on the reachable side is reached.
    EXPECT_LT(distance(chain.positions[2], chain.target), 0.02f);
}

TEST(Ik_solver, limit_stops_at_boundary_and_stays_stable)
{
    editor::Ik_chain chain = make_straight_chain();
    const float max_bend = 0.4f;
    for (int j = 0; j < 2; ++j) {
        chain.constraints[j].enabled    = true;
        chain.constraints[j].twist_axis = 1;
        chain.constraints[j].limit[0]   = true;
        chain.constraints[j].limit[2]   = true;
        chain.constraints[j].limit_min  = vec3{-max_bend, -c_pi, -max_bend};
        chain.constraints[j].limit_max  = vec3{max_bend, c_pi, max_bend};
    }
    chain.target = vec3{0.0f, -1.0f, 1.5f}; // far outside what the limits allow

    editor::Fabrik_solver solver;
    solver.solve(chain);

    for (int j = 0; j < 2; ++j) {
        const float x_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, chain.local_rotations[j], 0, 1);
        const float z_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, chain.local_rotations[j], 2, 1);
        EXPECT_LE(std::abs(x_angle), max_bend + 1.0e-3f) << "joint " << j;
        EXPECT_LE(std::abs(z_angle), max_bend + 1.0e-3f) << "joint " << j;
    }
    // Best-effort pose: no NaNs, lengths preserved.
    for (const vec3& p : chain.positions) {
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }
    EXPECT_NEAR(distance(chain.positions[0], chain.positions[1]), 1.0f, 1.0e-4f);
    EXPECT_NEAR(distance(chain.positions[1], chain.positions[2]), 1.0f, 1.0e-4f);
}

TEST(Ik_solver, out_of_limit_start_pose_is_not_teleported)
{
    editor::Ik_chain chain = make_straight_chain();
    // Start with the elbow already bent 1.0 rad about X, but authored
    // limits allowing only 0.2 rad. The extended constraint region includes
    // the drag-start angle, so solving toward the current effector position
    // must keep the pose (no snap toward the authored limit).
    const quat bent = angleAxis(1.0f, vec3{1.0f, 0.0f, 0.0f});
    chain.local_rotations[1] = bent;
    chain.positions[2]       = chain.positions[1] + bent * vec3{0.0f, 1.0f, 0.0f};
    chain.constraints[1].enabled    = true;
    chain.constraints[1].twist_axis = 1;
    chain.constraints[1].limit[0]   = true;
    chain.constraints[1].limit_min  = vec3{-0.2f, -c_pi, -c_pi};
    chain.constraints[1].limit_max  = vec3{0.2f, c_pi, c_pi};
    chain.target = chain.positions[2]; // no movement demanded

    editor::Fabrik_solver solver;
    solver.solve(chain);

    const float x_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, chain.local_rotations[1], 0, 1);
    EXPECT_NEAR(x_angle, 1.0f, 5.0e-2f); // stays at the drag-start bend
}

TEST(Ik_solver, zero_length_segment_with_constraints_is_safe)
{
    editor::Ik_chain chain;
    chain.positions       = { vec3{0.0f}, vec3{0.0f}, vec3{0.0f, 1.0f, 0.0f} };
    chain.lengths         = { 0.0f, 1.0f };
    chain.local_rotations = { quat{1.0f, 0.0f, 0.0f, 0.0f}, quat{1.0f, 0.0f, 0.0f, 0.0f}, quat{1.0f, 0.0f, 0.0f, 0.0f} };
    chain.child_dir_local = { vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f} };
    chain.constraints.resize(3);
    chain.constraints[1].enabled    = true;
    chain.constraints[1].twist_axis = 1;
    chain.constraints[1].limit[0]   = true;
    chain.constraints[1].limit_min  = vec3{-0.5f, -c_pi, -c_pi};
    chain.constraints[1].limit_max  = vec3{0.5f, c_pi, c_pi};
    chain.target = vec3{0.5f, 0.5f, 0.0f};

    editor::Fabrik_solver solver;
    solver.solve(chain);

    for (const vec3& p : chain.positions) {
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }
    for (const quat& q : chain.local_rotations) {
        EXPECT_TRUE(std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z));
    }
}

TEST(Ik_solver, twist_only_constraint_is_a_no_op)
{
    editor::Ik_chain constrained = make_straight_chain();
    constrained.constraints[1].enabled    = true;
    constrained.constraints[1].twist_axis = 1;
    constrained.constraints[1].lock[1]    = true;  // twist axis lock: solve no-op
    constrained.constraints[1].limit[1]   = true;  // twist axis limit: solve no-op
    constrained.constraints[1].limit_min  = vec3{-c_pi, -0.01f, -c_pi};
    constrained.constraints[1].limit_max  = vec3{c_pi, 0.01f, c_pi};
    constrained.target = vec3{0.4f, 1.3f, 0.5f};

    editor::Fabrik_solver solver;
    solver.solve(constrained);

    // The effector still reaches the target: a twist-axis constraint never
    // restricts the solve (the solver does not generate twist).
    EXPECT_LT(distance(constrained.positions[2], constrained.target), 0.02f);
}

} // anonymous namespace
