#include "level/foliage_index.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using editor::Foliage_index;
using editor::Foliage_index_entry;
using editor::Foliage_query_path;
using Handle = Foliage_index::Handle;

// splitmix64: the same stream on every platform, unlike the standard distributions.
class Test_random
{
public:
    explicit Test_random(const std::uint64_t seed) : m_state{seed} {}

    auto next() -> std::uint64_t
    {
        m_state += 0x9e3779b97f4a7c15ull;
        std::uint64_t z = m_state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    auto below(const std::uint32_t count) -> std::uint32_t
    {
        return static_cast<std::uint32_t>(next() % count);
    }

    // [low, high) from 24 bits.
    auto range(const float low, const float high) -> float
    {
        const float unit = static_cast<float>(next() >> 40) / 16777216.0f;
        return low + ((high - low) * unit);
    }

private:
    std::uint64_t m_state;
};

class Mirror_entry
{
public:
    Foliage_index_entry entry;
    bool                live{false};
};

// The brute-force reference: one record per handle the index gave.
class Mirror
{
public:
    void set(const Handle handle, const Foliage_index_entry& entry)
    {
        if (handle >= entries.size()) {
            entries.resize(static_cast<std::size_t>(handle) + 1);
        }
        entries[handle].entry = entry;
        entries[handle].live  = true;
    }

    [[nodiscard]] auto find_within(const glm::vec3 center, const float distance) const -> std::vector<Handle>
    {
        std::vector<Handle> result;
        const float distance_squared = distance * distance;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].live) {
                continue;
            }
            const glm::vec3 d = entries[i].entry.position - center;
            if (glm::dot(d, d) <= distance_squared) {
                result.push_back(static_cast<Handle>(i));
            }
        }
        return result;
    }

    [[nodiscard]] auto get_max_radius() const -> float
    {
        float max_radius = 0.0f;
        for (const Mirror_entry& e : entries) {
            if (e.live) {
                max_radius = std::max(max_radius, e.entry.radius);
            }
        }
        return max_radius;
    }

    [[nodiscard]] auto get_live_handles() const -> std::vector<Handle>
    {
        std::vector<Handle> result;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].live) {
                result.push_back(static_cast<Handle>(i));
            }
        }
        return result;
    }

    std::vector<Mirror_entry> entries;
};

// A position in [-20, 20)^3; one in four coordinates lands exactly on a cell border.
auto make_position(Test_random& random) -> glm::vec3
{
    glm::vec3 p{0.0f};
    for (int axis = 0; axis < 3; ++axis) {
        if (random.below(4) == 0) {
            p[axis] = Foliage_index::cell_size * static_cast<float>(static_cast<int>(random.below(11)) - 5);
        } else {
            p[axis] = random.range(-20.0f, 20.0f);
        }
    }
    return p;
}

// Returns the number of entries found.
auto expect_same_as_mirror(
    const Foliage_index& index,
    const Mirror&        mirror,
    const glm::vec3      center,
    const float          distance,
    std::vector<Handle>& scratch
) -> std::size_t
{
    index.find_within(center, distance, scratch);
    std::vector<Handle> found = scratch;
    std::sort(found.begin(), found.end());
    const std::vector<Handle> expected = mirror.find_within(center, distance);
    EXPECT_EQ(found, expected) << "center " << center.x << " " << center.y << " " << center.z << " distance " << distance;
    EXPECT_EQ(index.count_within(center, distance), expected.size());
    return expected.size();
}

TEST(Foliage_index, matches_brute_force_through_inserts_moves_radius_writes_and_removals)
{
    Foliage_index       index;
    Mirror              mirror;
    Test_random         random{0x5eed'f011'a6e5'0001ull};
    std::vector<Handle> scratch;
    std::size_t         cell_walk_queries  = 0;
    std::size_t         dense_scan_queries = 0;
    std::size_t         non_empty_walks    = 0;
    std::size_t         partial_results    = 0;
    std::uint64_t       next_key           = 1;

    for (int step = 0; step < 4000; ++step) {
        const std::vector<Handle> live = mirror.get_live_handles();
        const std::uint32_t       roll = random.below(100);
        if (live.empty() || (roll < 40)) {
            const Foliage_index_entry entry{
                .position = make_position(random),
                .radius   = random.range(0.1f, 3.0f),
                .key      = next_key++
            };
            const Handle handle = index.insert(entry);
            mirror.set(handle, entry);
        } else if (roll < 65) {
            const Handle    handle   = live[random.below(static_cast<std::uint32_t>(live.size()))];
            const glm::vec3 position = make_position(random);
            index.move(handle, position);
            mirror.entries[handle].entry.position = position;
        } else if (roll < 80) {
            const Handle handle = live[random.below(static_cast<std::uint32_t>(live.size()))];
            const float  radius = random.range(0.0f, 4.0f);
            index.set_radius(handle, radius);
            mirror.entries[handle].entry.radius = radius;
        } else {
            const Handle handle = live[random.below(static_cast<std::uint32_t>(live.size()))];
            index.remove(handle);
            mirror.entries[handle].live = false;
        }

        ASSERT_EQ(index.get_count(), mirror.get_live_handles().size());
        if ((step % 10) != 0) {
            continue;
        }
        ASSERT_EQ(index.get_max_radius(), mirror.get_max_radius());
        for (const Handle handle : mirror.get_live_handles()) {
            const Foliage_index_entry& entry = index.get_entry(handle);
            ASSERT_EQ(entry.position, mirror.entries[handle].entry.position);
            ASSERT_EQ(entry.radius,   mirror.entries[handle].entry.radius);
            ASSERT_EQ(entry.key,      mirror.entries[handle].entry.key);
        }
        for (int query = 0; query < 4; ++query) {
            const glm::vec3 center   = make_position(random);
            const float     distance = (query < 3) ? random.range(0.0f, 4.5f) : random.range(10.0f, 60.0f);
            const bool        cell_walk = (index.get_query_path(center, distance) == Foliage_query_path::cell_walk);
            const std::size_t found     = expect_same_as_mirror(index, mirror, center, distance, scratch);
            if (cell_walk) {
                ++cell_walk_queries;
                if (found > 0) {
                    ++non_empty_walks;
                }
            } else {
                ++dense_scan_queries;
            }
            if (found < index.get_count()) {
                ++partial_results;
            }
        }
    }
    EXPECT_GT(cell_walk_queries,  100u);
    EXPECT_GT(dense_scan_queries, 100u);
    EXPECT_GT(non_empty_walks,    100u);
    EXPECT_GT(partial_results,    100u);
}

TEST(Foliage_index, find_within_takes_both_paths)
{
    Foliage_index       index;
    Mirror              mirror;
    Test_random         random{0x5eed'f011'a6e5'0002ull};
    std::vector<Handle> scratch;
    for (int i = 0; i < 200; ++i) {
        const Foliage_index_entry entry{.position = make_position(random), .radius = 0.5f, .key = static_cast<std::uint64_t>(i)};
        mirror.set(index.insert(entry), entry);
    }

    // A spacing query: 2 x 2 x 2 or 3 x 3 x 3 cells, fewer than the entries.
    const glm::vec3 small_center{-1.5f, 0.5f, 2.0f};
    EXPECT_EQ(index.get_query_path(small_center, 1.0f), Foliage_query_path::cell_walk);
    expect_same_as_mirror(index, mirror, small_center, 1.0f, scratch);
    expect_same_as_mirror(index, mirror, small_center, 4.0f, scratch);

    // A 50 m brush: more cells than entries.
    EXPECT_EQ(index.get_query_path(small_center, 50.0f), Foliage_query_path::dense_scan);
    expect_same_as_mirror(index, mirror, small_center, 50.0f, scratch);
    EXPECT_EQ(scratch.size(), 200u);

    // The caller's buffer keeps its capacity through a smaller query.
    const std::size_t capacity = scratch.capacity();
    index.find_within(glm::vec3{1000.0f}, 1.0f, scratch);
    EXPECT_TRUE(scratch.empty());
    EXPECT_EQ(scratch.capacity(), capacity);
}

TEST(Foliage_index, cell_key_at_negative_coordinates_and_cell_borders)
{
    EXPECT_EQ(Foliage_index::get_cell_coordinate( 0.0f),     0);
    EXPECT_EQ(Foliage_index::get_cell_coordinate(-0.0f),     0);
    EXPECT_EQ(Foliage_index::get_cell_coordinate( 3.999f),   0);
    EXPECT_EQ(Foliage_index::get_cell_coordinate( 4.0f),     1);
    EXPECT_EQ(Foliage_index::get_cell_coordinate(-0.001f),  -1);
    EXPECT_EQ(Foliage_index::get_cell_coordinate(-4.0f),    -1);
    EXPECT_EQ(Foliage_index::get_cell_coordinate(-4.001f),  -2);
    EXPECT_EQ(Foliage_index::get_cell_coordinate(-8.0f),    -2);
    EXPECT_EQ(Foliage_index::get_cell_coordinate( 1.0e30f),  Foliage_index::cell_coordinate_max);
    EXPECT_EQ(Foliage_index::get_cell_coordinate(-1.0e30f),  Foliage_index::cell_coordinate_min);

    // Each axis has its own field: no two of these share a key.
    const std::vector<glm::vec3> positions{
        { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f, -1.0f}, { 5.0f,  1.0f,  1.0f}, { 1.0f,  5.0f,  1.0f}, { 1.0f,  1.0f,  5.0f},
        {-1.0e30f, 1.0f, 1.0f}, {1.0e30f, 1.0f, 1.0f}, {1.0f, -1.0e30f, 1.0f}, {1.0f, 1.0f, 1.0e30f}
    };
    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (std::size_t j = i + 1; j < positions.size(); ++j) {
            EXPECT_NE(Foliage_index::get_cell_key(positions[i]), Foliage_index::get_cell_key(positions[j])) << i << " " << j;
        }
    }
    EXPECT_EQ(Foliage_index::get_cell_key({-4.0f, -4.0f, -4.0f}), Foliage_index::get_cell_key({-0.5f, -3.0f, -2.0f}));

    // Entries exactly on cell borders, found from the neighbouring cell at exactly the distance, on both paths.
    Foliage_index       index;
    Mirror              mirror;
    std::vector<Handle> scratch;
    const std::vector<glm::vec3> borders{
        { 4.0f,  0.0f,  0.0f}, {-4.0f, -4.0f, -4.0f}, { 0.0f,  0.0f,  0.0f}, {-8.0f,  4.0f, -12.0f}
    };
    for (const glm::vec3 p : borders) {
        const Foliage_index_entry entry{.position = p, .radius = 1.0f, .key = 0};
        mirror.set(index.insert(entry), entry);
    }
    for (const glm::vec3 p : borders) {
        expect_same_as_mirror(index, mirror, p - glm::vec3{1.0f, 0.0f, 0.0f}, 1.0f, scratch);
        EXPECT_EQ(scratch.size(), 1u);
        expect_same_as_mirror(index, mirror, p + glm::vec3{0.0f, 0.0f, 1.0f}, 1.0f, scratch);
        EXPECT_EQ(scratch.size(), 1u);
    }
    EXPECT_EQ(index.get_query_path(borders[0], 1.0f), Foliage_query_path::dense_scan);

    for (int i = 0; i < 100; ++i) {
        const Foliage_index_entry entry{.position = {100.0f + static_cast<float>(i), 0.0f, 0.0f}, .radius = 1.0f, .key = 0};
        mirror.set(index.insert(entry), entry);
    }
    for (const glm::vec3 p : borders) {
        EXPECT_EQ(index.get_query_path(p - glm::vec3{1.0f, 0.0f, 0.0f}, 1.0f), Foliage_query_path::cell_walk);
        expect_same_as_mirror(index, mirror, p - glm::vec3{1.0f, 0.0f, 0.0f}, 1.0f, scratch);
        EXPECT_EQ(scratch.size(), 1u);
        expect_same_as_mirror(index, mirror, p + glm::vec3{0.0f, 0.0f, 1.0f}, 1.0f, scratch);
        EXPECT_EQ(scratch.size(), 1u);
    }
}

TEST(Foliage_index, max_radius_after_the_largest_entry_leaves)
{
    Foliage_index index;
    EXPECT_EQ(index.get_max_radius(), 0.0f);
    const Handle a = index.insert({.position = {0.0f, 0.0f, 0.0f}, .radius = 1.0f, .key = 1});
    const Handle b = index.insert({.position = {1.0f, 0.0f, 0.0f}, .radius = 5.0f, .key = 2});
    const Handle c = index.insert({.position = {2.0f, 0.0f, 0.0f}, .radius = 3.0f, .key = 3});
    EXPECT_EQ(index.get_max_radius(), 5.0f);

    index.remove(b);
    EXPECT_EQ(index.get_max_radius(), 3.0f);

    index.set_radius(c, 0.5f);
    EXPECT_EQ(index.get_max_radius(), 1.0f);

    index.set_radius(c, 7.0f);
    EXPECT_EQ(index.get_max_radius(), 7.0f);

    index.move(c, {-50.0f, 3.0f, 9.0f});
    EXPECT_EQ(index.get_max_radius(), 7.0f);

    index.remove(c);
    EXPECT_EQ(index.get_max_radius(), 1.0f);

    index.remove(a);
    EXPECT_EQ(index.get_max_radius(), 0.0f);
    EXPECT_EQ(index.get_count(), 0u);
}

TEST(Foliage_index, handles_reused_after_removal)
{
    Foliage_index       index;
    std::vector<Handle> scratch;
    const Handle a = index.insert({.position = {0.0f, 0.0f, 0.0f}, .radius = 1.0f, .key = 10});
    const Handle b = index.insert({.position = {9.0f, 0.0f, 0.0f}, .radius = 1.0f, .key = 11});
    const Handle c = index.insert({.position = {-9.0f, 0.0f, 0.0f}, .radius = 1.0f, .key = 12});
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);

    index.remove(b);
    index.find_within({9.0f, 0.0f, 0.0f}, 0.5f, scratch);
    EXPECT_TRUE(scratch.empty());

    // The freed slot comes back, holding only the new entry.
    const Handle d = index.insert({.position = {-20.0f, 1.0f, 2.0f}, .radius = 2.0f, .key = 13});
    EXPECT_EQ(d, b);
    EXPECT_EQ(index.get_entry(d).key, 13u);
    EXPECT_EQ(index.get_entry(d).position, glm::vec3(-20.0f, 1.0f, 2.0f));
    EXPECT_EQ(index.get_entry(a).key, 10u);
    EXPECT_EQ(index.get_entry(c).key, 12u);
    EXPECT_EQ(index.get_count(), 3u);
    index.find_within({9.0f, 0.0f, 0.0f}, 0.5f, scratch);
    EXPECT_TRUE(scratch.empty());
    index.find_within({-20.0f, 1.0f, 2.0f}, 0.5f, scratch);
    ASSERT_EQ(scratch.size(), 1u);
    EXPECT_EQ(scratch[0], d);

    // Two frees come back last-freed first; a fresh handle only when none is free.
    index.remove(a);
    index.remove(c);
    EXPECT_EQ(index.insert({.position = glm::vec3{0.0f}, .radius = 1.0f, .key = 14}), c);
    EXPECT_EQ(index.insert({.position = glm::vec3{0.0f}, .radius = 1.0f, .key = 15}), a);
    EXPECT_EQ(index.insert({.position = glm::vec3{0.0f}, .radius = 1.0f, .key = 16}), 3u);

    // clear() empties the index; the cells it knew are reused empty.
    index.clear();
    EXPECT_EQ(index.get_count(), 0u);
    EXPECT_EQ(index.get_max_radius(), 0.0f);
    index.find_within(glm::vec3{0.0f}, 100.0f, scratch);
    EXPECT_TRUE(scratch.empty());
    EXPECT_EQ(index.insert({.position = glm::vec3{0.0f}, .radius = 2.0f, .key = 17}), 0u);
    index.find_within(glm::vec3{0.0f}, 1.0f, scratch);
    ASSERT_EQ(scratch.size(), 1u);
    EXPECT_EQ(index.get_entry(scratch[0]).key, 17u);
}

// Fills an index with entries far from the cases' points, so small queries take the cell walk.
void add_far_fillers(Foliage_index& index, Mirror& mirror, const int count)
{
    for (int i = 0; i < count; ++i) {
        const Foliage_index_entry entry{.position = {-500.0f, 300.0f + static_cast<float>(i), 0.0f}, .radius = 1.0f, .key = 0};
        mirror.set(index.insert(entry), entry);
    }
}

TEST(Foliage_index, both_paths_agree_on_an_entry_a_rounding_step_outside_the_query_box)
{
    const glm::vec3           center{4.0f, 0.0f, 0.0f};
    const Foliage_index_entry edge{.position = {-1.0e-7f, 0.0f, 0.0f}, .radius = 1.0f, .key = 1};
    std::vector<Handle>       scratch;

    Foliage_index dense_index;
    Mirror        dense_mirror;
    dense_mirror.set(dense_index.insert(edge), edge);
    EXPECT_EQ(dense_index.get_query_path(center, 4.0f), Foliage_query_path::dense_scan);
    EXPECT_EQ(expect_same_as_mirror(dense_index, dense_mirror, center, 4.0f, scratch), 1u);

    Foliage_index walk_index;
    Mirror        walk_mirror;
    walk_mirror.set(walk_index.insert(edge), edge);
    add_far_fillers(walk_index, walk_mirror, 200);
    EXPECT_EQ(walk_index.get_query_path(center, 4.0f), Foliage_query_path::cell_walk);
    EXPECT_EQ(expect_same_as_mirror(walk_index, walk_mirror, center, 4.0f, scratch), 1u);
}

TEST(Foliage_index, entries_beyond_the_key_range_found_by_the_cell_walk)
{
    Foliage_index       index;
    Mirror              mirror;
    std::vector<Handle> scratch;
    const std::vector<glm::vec3> positions{
        { 5.0e6f, 0.0f, 0.0f}, {-5.0e6f, 0.0f, 0.0f}, {0.0f, 4.3e6f, 0.0f},
        { 1.0e30f, 0.0f, 0.0f}, {-1.0e30f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0e30f}
    };
    for (const glm::vec3 p : positions) {
        const Foliage_index_entry entry{.position = p, .radius = 1.0f, .key = 2};
        mirror.set(index.insert(entry), entry);
    }
    add_far_fillers(index, mirror, 100);
    for (const glm::vec3 p : positions) {
        EXPECT_EQ(index.get_query_path(p, 1.0f), Foliage_query_path::cell_walk);
        ASSERT_EQ(expect_same_as_mirror(index, mirror, p, 1.0f, scratch), 1u);
        EXPECT_EQ(index.get_entry(scratch[0]).position, p);
    }
    // 5e6 and 1e30 share the clamped cell; the distance test keeps them apart.
    EXPECT_EQ(Foliage_index::get_cell_key(positions[0]), Foliage_index::get_cell_key(positions[3]));
}

TEST(Foliage_index, move_within_the_same_cell)
{
    Foliage_index       index;
    Mirror              mirror;
    std::vector<Handle> scratch;
    const Foliage_index_entry entry{.position = {1.0f, 1.0f, 1.0f}, .radius = 1.0f, .key = 3};
    const Handle handle = index.insert(entry);
    mirror.set(handle, entry);
    add_far_fillers(index, mirror, 100);

    const glm::vec3 moved{3.5f, 0.25f, 2.0f};
    ASSERT_EQ(Foliage_index::get_cell_key(moved), Foliage_index::get_cell_key(entry.position));
    index.move(handle, moved);
    mirror.entries[handle].entry.position = moved;
    EXPECT_EQ(index.get_entry(handle).position, moved);
    EXPECT_EQ(index.get_query_path(moved, 0.5f), Foliage_query_path::cell_walk);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, moved, 0.5f, scratch), 1u);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, entry.position, 0.5f, scratch), 0u);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, moved, 1000.0f, scratch), index.get_count());
}

TEST(Foliage_index, zero_and_negative_query_distance)
{
    Foliage_index       index;
    Mirror              mirror;
    std::vector<Handle> scratch;
    const Foliage_index_entry entry{.position = {2.0f, -3.0f, 4.0f}, .radius = 1.0f, .key = 4};
    const glm::vec3           beside = entry.position + glm::vec3{0.0f, 1.0e-6f, 0.0f};
    mirror.set(index.insert(entry), entry);
    EXPECT_EQ(index.get_query_path(entry.position, 0.0f), Foliage_query_path::dense_scan);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, entry.position, 0.0f, scratch), 1u);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, beside, 0.0f, scratch), 0u);
    add_far_fillers(index, mirror, 20);
    EXPECT_EQ(index.get_query_path(entry.position, 0.0f), Foliage_query_path::cell_walk);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, entry.position, 0.0f, scratch), 1u);
    EXPECT_EQ(expect_same_as_mirror(index, mirror, beside, 0.0f, scratch), 0u);

    // A negative distance finds nothing.
    scratch.push_back(0);
    index.find_within(entry.position, -1.0f, scratch);
    EXPECT_TRUE(scratch.empty());
    EXPECT_EQ(index.count_within(entry.position, -1.0f), 0u);
}

TEST(Foliage_index, larger_radius_inserted_while_the_max_radius_is_stale)
{
    Foliage_index index;
    const Handle a = index.insert({.position = {0.0f, 0.0f, 0.0f}, .radius = 5.0f, .key = 1});
    static_cast<void>(index.insert({.position = {1.0f, 0.0f, 0.0f}, .radius = 3.0f, .key = 2}));
    index.remove(a); // stale, not read
    static_cast<void>(index.insert({.position = {2.0f, 0.0f, 0.0f}, .radius = 4.0f, .key = 3}));
    EXPECT_EQ(index.get_max_radius(), 4.0f);

    const Handle b = index.insert({.position = {3.0f, 0.0f, 0.0f}, .radius = 9.0f, .key = 4});
    index.remove(b); // stale, not read
    static_cast<void>(index.insert({.position = {4.0f, 0.0f, 0.0f}, .radius = 12.0f, .key = 5}));
    EXPECT_EQ(index.get_max_radius(), 12.0f);
}

TEST(Foliage_index, queries_on_an_empty_index)
{
    Foliage_index       index;
    std::vector<Handle> scratch{1, 2, 3};
    EXPECT_EQ(index.count_within(glm::vec3{0.0f}, 10.0f), 0u);
    index.find_within(glm::vec3{0.0f}, 10.0f, scratch);
    EXPECT_TRUE(scratch.empty());
    EXPECT_EQ(index.get_count(), 0u);
}

} // anonymous namespace
