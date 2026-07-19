#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::physics {

class Collision_filter;

// A Collision_filter compiled into bitsets. Mirrors the Jolt backend's
// Compiled_collision_filter so both backends implement identical semantics.
class Compiled_collision_filter
{
public:
    uint64_t membership   {0};     // systems this filter's body belongs to
    uint64_t mask         {0};     // collide_with (allow list) or not_collide_with (deny list) systems
    bool     is_allow_list{false};
};

// Interns free-form collision system names into 64-bit bitsets and answers the
// bidirectional KHR_physics_rigid_bodies collision-system test.
//
// Why this is not mapped onto b3Filter{categoryBits, maskBits}: Box3D's built-in
// filter collides a pair iff (A.mask & B.category) != 0 && (A.category & B.mask) != 0,
// i.e. "the pair must share a bit". erhe's deny-list semantics are the opposite --
// "the pair must NOT share a bit" -- and that is not expressible as a shared-bit
// test once a body belongs to more than one system. Proof: pick deny set D and a
// body whose membership is {b1, b2} with b1 in D and b2 not in D; the deny rule
// says do not collide, while any mask M with the single-bit cases correct
// (M = ~D) says collide. So the deny direction needs a real predicate, and the
// backend routes filtering through b3World_SetCustomFilterCallback (which, unlike
// the friction/restitution callbacks, does take a context pointer) instead.
class Box3d_collision_filter_table
{
public:
    static constexpr int no_collision_filter = -1;

    // Compiles the filter if it has not been seen before, returning its index.
    // Compilation is cached per filter item: editing a live filter requires
    // re-assigning it to the bodies that use it, or calling recompile().
    //
    // The cache is keyed by raw item pointer but validated against a weak_ptr,
    // because a freed filter's address can be reused by the next allocation and
    // would otherwise silently inherit the dead filter's compiled bitsets. A
    // strong reference is deliberately not held: Collision_filter is an
    // erhe::Item and may be hosted by a scene, so owning one here would keep a
    // closed scene's content alive.
    [[nodiscard]] auto get_or_compile(const std::shared_ptr<Collision_filter>& filter) -> int;
    [[nodiscard]] auto recompile     (const std::shared_ptr<Collision_filter>& filter) -> int;

    // Bidirectional test. Either index may be no_collision_filter, meaning the
    // body carries no filter and so collides with everything.
    [[nodiscard]] auto should_collide(int filter_index_a, int filter_index_b) const -> bool;

    [[nodiscard]] auto get_compiled_filter(int filter_index) const -> const Compiled_collision_filter&;
    [[nodiscard]] auto get_system_count   () const -> std::size_t { return m_system_names.size(); }

    // Interns a system name, returning its bit index, or -1 when the 64-system
    // budget is exhausted (an error is logged; the name is then ignored rather
    // than aliased onto another system's bit).
    [[nodiscard]] auto intern_system(const std::string& name) -> int;
    [[nodiscard]] auto make_bitset   (const std::vector<std::string>& names) -> uint64_t;

private:
    [[nodiscard]] auto compile(const Collision_filter& filter) -> Compiled_collision_filter;

    // One direction of the test: does a filter with the given compiled data
    // accept a body whose membership bitset is other_membership?
    [[nodiscard]] static auto accepts(const Compiled_collision_filter& filter, uint64_t other_membership) -> bool;

    class Filter_cache_entry
    {
    public:
        int                            index{no_collision_filter};
        std::weak_ptr<Collision_filter> filter{};
    };

    std::vector<std::string>                                        m_system_names;
    std::vector<Compiled_collision_filter>                          m_compiled_filters;
    std::unordered_map<const Collision_filter*, Filter_cache_entry> m_filter_to_compiled;
};

} // namespace erhe::physics
