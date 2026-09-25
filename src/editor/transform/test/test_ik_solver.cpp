#include "transform/ik_solver.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

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

// A straight four-segment chain along +Y: root at origin, joints at +1 .. +4.
[[nodiscard]] auto make_straight_five_joint_chain() -> editor::Ik_chain
{
    editor::Ik_chain chain;
    chain.positions = {
        vec3{0.0f, 0.0f, 0.0f},
        vec3{0.0f, 1.0f, 0.0f},
        vec3{0.0f, 2.0f, 0.0f},
        vec3{0.0f, 3.0f, 0.0f},
        vec3{0.0f, 4.0f, 0.0f}
    };
    chain.lengths = { 1.0f, 1.0f, 1.0f, 1.0f };
    chain.local_rotations.assign(5, quat{1.0f, 0.0f, 0.0f, 0.0f});
    chain.child_dir_local.assign(4, vec3{0.0f, 1.0f, 0.0f});
    chain.constraints.resize(5);
    return chain;
}

// Component of v - root orthogonal to the unit axis a (R11 step 2).
[[nodiscard]] auto perpendicular_offset(const vec3 v, const vec3 root, const vec3 a) -> vec3
{
    const vec3 offset = v - root;
    return offset - (a * dot(offset, a));
}

// Unit root-to-effector axis of a solved chain (R11 step 1).
[[nodiscard]] auto chain_axis(const editor::Ik_chain& chain) -> vec3
{
    return normalize(chain.positions.back() - chain.positions.front());
}

// Unit mean bend direction of a solved chain (R11 step 3).
[[nodiscard]] auto chain_bend_direction(const editor::Ik_chain& chain) -> vec3
{
    const vec3 root = chain.positions.front();
    const vec3 a    = chain_axis(chain);
    vec3 bend{0.0f};
    for (std::size_t i = 1; i + 1 < chain.positions.size(); ++i) {
        bend += perpendicular_offset(chain.positions[i], root, a);
    }
    return normalize(bend);
}

// Signed angle about the unit axis a taking direction from to direction to.
[[nodiscard]] auto signed_angle_about(const vec3 a, const vec3 from, const vec3 to) -> float
{
    return std::atan2(dot(cross(from, to), a), dot(from, to));
}

TEST(Ik_solver, pole_aims_bend_at_either_side)
{
    const vec3 target{1.0f, 1.0f, 0.0f};
    for (const float pole_z : { 5.0f, -5.0f }) {
        editor::Ik_chain chain = make_straight_chain();
        chain.target        = target;
        chain.has_pole      = true;
        chain.pole_position = vec3{0.0f, 1.0f, pole_z};

        editor::Fabrik_solver solver;
        solver.solve(chain);

        const vec3 root = chain.positions.front();
        const vec3 a    = chain_axis(chain);
        const vec3 b    = chain_bend_direction(chain);
        const vec3 d    = normalize(perpendicular_offset(chain.pole_position, root, a));
        EXPECT_NEAR(dot(b, d), 1.0f, 1.0e-3f) << "pole_z " << pole_z;

        EXPECT_LT(distance(chain.positions[2], target), 1.0e-2f) << "pole_z " << pole_z;
        EXPECT_NEAR(distance(chain.positions[0], chain.positions[1]), 1.0f, 1.0e-4f);
        EXPECT_NEAR(distance(chain.positions[1], chain.positions[2]), 1.0f, 1.0e-4f);
        EXPECT_EQ(chain.positions[0], (vec3{0.0f, 0.0f, 0.0f})); // root fixed
    }
}

TEST(Ik_solver, pole_angle_swivels_by_that_angle_with_sign)
{
    for (const float pole_angle : { 0.5f * c_pi, -0.5f * c_pi }) {
        editor::Ik_chain chain = make_straight_chain();
        chain.target        = vec3{1.0f, 1.0f, 0.0f};
        chain.has_pole      = true;
        chain.pole_position = vec3{0.0f, 1.0f, 5.0f};
        chain.pole_angle    = pole_angle;

        editor::Fabrik_solver solver;
        solver.solve(chain);

        const vec3 a = chain_axis(chain);
        const vec3 b = chain_bend_direction(chain);
        const vec3 d = normalize(perpendicular_offset(chain.pole_position, chain.positions.front(), a));
        // R11 step 5: the bend is aimed at d turned by pole_angle about a.
        EXPECT_NEAR(signed_angle_about(a, d, b), pole_angle, 1.0e-3f) << "pole_angle " << pole_angle;
    }
}

TEST(Ik_solver, pole_on_long_chain_rotates_rigidly)
{
    editor::Ik_chain chain = make_straight_five_joint_chain();
    chain.target = vec3{1.5f, 1.5f, 0.3f};

    editor::Fabrik_solver solver;
    solver.solve(chain);
    const std::vector<vec3> before_pole = chain.positions;

    chain.pole_position = vec3{0.0f, 1.0f, 7.0f};
    editor::ik_apply_pole(chain.positions, chain.pole_position, 0.0f, 1.0f);

    const vec3 a = chain_axis(chain);
    const vec3 b = chain_bend_direction(chain);
    const vec3 d = normalize(perpendicular_offset(chain.pole_position, chain.positions.front(), a));
    EXPECT_NEAR(dot(b, d), 1.0f, 1.0e-3f);

    // Rigid rotation: every pairwise distance is the pre-pole one.
    for (std::size_t i = 0; i < before_pole.size(); ++i) {
        for (std::size_t j = i + 1; j < before_pole.size(); ++j) {
            EXPECT_NEAR(
                distance(chain.positions[i], chain.positions[j]),
                distance(before_pole[i], before_pole[j]),
                1.0e-5f
            ) << "joints " << i << " " << j;
        }
    }
    EXPECT_EQ(chain.positions.front(), before_pole.front());
    EXPECT_EQ(chain.positions.back(),  before_pole.back());
}

TEST(Ik_solver, pole_degenerate_cases_leave_positions_untouched)
{
    // Folded onto the root: the swivel line does not exist (R11 step 1).
    {
        std::vector<vec3> positions = { vec3{0.0f, 0.0f, 0.0f}, vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 0.0f} };
        const std::vector<vec3> before = positions;
        editor::ik_apply_pole(positions, vec3{0.0f, 5.0f, 0.0f}, 0.3f, 1.0f);
        EXPECT_EQ(positions, before);
    }
    // Straight chain: no bend to aim (R11 step 3).
    {
        std::vector<vec3> positions = { vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
        const std::vector<vec3> before = positions;
        editor::ik_apply_pole(positions, vec3{5.0f, 0.0f, 0.0f}, 0.3f, 1.0f);
        EXPECT_EQ(positions, before);
    }
    // Pole on the root-to-effector line: it names no direction (R11 step 4).
    {
        std::vector<vec3> positions = { vec3{0.0f, 0.0f, 0.0f}, vec3{1.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
        const std::vector<vec3> before = positions;
        editor::ik_apply_pole(positions, vec3{0.0f, 5.0f, 0.0f}, 0.3f, 1.0f);
        EXPECT_EQ(positions, before);
    }
    // All of them finite.
    std::vector<vec3> positions = { vec3{0.0f, 0.0f, 0.0f}, vec3{1.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
    editor::ik_apply_pole(positions, vec3{0.0f, 5.0f, 0.0f}, 0.3f, 1.0f);
    for (const vec3& p : positions) {
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }
}

// doc/plans/rigging/ik_drag_options.md R28, 3.6 criterion 5: the weight
// scales the swivel - 0 leaves the positions untouched, 1 is the full swivel,
// 0.5 half the angle - and the full angle is returned either way.
TEST(Ik_solver, pole_weight_scales_the_swivel)
{
    // Bend toward +X about the +Y root-to-effector line; the pole is +Z, a
    // quarter turn away.
    const std::vector<vec3> bent = { vec3{0.0f, 0.0f, 0.0f}, vec3{1.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
    const vec3 pole{0.0f, 1.0f, 5.0f};
    const vec3 a   {0.0f, 1.0f, 0.0f};
    const vec3 d   {0.0f, 0.0f, 1.0f};
    const float full = signed_angle_about(a, vec3{1.0f, 0.0f, 0.0f}, d);
    ASSERT_NEAR(std::abs(full), 0.5f * c_pi, 1.0e-6f);

    for (const float weight : { 0.0f, 0.5f, 1.0f }) {
        std::vector<vec3> positions = bent;
        const std::optional<float> swivel = editor::ik_apply_pole(positions, pole, 0.0f, weight);
        ASSERT_TRUE(swivel.has_value()) << "weight " << weight;
        EXPECT_NEAR(swivel.value(), full, 1.0e-5f) << "the full angle is returned at weight " << weight;
        if (weight == 0.0f) {
            EXPECT_EQ(positions, bent) << "weight 0 leaves the positions untouched";
            continue;
        }
        // The swivel is a rotation about the axis by weight times the full angle.
        const vec3 offset = positions[1] - positions[0];
        const vec3 b      = normalize(offset - (a * dot(offset, a)));
        EXPECT_NEAR(signed_angle_about(a, vec3{1.0f, 0.0f, 0.0f}, b), weight * full, 1.0e-5f) << "weight " << weight;
        EXPECT_NEAR(signed_angle_about(a, b, d), (1.0f - weight) * full, 1.0e-5f) << "weight " << weight;
        EXPECT_EQ(positions.front(), bent.front());
        EXPECT_EQ(positions.back(),  bent.back());
        EXPECT_NEAR(distance(positions[0], positions[1]), distance(bent[0], bent[1]), 1.0e-5f);
    }

    // An undefined swivel returns nothing.
    std::vector<vec3> straight = { vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f} };
    EXPECT_FALSE(editor::ik_apply_pole(straight, pole, 0.0f, 0.5f).has_value());
}

// R28: the weight applies on both solver paths. The solved bend sits
// (1 - weight) times the unpoled bend's angle off the pole; the constrained
// path's iterations hold that residual rather than compounding the weight
// toward a full snap.
TEST(Ik_solver, pole_weight_applies_on_both_solver_paths)
{
    for (const bool constrained : { false, true }) {
        // Five joints, so the solve takes several iterations: a weight applied
        // afresh in each would compound toward the full swivel.
        const auto make_chain = [constrained]() -> editor::Ik_chain {
            editor::Ik_chain chain = make_straight_five_joint_chain();
            if (constrained) {
                // Enabled with no lock or limit: the constrained path, nothing clamped.
                chain.constraints[1].enabled    = true;
                chain.constraints[1].twist_axis = 1;
            }
            chain.target        = vec3{1.5f, 1.5f, 0.3f};
            chain.pole_position = vec3{0.0f, 1.0f, 7.0f};
            return chain;
        };
        editor::Fabrik_solver solver;

        editor::Ik_chain unpoled = make_chain();
        ASSERT_EQ(unpoled.has_constraints(), constrained);
        solver.solve(unpoled);
        const vec3  a        = chain_axis(unpoled);
        const vec3  d        = normalize(perpendicular_offset(unpoled.pole_position, unpoled.positions.front(), a));
        const float off_pole = signed_angle_about(a, chain_bend_direction(unpoled), d);
        ASSERT_GT(std::abs(off_pole), 0.25f * c_pi) << "the unpoled bend is well off the pole";

        for (const float weight : { 0.0f, 0.25f, 0.5f, 1.0f }) {
            editor::Ik_chain chain = make_chain();
            chain.has_pole    = true;
            chain.pole_weight = weight;
            solver.solve(chain);
            const vec3 solved_axis = chain_axis(chain);
            const vec3 solved_d    = normalize(perpendicular_offset(chain.pole_position, chain.positions.front(), solved_axis));
            EXPECT_NEAR(
                signed_angle_about(solved_axis, chain_bend_direction(chain), solved_d),
                (1.0f - weight) * off_pole,
                2.0e-3f
            ) << (constrained ? "constrained" : "unconstrained") << " weight " << weight;
            EXPECT_LT(distance(chain.positions.back(), chain.target), 1.0e-2f);
            for (std::size_t i = 0; i + 1 < chain.positions.size(); ++i) {
                EXPECT_NEAR(distance(chain.positions[i], chain.positions[i + 1]), 1.0f, 1.0e-4f) << "segment " << i;
            }
        }
    }
}

TEST(Ik_solver, limits_win_over_pole)
{
    // The pole pulls the elbow toward +X, which for a chain along +Y is a
    // swing about Z; the tight Z limit therefore fights the pole (R14).
    const float max_bend = 0.05f;
    const auto make_poled_chain = [](const float z_min, const float z_max) -> editor::Ik_chain {
        editor::Ik_chain chain = make_straight_chain();
        chain.constraints[1].enabled    = true;
        chain.constraints[1].twist_axis = 1;
        chain.constraints[1].limit[2]   = true;
        chain.constraints[1].limit_min  = vec3{-c_pi, -c_pi, z_min};
        chain.constraints[1].limit_max  = vec3{c_pi, c_pi, z_max};
        chain.target        = vec3{0.0f, 1.0f, 0.6f};
        chain.has_pole      = true;
        chain.pole_position = vec3{5.0f, 1.0f, 0.0f};
        return chain;
    };

    editor::Ik_chain limited = make_poled_chain(-max_bend, max_bend);
    editor::Ik_chain loose   = make_poled_chain(-c_pi, c_pi);

    editor::Fabrik_solver solver;
    solver.solve(limited);
    solver.solve(loose);

    const float z_angle = swing_angle_about(quat{1.0f, 0.0f, 0.0f, 0.0f}, limited.local_rotations[1], 2, 1);
    EXPECT_LE(std::abs(z_angle), max_bend + 1.0e-3f);
    EXPECT_GT(distance(limited.positions[1], loose.positions[1]), 1.0e-2f);
    for (const vec3& p : limited.positions) {
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }
}

// Scenarios of the check 8.3 stability sweep of
// doc/plans/rigging/interactive_test_pass.md (scripts/ik_interactive_pass_verify.py
// make_sweep_scenarios, seed 20260925), replayed without the editor: three
// unit bones along +Y, each bent about its local X, per-bone settings, a
// pole, and a random walk of the target from the tip's drag-start position;
// each step solves from the drag-start pose (Solve From "Drag Start").
enum class Sweep_setting : int { free, lock_y, hinge, limit_x, limit_z };

class Sweep_bone
{
public:
    float         bend_deg{0.0f};
    Sweep_setting setting{Sweep_setting::free};
    float         limit_min_deg{0.0f};
    float         limit_max_deg{0.0f};
};

class Sweep_scenario
{
public:
    std::array<Sweep_bone, 3> bones;
    vec3                      pole{0.0f};
    std::vector<vec3>         walk;
};

[[nodiscard]] auto make_sweep_chain(const Sweep_scenario& scenario) -> editor::Ik_chain
{
    editor::Ik_chain chain;
    chain.positions.assign(4, vec3{0.0f});
    chain.lengths = { 1.0f, 1.0f, 1.0f };
    chain.local_rotations.assign(4, quat{1.0f, 0.0f, 0.0f, 0.0f});
    chain.child_dir_local.assign(3, vec3{0.0f, 1.0f, 0.0f});
    chain.constraints.resize(4);
    quat world{1.0f, 0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < 3; ++i) {
        const Sweep_bone& bone = scenario.bones[i];
        chain.local_rotations[i] = angleAxis(radians(bone.bend_deg), vec3{1.0f, 0.0f, 0.0f});
        world = world * chain.local_rotations[i];
        chain.positions[i + 1] = chain.positions[i] + (world * vec3{0.0f, 1.0f, 0.0f});

        editor::Ik_joint_constraint& constraint = chain.constraints[i];
        constraint.enabled    = true;
        constraint.twist_axis = 1;
        constraint.limit_min  = vec3{-c_pi, -c_pi, -c_pi};
        constraint.limit_max  = vec3{c_pi, c_pi, c_pi};
        switch (bone.setting) {
            case Sweep_setting::free:    break;
            case Sweep_setting::lock_y:  constraint.lock[1] = true; break;
            case Sweep_setting::hinge:   constraint.lock[1] = true; constraint.lock[2] = true; break;
            case Sweep_setting::limit_x: {
                constraint.limit[0]    = true;
                constraint.limit_min.x = radians(bone.limit_min_deg);
                constraint.limit_max.x = radians(bone.limit_max_deg);
                break;
            }
            case Sweep_setting::limit_z: {
                constraint.limit[2]    = true;
                constraint.limit_min.z = radians(bone.limit_min_deg);
                constraint.limit_max.z = radians(bone.limit_max_deg);
                break;
            }
        }
    }
    chain.pole_position = scenario.pole;
    return chain;
}

// Sweep scenario 0, finding F7: a hinge on the root, a pole off the bend
// plane.
[[nodiscard]] auto make_sweep_scenario_0() -> Sweep_scenario
{
    Sweep_scenario scenario;
    scenario.bones = {
        Sweep_bone{.bend_deg = -13.646430f, .setting = Sweep_setting::hinge},
        Sweep_bone{.bend_deg = -29.603832f, .setting = Sweep_setting::limit_x, .limit_min_deg = -29.603832f - 26.997476f, .limit_max_deg = 21.779045f},
        Sweep_bone{.bend_deg =   0.814712f, .setting = Sweep_setting::limit_z, .limit_min_deg = -38.711265f, .limit_max_deg = 17.935194f}
    };
    scenario.pole = vec3{1.302905f, 0.897294f, 0.434952f};
    scenario.walk = {
        vec3{0.034289f, -0.035602f, -0.007535f}, vec3{0.063111f, -0.070364f, 0.013933f},
        vec3{0.079539f, -0.109499f, 0.040365f},  vec3{0.088462f, -0.142737f, 0.076636f},
        vec3{0.093946f, -0.151904f, 0.125482f},  vec3{0.074762f, -0.161341f, 0.170680f},
        vec3{0.059138f, -0.143778f, 0.214810f},  vec3{0.038221f, -0.120531f, 0.253824f},
        vec3{0.011191f, -0.128038f, 0.295212f},  vec3{-0.010393f, -0.104750f, 0.333836f},
        vec3{-0.037713f, -0.085518f, 0.371035f}, vec3{-0.055247f, -0.054976f, 0.406528f},
        vec3{-0.053588f, -0.035421f, 0.452516f}, vec3{-0.066310f, -0.010861f, 0.494168f},
        vec3{-0.069412f, 0.006785f, 0.540848f},  vec3{-0.093146f, 0.016863f, 0.583687f},
        vec3{-0.132679f, 0.040088f, 0.603630f},  vec3{-0.153419f, 0.084525f, 0.613387f},
        vec3{-0.181932f, 0.120944f, 0.594397f},  vec3{-0.216678f, 0.153484f, 0.579102f},
        vec3{-0.235982f, 0.182891f, 0.543570f},  vec3{-0.242774f, 0.201599f, 0.497701f},
        vec3{-0.271401f, 0.224810f, 0.463912f},  vec3{-0.292997f, 0.242788f, 0.422555f}
    };
    return scenario;
}

// Sweep scenario 10: Z limits on the root and the last bone, the middle
// bone's twist locked, a pole.
[[nodiscard]] auto make_sweep_scenario_10() -> Sweep_scenario
{
    Sweep_scenario scenario;
    scenario.bones = {
        Sweep_bone{.bend_deg = -21.248120f, .setting = Sweep_setting::limit_z, .limit_min_deg = -11.283419f, .limit_max_deg = 10.841838f},
        Sweep_bone{.bend_deg =   9.093844f, .setting = Sweep_setting::lock_y},
        Sweep_bone{.bend_deg =  10.606820f, .setting = Sweep_setting::limit_z, .limit_min_deg = -16.522818f, .limit_max_deg = 28.259948f}
    };
    scenario.pole = vec3{-1.089546f, 0.730551f, 0.686175f};
    scenario.walk = {
        vec3{-0.017076f, 0.045448f, 0.011955f}, vec3{-0.019100f, 0.082673f, 0.045275f}, vec3{-0.047115f, 0.103697f, 0.080956f},
        vec3{-0.057677f, 0.102035f, 0.129800f}, vec3{-0.062338f, 0.071373f, 0.169019f}, vec3{-0.084526f, 0.046815f, 0.206496f},
        vec3{-0.080471f, 0.013109f, 0.243204f}, vec3{-0.048651f, -0.008202f, 0.275349f}, vec3{-0.028484f, -0.050991f, 0.291549f},
        vec3{-0.014271f, -0.089817f, 0.319664f}, vec3{-0.029345f, -0.133782f, 0.338100f}, vec3{-0.055082f, -0.175759f, 0.329409f},
        vec3{-0.064055f, -0.212472f, 0.296673f}, vec3{-0.087114f, -0.246293f, 0.267960f}, vec3{-0.080890f, -0.283089f, 0.234683f},
        vec3{-0.068026f, -0.330849f, 0.227372f}, vec3{-0.066870f, -0.377867f, 0.210401f}, vec3{-0.046499f, -0.423516f, 0.211491f},
        vec3{-0.045736f, -0.472854f, 0.203421f}, vec3{-0.029811f, -0.517947f, 0.188826f}, vec3{-0.000465f, -0.543739f, 0.157624f},
        vec3{0.021964f, -0.586941f, 0.146197f}, vec3{0.061281f, -0.617800f, 0.144831f}, vec3{0.107417f, -0.631858f, 0.131646f}
    };
    return scenario;
}

// One sweep scenario dragged under both Solve From variants
// (doc/plans/rigging/ik_drag_options.md R25, R26): drag_start solves every
// step from the drag-start pose, previous_step from the pose the previous
// step left (positions and local rotations, as Ik_drag::apply loads them).
enum class Sweep_solve_from : int { drag_start, previous_step };

// Per step of one drag: the largest intermediate-joint displacement over
// the target's step, and the effector's distance to the target.
class Sweep_drag
{
public:
    std::vector<float> ratio;
    std::vector<float> error;
};

constexpr float c_sweep_step      = 0.05f; // the sweep's target step
constexpr float c_sweep_jump      = 5.0f;  // JUMP_RATIO of the sweep
constexpr float c_sweep_reach_tol = 1.0e-3f;

[[nodiscard]] auto drag_sweep_scenario(
    const Sweep_scenario&  scenario,
    const Sweep_solve_from solve_from,
    const bool             has_pole
) -> Sweep_drag
{
    const editor::Ik_chain start = make_sweep_chain(scenario);
    editor::Fabrik_solver  solver;
    Sweep_drag             drag;
    editor::Ik_chain       previous = start;
    for (std::size_t step = 0; step < scenario.walk.size(); ++step) {
        editor::Ik_chain chain = (solve_from == Sweep_solve_from::previous_step) ? previous : start;
        chain.target   = start.positions.back() + scenario.walk[step];
        chain.has_pole = has_pole;
        solver.solve(chain);

        float moved = 0.0f;
        for (std::size_t i = 1; i + 1 < chain.positions.size(); ++i) { // intermediate joints, as the sweep measures
            moved = std::max(moved, distance(previous.positions[i], chain.positions[i]));
        }
        drag.ratio.push_back(moved / c_sweep_step);
        drag.error.push_back(distance(chain.positions.back(), chain.target));
        for (std::size_t i = 0; i + 1 < chain.positions.size(); ++i) {
            EXPECT_NEAR(distance(chain.positions[i], chain.positions[i + 1]), 1.0f, 1.0e-4f) << "step " << step;
        }
        previous = chain;
    }
    return drag;
}

// The rule of check 8.3: a previous-step drag is continuous (no step moves a
// joint more than 5 times the target's step); a drag-start drag is a
// function of the target and may change solution family between nearby
// targets (ik_settings.md section 4, "Solution families"), so it carries no
// continuity assertion. Bone lengths hold in both (drag_sweep_scenario). The
// pole costs no reach: the poled drag-start solve reaches every target the
// unpoled one reaches.
// The first step carries the pole alignment (Snap, ik_drag_options.md R27)
// and is not judged for jumps.
void expect_sweep_rule(const Sweep_scenario& scenario)
{
    const Sweep_drag drag_start    = drag_sweep_scenario(scenario, Sweep_solve_from::drag_start,    true);
    const Sweep_drag previous_step = drag_sweep_scenario(scenario, Sweep_solve_from::previous_step, true);
    const Sweep_drag unpoled       = drag_sweep_scenario(scenario, Sweep_solve_from::drag_start,    false);
    for (std::size_t step = 1; step < drag_start.ratio.size(); ++step) {
        EXPECT_LT(previous_step.ratio[step], c_sweep_jump)
            << "previous_step step " << step << ": joints moved " << previous_step.ratio[step] << "x the target step";
    }
    for (std::size_t step = 0; step < drag_start.error.size(); ++step) {
        if (unpoled.error[step] < c_sweep_reach_tol) {
            EXPECT_LT(drag_start.error[step], c_sweep_reach_tol)
                << "step " << step << ": the unpoled solve reaches the target, the poled one is " << drag_start.error[step] << " off";
        }
    }
}

// Finding F7 (a hinge root with a pole) and F8 (two solution families).
TEST(Ik_solver, sweep_scenario_0_hinge_root_with_pole)
{
    expect_sweep_rule(make_sweep_scenario_0());
}

TEST(Ik_solver, sweep_scenario_10_limited_chain_with_pole)
{
    expect_sweep_rule(make_sweep_scenario_10());
}

TEST(Ik_solver, two_joint_chain_ignores_pole)
{
    editor::Ik_chain without;
    without.positions       = { vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f} };
    without.lengths         = { 1.0f };
    without.local_rotations = { quat{1.0f, 0.0f, 0.0f, 0.0f}, quat{1.0f, 0.0f, 0.0f, 0.0f} };
    without.child_dir_local = { vec3{0.0f, 1.0f, 0.0f} };
    without.constraints.resize(2);
    without.target = vec3{0.5f, 0.5f, 0.2f};

    editor::Ik_chain with = without;
    with.has_pole      = true;
    with.pole_position = vec3{0.0f, 0.0f, 5.0f};
    with.pole_angle    = 0.7f;

    editor::Fabrik_solver solver;
    solver.solve(without);
    solver.solve(with);

    EXPECT_EQ(with.positions, without.positions);
}

TEST(Ik_solver, poled_unreachable_target_keeps_straight_layout)
{
    editor::Ik_chain without = make_straight_chain();
    without.target = vec3{10.0f, 0.0f, 0.0f};

    editor::Ik_chain with = without;
    with.has_pole      = true;
    with.pole_position = vec3{0.0f, 0.0f, 5.0f};
    with.pole_angle    = 0.4f;

    editor::Fabrik_solver solver;
    solver.solve(without);
    solver.solve(with);

    EXPECT_EQ(with.positions, without.positions); // R11 step 3: no bend to aim
    for (const vec3& p : with.positions) {
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }
    // Straight layout toward the unreachable target.
    EXPECT_NEAR(dot(
        normalize(with.positions[1] - with.positions[0]),
        normalize(with.positions[2] - with.positions[1])), 1.0f, 1.0e-4f);
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
    ASSERT_FALSE(chain.has_pole); // the routing of Ik_drag::apply on a pole-free chain
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

TEST(Ik_solver, twist_only_constraint_still_reaches_the_target)
{
    editor::Ik_chain constrained = make_straight_chain();
    constrained.constraints[1].enabled    = true;
    constrained.constraints[1].twist_axis = 1;
    constrained.constraints[1].lock[1]    = true;  // twist axis lock
    constrained.constraints[1].limit[1]   = true;  // twist axis limit
    constrained.constraints[1].limit_min  = vec3{-c_pi, -0.01f, -c_pi};
    constrained.constraints[1].limit_max  = vec3{c_pi, 0.01f, c_pi};
    constrained.target = vec3{0.4f, 1.3f, 0.5f};

    editor::Fabrik_solver solver;
    solver.solve(constrained);

    // The effector still reaches the target: a twist-axis constraint never
    // moves the joint's own child (twist turns about the child direction).
    EXPECT_LT(distance(constrained.positions[2], constrained.target), 0.02f);
}

// A joint locked on its twist axis and on one swing axis is a hinge: with
// every joint a hinge about X, an out-of-plane target leaves each local
// rotation a pure X rotation. World-space shortest arcs composed onto a bent
// parent chain carry twist, so this holds only while the twist lock is
// enforced.
TEST(Ik_solver, twist_and_swing_lock_make_a_hinge)
{
    editor::Ik_chain chain;
    chain.positions       = { vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f}, vec3{0.0f, 3.0f, 0.0f} };
    chain.lengths         = { 1.0f, 1.0f, 1.0f };
    chain.local_rotations.assign(4, quat{1.0f, 0.0f, 0.0f, 0.0f});
    chain.child_dir_local.assign(3, vec3{0.0f, 1.0f, 0.0f});
    chain.constraints.resize(4);
    for (std::size_t j = 0; j < 3; ++j) {
        chain.constraints[j].enabled    = true;
        chain.constraints[j].twist_axis = 1;
        chain.constraints[j].lock[1]    = true;
        chain.constraints[j].lock[2]    = true;
    }
    chain.target = vec3{1.2f, 1.8f, 0.6f};

    editor::Fabrik_solver solver;
    solver.solve(chain);

    bool any_bend = false;
    for (std::size_t j = 0; j < 3; ++j) {
        const quat& q = chain.local_rotations[j];
        EXPECT_NEAR(q.y, 0.0f, 1.0e-5f) << "joint " << j;
        EXPECT_NEAR(q.z, 0.0f, 1.0e-5f) << "joint " << j;
        any_bend = any_bend || (std::abs(q.x) > 1.0e-3f);
        EXPECT_NEAR(chain.positions[j + 1].x, 0.0f, 1.0e-4f) << "joint " << j;
    }
    EXPECT_TRUE(any_bend) << "the hinge axis stays free";
}

// Chain visualization line list (doc/plans/rigging/ik_drag_options.md R16,
// R17). A bent three-joint chain: root, elbow, effector.
[[nodiscard]] auto make_drag_line_positions() -> std::vector<vec3>
{
    return { vec3{0.0f, 0.0f, 0.0f}, vec3{0.6f, 0.8f, 0.0f}, vec3{0.0f, 1.6f, 0.0f} };
}

TEST(Ik_drag_lines, three_joint_chain_without_pole)
{
    const std::vector<vec3>    positions = make_drag_line_positions();
    editor::Ik_drag_line_input input{};
    input.joint_positions = positions;

    editor::Ik_drag_line_buffer buffer;
    editor::build_ik_drag_lines(input, buffer);

    // Two segment lines, no pole line; a three-arm cross at the root and one
    // at the effector.
    EXPECT_EQ(buffer.path_lines.size(),   2u);
    EXPECT_EQ(buffer.marker_lines.size(), 6u);
    EXPECT_EQ(buffer.line_count(),        8u);
    EXPECT_EQ(buffer.path_lines[0].p0, positions[0]);
    EXPECT_EQ(buffer.path_lines[0].p1, positions[1]);
    EXPECT_EQ(buffer.path_lines[1].p0, positions[1]);
    EXPECT_EQ(buffer.path_lines[1].p1, positions[2]);
    EXPECT_EQ(buffer.path_lines[0].color, input.chain_color);

    // Marker arms are a fraction of the chain's reach, centered on the joint.
    const float reach = distance(positions[0], positions[1]) + distance(positions[1], positions[2]);
    const float arm   = reach * input.marker_scale;
    EXPECT_NEAR(distance(buffer.marker_lines[0].p0, buffer.marker_lines[0].p1), 2.0f * arm, 1.0e-5f);
    for (std::size_t i = 0; i < 3; ++i) {
        const vec3 center = 0.5f * (buffer.marker_lines[i].p0 + buffer.marker_lines[i].p1);
        EXPECT_LT(distance(center, positions[0]), 1.0e-5f);
        EXPECT_EQ(buffer.marker_lines[i].color, input.root_color);
    }
    for (std::size_t i = 3; i < 6; ++i) {
        const vec3 center = 0.5f * (buffer.marker_lines[i].p0 + buffer.marker_lines[i].p1);
        EXPECT_LT(distance(center, positions[2]), 1.0e-5f);
        EXPECT_EQ(buffer.marker_lines[i].color, input.chain_color); // effector marker: chain color
    }
}

TEST(Ik_drag_lines, pole_adds_line_to_root_and_marker)
{
    const std::vector<vec3>    positions     = make_drag_line_positions();
    const vec3                 pole_position = vec3{2.0f, 0.5f, -1.0f};
    editor::Ik_drag_line_input input{};
    input.joint_positions = positions;
    input.pole_position   = pole_position;

    editor::Ik_drag_line_buffer buffer;
    editor::build_ik_drag_lines(input, buffer);

    EXPECT_EQ(buffer.path_lines.size(),   3u);
    EXPECT_EQ(buffer.marker_lines.size(), 9u);
    EXPECT_EQ(buffer.path_lines[2].p0,    pole_position);
    EXPECT_EQ(buffer.path_lines[2].p1,    positions[0]); // pole line ends at the chain root
    EXPECT_EQ(buffer.path_lines[2].color, input.pole_color);
    for (std::size_t i = 6; i < 9; ++i) {
        const vec3 center = 0.5f * (buffer.marker_lines[i].p0 + buffer.marker_lines[i].p1);
        EXPECT_LT(distance(center, pole_position), 1.0e-5f);
        EXPECT_EQ(buffer.marker_lines[i].color, input.pole_color);
    }
}

TEST(Ik_drag_lines, lower_chain_continues_polyline_and_marks_pinned_end)
{
    // ik_drag_options.md R24: under Pin Chain End the polyline continues from
    // the effector through the lower chain, and the end joint gets a cross in
    // the root color.
    const std::vector<vec3> upper = make_drag_line_positions();
    const std::vector<vec3> lower = { upper.back(), upper.back() + vec3{0.0f, 1.0f, 0.0f}, upper.back() + vec3{0.5f, 2.0f, 0.0f} };
    editor::Ik_drag_line_input input{};
    input.joint_positions       = upper;
    input.lower_joint_positions = lower;

    editor::Ik_drag_line_buffer buffer;
    editor::build_ik_drag_lines(input, buffer);

    ASSERT_EQ(buffer.path_lines.size(),   4u); // two upper segments, two lower
    ASSERT_EQ(buffer.marker_lines.size(), 9u); // root, effector, end joint
    EXPECT_EQ(buffer.path_lines[2].p0,    lower[0]);
    EXPECT_EQ(buffer.path_lines[2].p1,    lower[1]);
    EXPECT_EQ(buffer.path_lines[3].p0,    lower[1]);
    EXPECT_EQ(buffer.path_lines[3].p1,    lower[2]);
    EXPECT_EQ(buffer.path_lines[3].color, input.chain_color);

    // Marker arms are a fraction of the whole drawn polyline's reach.
    const float reach =
        distance(upper[0], upper[1]) + distance(upper[1], upper[2]) +
        distance(lower[0], lower[1]) + distance(lower[1], lower[2]);
    const float arm = reach * input.marker_scale;
    for (std::size_t i = 6; i < 9; ++i) {
        const vec3 center = 0.5f * (buffer.marker_lines[i].p0 + buffer.marker_lines[i].p1);
        EXPECT_LT(distance(center, lower[2]), 1.0e-5f);
        EXPECT_NEAR(distance(buffer.marker_lines[i].p0, buffer.marker_lines[i].p1), 2.0f * arm, 1.0e-5f);
        EXPECT_EQ(buffer.marker_lines[i].color, input.root_color); // held fixed like the root
    }

    // A lower span of one position (the effector alone) is no lower chain.
    input.lower_joint_positions = std::span<const vec3>{lower}.first(1);
    editor::build_ik_drag_lines(input, buffer);
    EXPECT_EQ(buffer.path_lines.size(),   2u);
    EXPECT_EQ(buffer.marker_lines.size(), 6u);
}

TEST(Ik_drag_lines, refill_is_stable_and_allocation_free)
{
    const std::vector<vec3>    positions = make_drag_line_positions();
    editor::Ik_drag_line_input input{};
    input.joint_positions = positions;
    input.pole_position   = vec3{2.0f, 0.5f, -1.0f};

    editor::Ik_drag_line_buffer buffer;
    editor::build_ik_drag_lines(input, buffer);
    const std::size_t path_count       = buffer.path_lines.size();
    const std::size_t marker_count     = buffer.marker_lines.size();
    const std::size_t path_capacity    = buffer.path_lines.capacity();
    const std::size_t marker_capacity  = buffer.marker_lines.capacity();
    const editor::Ik_drag_line first_path_line = buffer.path_lines[0];

    // The high-water mark is reached: a second build clears and refills the
    // same storage, so a steady-state drag frame allocates nothing (R16).
    editor::build_ik_drag_lines(input, buffer);
    EXPECT_EQ(buffer.path_lines.size(),      path_count);
    EXPECT_EQ(buffer.marker_lines.size(),    marker_count);
    EXPECT_EQ(buffer.path_lines.capacity(),  path_capacity);
    EXPECT_EQ(buffer.marker_lines.capacity(), marker_capacity);
    EXPECT_EQ(buffer.path_lines[0].p0,       first_path_line.p0);
    EXPECT_EQ(buffer.path_lines[0].p1,       first_path_line.p1);

    // clear() keeps the capacity too.
    buffer.clear();
    EXPECT_EQ(buffer.line_count(),           0u);
    EXPECT_EQ(buffer.path_lines.capacity(),  path_capacity);
    EXPECT_EQ(buffer.marker_lines.capacity(), marker_capacity);
}

TEST(Ik_drag_lines, degenerate_inputs_draw_nothing_extra)
{
    editor::Ik_drag_line_buffer buffer;

    // Fewer than two joints: no chain to draw.
    const std::vector<vec3>    single = { vec3{1.0f, 2.0f, 3.0f} };
    editor::Ik_drag_line_input one_joint{};
    one_joint.joint_positions = single;
    editor::build_ik_drag_lines(one_joint, buffer);
    EXPECT_EQ(buffer.line_count(), 0u);

    // Zero reach: the segment lines exist (degenerate) but the markers, whose
    // arm is a fraction of the reach, are left out.
    const std::vector<vec3>    folded = { vec3{1.0f}, vec3{1.0f}, vec3{1.0f} };
    editor::Ik_drag_line_input zero_reach{};
    zero_reach.joint_positions = folded;
    editor::build_ik_drag_lines(zero_reach, buffer);
    EXPECT_EQ(buffer.path_lines.size(),   2u);
    EXPECT_EQ(buffer.marker_lines.size(), 0u);
}

} // anonymous namespace
