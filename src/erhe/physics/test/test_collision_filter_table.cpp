#include "erhe_physics/box3d/box3d_collision_filter_table.hpp"
#include "erhe_physics/collision_filter.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using erhe::physics::Box3d_collision_filter_table;
using erhe::physics::Collision_filter;

[[nodiscard]] auto make_filter(
    const std::vector<std::string>& systems,
    const std::vector<std::string>& collide_with,
    const std::vector<std::string>& not_collide_with
) -> std::shared_ptr<Collision_filter>
{
    std::shared_ptr<Collision_filter> filter = std::make_shared<Collision_filter>();
    filter->set_collision_systems       (systems);
    filter->set_collide_with_systems    (collide_with);
    filter->set_not_collide_with_systems(not_collide_with);
    return filter;
}

} // anonymous namespace

TEST(collision_filter_table, interning_is_stable_and_distinct)
{
    Box3d_collision_filter_table table;
    const int a  = table.intern_system("player");
    const int b  = table.intern_system("enemy");
    const int a2 = table.intern_system("player");
    EXPECT_EQ(a, a2);
    EXPECT_NE(a, b);
    EXPECT_EQ(table.get_system_count(), 2u);
}

TEST(collision_filter_table, system_budget_is_64_and_overflow_is_rejected)
{
    Box3d_collision_filter_table table;
    for (int i = 0; i < 64; ++i) {
        EXPECT_GE(table.intern_system("system_" + std::to_string(i)), 0) << "index " << i;
    }
    EXPECT_EQ(table.get_system_count(), 64u);
    // The 65th name is refused rather than aliased onto an existing bit.
    EXPECT_EQ(table.intern_system("one_too_many"), -1);
    EXPECT_EQ(table.get_system_count(), 64u);
}

TEST(collision_filter_table, no_filter_collides_with_everything)
{
    Box3d_collision_filter_table table;
    EXPECT_TRUE(
        table.should_collide(
            Box3d_collision_filter_table::no_collision_filter,
            Box3d_collision_filter_table::no_collision_filter
        )
    );
}

TEST(collision_filter_table, allow_list)
{
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player_filter  = make_filter({"player"},  {"terrain"}, {});
    const std::shared_ptr<Collision_filter> terrain_filter = make_filter({"terrain"}, {},          {});
    const std::shared_ptr<Collision_filter> debris_filter  = make_filter({"debris"},  {},          {});
    const int player  = table.get_or_compile(player_filter);
    const int terrain = table.get_or_compile(terrain_filter);
    const int debris  = table.get_or_compile(debris_filter);

    EXPECT_TRUE (table.should_collide(player, terrain));
    EXPECT_FALSE(table.should_collide(player, debris));
    // The test is bidirectional, so the order of the arguments does not matter.
    EXPECT_TRUE (table.should_collide(terrain, player));
    EXPECT_FALSE(table.should_collide(debris,  player));
}

TEST(collision_filter_table, allow_list_rejects_unfiltered_body)
{
    // An unfiltered body belongs to no collision system, so it satisfies no
    // allow list. This matches the Jolt backend.
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player_filter = make_filter({"player"}, {"terrain"}, {});
    const int player = table.get_or_compile(player_filter);
    EXPECT_FALSE(table.should_collide(player, Box3d_collision_filter_table::no_collision_filter));
}

TEST(collision_filter_table, deny_list)
{
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player_filter  = make_filter({"player"},  {}, {"debris"});
    const std::shared_ptr<Collision_filter> terrain_filter = make_filter({"terrain"}, {}, {});
    const std::shared_ptr<Collision_filter> debris_filter  = make_filter({"debris"},  {}, {});
    const int player  = table.get_or_compile(player_filter);
    const int terrain = table.get_or_compile(terrain_filter);
    const int debris  = table.get_or_compile(debris_filter);

    EXPECT_TRUE (table.should_collide(player, terrain));
    EXPECT_FALSE(table.should_collide(player, debris));
    EXPECT_FALSE(table.should_collide(debris, player));
}

TEST(collision_filter_table, deny_list_accepts_unfiltered_body)
{
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player_filter = make_filter({"player"}, {}, {"debris"});
    const int player = table.get_or_compile(player_filter);
    EXPECT_TRUE(table.should_collide(player, Box3d_collision_filter_table::no_collision_filter));
}

TEST(collision_filter_table, deny_list_with_multi_system_body)
{
    // This is the case Box3D's built-in category/mask filter cannot express:
    // the other body is in one denied and one allowed system, and the deny rule
    // must still reject the pair.
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player_filter = make_filter({"player"}, {}, {"debris"});
    const std::shared_ptr<Collision_filter> both_filter   = make_filter({"debris", "terrain"}, {}, {});
    const int player = table.get_or_compile(player_filter);
    const int both   = table.get_or_compile(both_filter);
    EXPECT_FALSE(table.should_collide(player, both));
}

TEST(collision_filter_table, collide_with_takes_precedence_over_not_collide_with)
{
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player_filter  = make_filter({"player"},  {"terrain"}, {"terrain"});
    const std::shared_ptr<Collision_filter> terrain_filter = make_filter({"terrain"}, {},          {});
    const int filter  = table.get_or_compile(player_filter);
    const int terrain = table.get_or_compile(terrain_filter);
    // A non-empty collide_with list selects allow-list semantics, so terrain is
    // allowed despite also appearing in not_collide_with.
    EXPECT_TRUE(table.should_collide(filter, terrain));
}

TEST(collision_filter_table, compilation_is_cached_per_item)
{
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> filter = make_filter({"player"}, {"terrain"}, {});
    const int first  = table.get_or_compile(filter);
    const int second = table.get_or_compile(filter);
    EXPECT_EQ(first, second);
}

TEST(collision_filter_table, recompile_picks_up_edits)
{
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> player = make_filter({"player"}, {"terrain"}, {});
    const std::shared_ptr<Collision_filter> debris = make_filter({"debris"}, {}, {});
    const int player_index = table.get_or_compile(player);
    const int debris_index = table.get_or_compile(debris);
    EXPECT_FALSE(table.should_collide(player_index, debris_index));

    player->set_collide_with_systems({"terrain", "debris"});
    // A cached compilation does not see the edit until recompile().
    EXPECT_FALSE(table.should_collide(player_index, debris_index));
    EXPECT_EQ(table.recompile(player), player_index);
    EXPECT_TRUE(table.should_collide(player_index, debris_index));
}

TEST(collision_filter_table, a_reused_address_does_not_inherit_the_dead_filter)
{
    // The cache is keyed by raw item pointer, so a filter freed and replaced by
    // a new allocation at the same address must not pick up the old compiled
    // bitsets. Looping makes the address reuse near certain.
    Box3d_collision_filter_table table;
    const std::shared_ptr<Collision_filter> debris = make_filter({"debris"}, {}, {});
    const int debris_index = table.get_or_compile(debris);

    int last_index = Box3d_collision_filter_table::no_collision_filter;
    for (int i = 0; i < 32; ++i) {
        // Allows debris.
        const std::shared_ptr<Collision_filter> allows = make_filter({"player"}, {"debris"}, {});
        EXPECT_TRUE(table.should_collide(table.get_or_compile(allows), debris_index)) << "iteration " << i;
    }
    for (int i = 0; i < 32; ++i) {
        // Denies debris. If a stale entry were reused, this would report a
        // collision because the previous filter allowed debris.
        const std::shared_ptr<Collision_filter> denies = make_filter({"player"}, {}, {"debris"});
        last_index = table.get_or_compile(denies);
        EXPECT_FALSE(table.should_collide(last_index, debris_index)) << "iteration " << i;
    }
    EXPECT_NE(last_index, Box3d_collision_filter_table::no_collision_filter);
}

TEST(collision_filter_table, null_filter_maps_to_no_collision_filter)
{
    Box3d_collision_filter_table table;
    EXPECT_EQ(table.get_or_compile({}), Box3d_collision_filter_table::no_collision_filter);
    EXPECT_EQ(table.recompile({}),      Box3d_collision_filter_table::no_collision_filter);
}
