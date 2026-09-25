// Side-suffix recognition and flipping (doc/plans/rigging/skeleton_editing.md
// R11): every spelling family, whole trailing words, trailing index groups,
// names without a side, and round trips.

#include "rig/bone_naming.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace {

using editor::Bone_side;
using editor::bone_side;
using editor::flip_side_name;

class Flip_case
{
public:
    std::string_view name;
    Bone_side        side;
    std::string_view flipped;
};

constexpr std::array<Flip_case, 26> c_flip_cases{{
    // Separator suffixes, each spelling family both ways.
    {"arm.L",          Bone_side::left,  "arm.R"         },
    {"arm.R",          Bone_side::right, "arm.L"         },
    {"arm_L",          Bone_side::left,  "arm_R"         },
    {"arm_R",          Bone_side::right, "arm_L"         },
    {"arm.l",          Bone_side::left,  "arm.r"         },
    {"arm.r",          Bone_side::right, "arm.l"         },
    {"arm_l",          Bone_side::left,  "arm_r"         },
    {"arm_r",          Bone_side::right, "arm_l"         },
    // Whole trailing words.
    {"HandLeft",       Bone_side::left,  "HandRight"     },
    {"HandRight",      Bone_side::right, "HandLeft"      },
    {"hand_Left",      Bone_side::left,  "hand_Right"    },
    {"hand.Right",     Bone_side::right, "hand.Left"     },
    {"Left",           Bone_side::left,  "Right"         },
    {"Right",          Bone_side::right, "Left"          },
    {"hand_left",      Bone_side::left,  "hand_right"    },
    {"hand right",     Bone_side::right, "hand left"     },
    {"left",           Bone_side::left,  "right"         },
    {"right",          Bone_side::right, "left"          },
    // Trailing index groups are kept.
    {"arm.L.001",      Bone_side::left,  "arm.R.001"     },
    {"arm_joint_L_1",  Bone_side::left,  "arm_joint_R_1" },
    {"leg_joint_R_5",  Bone_side::right, "leg_joint_L_5" },
    {"HandLeft.002",   Bone_side::left,  "HandRight.002" },
    {"hand_right_3",   Bone_side::right, "hand_left_3"   },
    {"foot.r.12",      Bone_side::right, "foot.l.12"     },
    // A side marker in the middle of a longer word stem is not a side.
    {"spine_L_mid",    Bone_side::none,  "spine_L_mid"   },
    {"Left_arm",       Bone_side::none,  "Left_arm"      }
}};

TEST(Bone_naming, recognizes_and_flips_every_spelling_family)
{
    for (const Flip_case& c : c_flip_cases) {
        EXPECT_EQ(bone_side(c.name), c.side) << c.name;
        EXPECT_EQ(flip_side_name(c.name), c.flipped) << c.name;
    }
}

TEST(Bone_naming, flip_keeps_the_spelling_family)
{
    EXPECT_EQ(flip_side_name("a.L"), "a.R");
    EXPECT_EQ(flip_side_name("a_L"), "a_R");
    EXPECT_EQ(flip_side_name("a.l"), "a.r");
    EXPECT_EQ(flip_side_name("a_l"), "a_r");
    EXPECT_EQ(flip_side_name("aLeft"), "aRight");
    EXPECT_EQ(flip_side_name("a_left"), "a_right");
}

TEST(Bone_naming, names_without_a_side)
{
    constexpr std::array<std::string_view, 17> names{{
        "",
        "spine",
        "L",
        "R",
        "_L",
        ".R",
        "Hips",
        "cleft",       // "left" inside a word
        "upright",     // "right" inside a word
        "Bright",      // "right" after a letter
        "HLeft",       // "Left" after an uppercase letter
        "handleft",    // lowercase "left" after a letter
        "hand2left",   // lowercase "left" after a digit
        "arm.X",
        "arm_1",
        "arm.001",
        "Armature"
    }};
    for (const std::string_view name : names) {
        EXPECT_EQ(bone_side(name), Bone_side::none) << name;
        EXPECT_EQ(flip_side_name(name), name) << name;
    }
}

TEST(Bone_naming, flip_is_an_involution)
{
    for (const Flip_case& c : c_flip_cases) {
        const std::string once  = flip_side_name(c.name);
        const std::string twice = flip_side_name(once);
        EXPECT_EQ(twice, c.name) << c.name;
        if (c.side != Bone_side::none) {
            const Bone_side expected = (c.side == Bone_side::left) ? Bone_side::right : Bone_side::left;
            EXPECT_EQ(bone_side(once), expected) << c.name;
        }
    }
}

} // anonymous namespace
