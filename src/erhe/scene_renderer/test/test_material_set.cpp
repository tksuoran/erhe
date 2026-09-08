// Material_set membership and slot logic (doc/draw_list_material_set_plan.md
// V1). No window, no device, no scene and no draw list: the slot table is
// bookkeeping over shared_ptr<Material>, and the reported bug it exists to
// prevent - one material resolving through different slots depending on what
// rendered last - is a property of that bookkeeping alone.

#include <gtest/gtest.h>

#include "erhe_scene_renderer/material_set.hpp"

#include "erhe_primitive/material.hpp"

#include <memory>
#include <vector>

namespace {

using erhe::primitive::Material;
using erhe::primitive::Material_create_info;
using erhe::scene_renderer::Material_set;
using erhe::scene_renderer::Material_slot;
using erhe::scene_renderer::Material_slot_id;

[[nodiscard]] auto make_material(const char* name) -> std::shared_ptr<Material>
{
    return std::make_shared<Material>(Material_create_info{.name = name});
}

using Material_list = std::vector<std::shared_ptr<Material>>;

void sync_library(Material_set& set, const Material_list& materials)
{
    set.sync_library(std::span<const std::shared_ptr<Material>>{materials});
}

} // anonymous namespace

// Every primitive with no material of its own names
// default_material_slot_index, so that slot must exist from construction and
// must never come to hold a material.
TEST(Material_set, default_slot_is_reserved)
{
    Material_set set;
    EXPECT_EQ(set.get_slot_count(), 1u) << "the reserved default slot must be covered by the GPU write";
    EXPECT_EQ(set.get_live_count(), 0u) << "the reserved slot holds no material";
    const std::span<const Material_slot> slots = set.get_materials();
    ASSERT_GE(slots.size(), 1u);
    EXPECT_TRUE(slots[Material_set::default_material_slot_index].alive);
    EXPECT_EQ(slots[Material_set::default_material_slot_index].material, nullptr);
}

TEST(Material_set, default_slot_is_never_assigned)
{
    Material_set set;
    const Material_list materials{make_material("a"), make_material("b")};
    sync_library(set, materials);
    const std::shared_ptr<Material> referenced = make_material("referenced");
    static_cast<void>(set.add_ref(referenced));

    // Freeing every material must not make the reserved slot reusable either.
    sync_library(set, Material_list{});
    set.release(referenced.get());
    const std::shared_ptr<Material> after = make_material("after");
    EXPECT_NE(set.add_ref(after).index, Material_set::default_material_slot_index);
}

TEST(Material_set, library_sync_assigns_slots_in_order)
{
    Material_set set;
    const Material_list materials{make_material("a"), make_material("b"), make_material("c")};
    sync_library(set, materials);

    // Slot 0 is the reserved default-material slot, so library materials
    // start at slot 1.
    EXPECT_EQ(set.get_slot(materials[0].get()), 1u);
    EXPECT_EQ(set.get_slot(materials[1].get()), 2u);
    EXPECT_EQ(set.get_slot(materials[2].get()), 3u);
    EXPECT_EQ(set.get_slot_count(), 4u);
}

TEST(Material_set, slot_is_stable_across_sync)
{
    Material_set set;
    const Material_list materials{make_material("a"), make_material("b")};
    sync_library(set, materials);
    const std::optional<uint32_t> before = set.get_slot(materials[1].get());
    sync_library(set, materials);
    EXPECT_EQ(set.get_slot(materials[1].get()), before);
}

// The property that makes cached records safe: a record naming slot 3 must
// still mean the same material after unrelated membership changes.
TEST(Material_set, unrelated_removal_does_not_shift)
{
    Material_set set;
    const Material_list materials{make_material("a"), make_material("b"), make_material("c")};
    sync_library(set, materials);
    ASSERT_EQ(set.get_slot(materials[2].get()), 3u);

    sync_library(set, Material_list{materials[0], materials[2]});
    EXPECT_EQ(set.get_slot(materials[2].get()), 3u);
    EXPECT_EQ(set.get_slot(materials[0].get()), 1u);
    EXPECT_FALSE(set.get_slot(materials[1].get()).has_value());
}

TEST(Material_set, unrelated_addition_does_not_shift)
{
    Material_set set;
    const Material_list materials{make_material("a"), make_material("b")};
    sync_library(set, materials);

    const std::shared_ptr<Material> added = make_material("c");
    sync_library(set, Material_list{materials[0], added, materials[1]});
    EXPECT_EQ(set.get_slot(materials[0].get()), 1u);
    EXPECT_EQ(set.get_slot(materials[1].get()), 2u);
    EXPECT_EQ(set.get_slot(added.get()),        3u);
}

// The unit-level statement of the reported bug: what one set does to a
// material's slot must be invisible to another set holding the same material.
TEST(Material_set, two_sets_are_independent)
{
    const std::shared_ptr<Material> shared = make_material("shared");
    const std::shared_ptr<Material> other  = make_material("other");

    Material_set a;
    Material_set b;
    sync_library(a, Material_list{other, shared});  // shared -> slot 2
    sync_library(b, Material_list{shared});         // shared -> slot 1

    EXPECT_EQ(a.get_slot(shared.get()), 2u);
    EXPECT_EQ(b.get_slot(shared.get()), 1u);

    sync_library(b, Material_list{shared, other});
    EXPECT_EQ(a.get_slot(shared.get()), 2u) << "one set's sync moved another set's slot";
    EXPECT_EQ(a.get_slot(other.get()),  1u);
}

// A lookup must never assign: record writers are handed a const Material_set
// and everything they can call has to leave membership alone.
TEST(Material_set, get_slot_never_assigns)
{
    Material_set set;
    const Material_list materials{make_material("a")};
    sync_library(set, materials);

    const std::shared_ptr<Material> stranger = make_material("stranger");
    const Material_set&             const_set = set;
    EXPECT_FALSE(const_set.get_slot(stranger.get()).has_value());
    EXPECT_FALSE(const_set.find(stranger.get()).is_valid());
    EXPECT_EQ(const_set.get_slot_count(), 2u); // the reserved slot and the library material
    EXPECT_EQ(const_set.get_live_count(), 1u);
}

// Assigning a material to a registered mesh: the material need not be in any
// content library for the mesh to render with it.
TEST(Material_set, add_ref_assigns_slot_for_material_not_in_library)
{
    Material_set set;
    const std::shared_ptr<Material> library_material = make_material("library");
    sync_library(set, Material_list{library_material});

    const std::shared_ptr<Material> assigned = make_material("assigned");
    const Material_slot_id id = set.add_ref(assigned);
    ASSERT_TRUE(id.is_valid());
    EXPECT_EQ(set.get_slot(assigned.get()), id.index);
    EXPECT_NE(id.index, set.get_slot(library_material.get()).value());
}

TEST(Material_set, referenced_material_survives_library_sync)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    const Material_slot_id id = set.add_ref(material);

    sync_library(set, Material_list{make_material("unrelated")});
    EXPECT_EQ(set.get_slot(material.get()), id.index) << "an object reference did not hold the slot";
}

TEST(Material_set, slot_freed_only_when_both_sources_are_gone)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    sync_library(set, Material_list{material});
    static_cast<void>(set.add_ref(material));

    sync_library(set, Material_list{});
    EXPECT_TRUE(set.get_slot(material.get()).has_value()) << "object reference should still hold it";

    set.release(material.get());
    EXPECT_FALSE(set.get_slot(material.get()).has_value());
}

TEST(Material_set, freed_slot_is_reused)
{
    Material_set set;
    const std::shared_ptr<Material> first = make_material("first");
    const Material_slot_id first_id = set.add_ref(first);
    ASSERT_EQ(first_id.index, 1u);

    set.release(first.get());
    EXPECT_FALSE(set.is_valid(first_id)) << "a handle to a freed slot must not validate";

    const std::shared_ptr<Material> second = make_material("second");
    const Material_slot_id second_id = set.add_ref(second);
    EXPECT_EQ(second_id.index, first_id.index) << "the freed slot should be reused";
    EXPECT_NE(second_id.generation, first_id.generation) << "the generation must move";
    EXPECT_TRUE(set.is_valid(second_id));
    EXPECT_FALSE(set.is_valid(first_id));
}

TEST(Material_set, add_ref_is_refcounted)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    static_cast<void>(set.add_ref(material));
    static_cast<void>(set.add_ref(material));

    set.release(material.get());
    EXPECT_TRUE(set.get_slot(material.get()).has_value()) << "one reference remained";
    set.release(material.get());
    EXPECT_FALSE(set.get_slot(material.get()).has_value());
}

TEST(Material_set, add_ref_returns_existing_id)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    sync_library(set, Material_list{material});
    const Material_slot_id id = set.add_ref(material);
    EXPECT_EQ(id.index, set.get_slot(material.get()).value());
}

TEST(Material_set, removed_material_has_no_slot)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    sync_library(set, Material_list{material});
    sync_library(set, Material_list{});
    EXPECT_FALSE(set.get_slot(material.get()).has_value());
    EXPECT_FALSE(set.find(material.get()).is_valid());
}

TEST(Material_set, slot_count_covers_holes)
{
    Material_set set;
    const Material_list materials{make_material("a"), make_material("b"), make_material("c")};
    sync_library(set, materials);
    sync_library(set, Material_list{materials[0], materials[2]});

    EXPECT_EQ(set.get_slot_count(), 4u) << "the GPU write has to cover the hole at slot 2";
    EXPECT_EQ(set.get_live_count(), 2u);
    const std::span<const Material_slot> slots = set.get_materials();
    ASSERT_GE(slots.size(), 4u);
    EXPECT_TRUE (slots[0].alive); // the reserved default slot
    EXPECT_TRUE (slots[1].alive);
    EXPECT_FALSE(slots[2].alive);
    EXPECT_TRUE (slots[3].alive);
}

TEST(Material_set, set_releases_material_reference_when_slot_freed)
{
    Material_set set;
    std::weak_ptr<Material> weak;
    {
        const std::shared_ptr<Material> material = make_material("m");
        weak = material;
        sync_library(set, Material_list{material});
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_FALSE(weak.expired()) << "the set should hold the material alive while registered";
    sync_library(set, Material_list{});
    EXPECT_TRUE(weak.expired()) << "the set kept a reference to a material it no longer holds a slot for";
}

TEST(Material_set, membership_dirty_is_an_exact_edge)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    sync_library(set, Material_list{material});
    EXPECT_TRUE(set.is_membership_dirty());

    set.clear_membership_dirty();
    sync_library(set, Material_list{material});
    EXPECT_FALSE(set.is_membership_dirty()) << "a sync that changed nothing dirtied the set";

    static_cast<void>(set.add_ref(material));
    EXPECT_FALSE(set.is_membership_dirty()) << "referencing an already-registered material dirtied the set";

    static_cast<void>(set.add_ref(material));
    set.release(material.get());
    EXPECT_FALSE(set.is_membership_dirty()) << "a release that left the count above zero dirtied the set";

    const std::shared_ptr<Material> added = make_material("added");
    static_cast<void>(set.add_ref(added));
    EXPECT_TRUE(set.is_membership_dirty()) << "assigning a slot must dirty the set";

    set.clear_membership_dirty();
    set.release(added.get());
    EXPECT_TRUE(set.is_membership_dirty()) << "freeing a slot must dirty the set";
}

// The leak guard for object-sourced membership: the material preview assigns a
// different material to the same mesh several times per frame, so the pattern
// below runs thousands of times per session.
TEST(Material_set, object_churn_does_not_grow_the_slot_table)
{
    Material_set set;
    std::shared_ptr<Material> previous;
    std::vector<std::weak_ptr<Material>> released;
    for (int i = 0; i < 100; ++i) {
        const std::shared_ptr<Material> material = make_material("churn");
        static_cast<void>(set.add_ref(material));
        if (previous) {
            set.release(previous.get());
            released.emplace_back(previous);
        }
        previous = material;
    }
    // Two, not one: the new material is referenced BEFORE the old one is
    // released - the ordering that keeps a material shared between the two
    // lists from ever reaching zero - so both hold a slot for an instant. What
    // matters is that the table does not grow with the number of rounds.
    EXPECT_LE(set.get_slot_count(), 3u) << "slots accumulated across preview renders";
    EXPECT_EQ(set.get_live_count(), 1u);

    previous.reset();
    for (const std::weak_ptr<Material>& weak : released) {
        EXPECT_TRUE(weak.expired()) << "the set held a released material alive";
    }
}

TEST(Material_set, enqueued_reference_is_not_visible_until_flush)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    const Material_list materials{material};
    set.enqueue_object_materials(1, std::span<const std::shared_ptr<Material>>{materials});

    EXPECT_FALSE(set.get_slot(material.get()).has_value()) << "an enqueued reference applied off the main thread";
    EXPECT_EQ(set.get_pending_count(), 1u);

    set.flush_pending();
    EXPECT_TRUE(set.get_slot(material.get()).has_value());
    EXPECT_EQ(set.get_pending_count(), 0u);
}

TEST(Material_set, flush_applies_enqueued_references_in_order)
{
    Material_set set;
    const std::shared_ptr<Material> material = make_material("m");
    const Material_list materials{material};

    // Register then unregister: applied in this order the material ends up
    // with no slot. Applied in the other order it would keep one forever.
    set.enqueue_object_materials(7, std::span<const std::shared_ptr<Material>>{materials});
    set.enqueue_release_object(7);
    set.flush_pending();
    EXPECT_FALSE(set.get_slot(material.get()).has_value());

    // And the other way round: unregistering an object that was never
    // registered must not disturb the registration that follows it.
    set.enqueue_release_object(8);
    set.enqueue_object_materials(8, std::span<const std::shared_ptr<Material>>{materials});
    set.flush_pending();
    EXPECT_TRUE(set.get_slot(material.get()).has_value());
}

// The property sync_object_materials rests on, stated in set terms so this
// needs no draw list: reassigning an object's material list must not drop a
// material both lists contain, or a record naming its slot would be stale for
// as long as the gap lasts.
TEST(Material_set, shared_material_survives_a_list_diff)
{
    Material_set set;
    const std::shared_ptr<Material> shared  = make_material("shared");
    const std::shared_ptr<Material> dropped = make_material("dropped");
    const std::shared_ptr<Material> added   = make_material("added");

    const Material_list before{shared, dropped};
    set.sync_object_materials(1, std::span<const std::shared_ptr<Material>>{before});
    const std::optional<uint32_t> shared_slot = set.get_slot(shared.get());
    ASSERT_TRUE(shared_slot.has_value());

    const Material_list after{shared, added};
    set.sync_object_materials(1, std::span<const std::shared_ptr<Material>>{after});

    EXPECT_EQ(set.get_slot(shared.get()), shared_slot) << "a material in both lists lost its slot";
    EXPECT_FALSE(set.get_slot(dropped.get()).has_value());
    EXPECT_TRUE (set.get_slot(added.get()).has_value());

    set.release_object_materials(1);
    EXPECT_FALSE(set.get_slot(shared.get()).has_value());
    EXPECT_FALSE(set.get_slot(added.get()).has_value());
}
