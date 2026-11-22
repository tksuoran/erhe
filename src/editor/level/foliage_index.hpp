#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// The point grid of the foliage items of one scene (doc/plans/foliage_paint_tool_design.md FD4).
namespace editor {

class Foliage_index_entry
{
public:
    glm::vec3     position{0.0f}; // scene root space
    float         radius{0.0f};
    std::uint64_t key{0};         // the caller's, here the node's id
};

enum class Foliage_query_path : unsigned int {
    cell_walk = 0,
    dense_scan
};

class Foliage_index
{
public:
    using Handle = std::uint32_t;

    static constexpr float         cell_size           = 4.0f;
    static constexpr int           cell_coordinate_min = -(1 << 20);
    static constexpr int           cell_coordinate_max = (1 << 20) - 1;
    static constexpr std::uint32_t no_slot             = 0xffffffffu;

    [[nodiscard]] auto insert        (const Foliage_index_entry& entry) -> Handle;
    void               remove        (Handle handle);
    void               move          (Handle handle, glm::vec3 position);
    void               set_radius    (Handle handle, float radius);
    void               clear         ();
    [[nodiscard]] auto get_entry     (Handle handle) const -> const Foliage_index_entry&;
    [[nodiscard]] auto get_count     () const -> std::size_t;
    [[nodiscard]] auto get_max_radius() const -> float;
    void               find_within   (glm::vec3 center, float distance, std::vector<Handle>& out_handles) const;
    [[nodiscard]] auto count_within  (glm::vec3 center, float distance) const -> std::size_t;

    // The path find_within() and count_within() take for this query (FD4.3).
    [[nodiscard]] auto get_query_path(glm::vec3 center, float distance) const -> Foliage_query_path;

    // floor(value / cell_size), clamped to the 21-bit range of the packed key.
    [[nodiscard]] static auto get_cell_coordinate(float value) -> int;
    [[nodiscard]] static auto get_cell_key       (glm::vec3 position) -> std::uint64_t;

private:
    class Slot
    {
    public:
        Foliage_index_entry entry;
        std::uint64_t       cell_key{0};
        std::uint32_t       previous{no_slot}; // in its cell
        std::uint32_t       next    {no_slot}; // in its cell; in a free slot, the next free slot
        bool                in_use  {false};
    };

    void link  (std::uint32_t slot_index);
    void unlink(std::uint32_t slot_index);
    [[nodiscard]] auto get_slot(Handle handle) -> Slot&;
    [[nodiscard]] auto get_slot(Handle handle) const -> const Slot&;

    template <typename Visit>
    void visit_within(glm::vec3 center, float distance, Visit&& visit) const;

    std::vector<Slot>                                m_slots;
    std::unordered_map<std::uint64_t, std::uint32_t> m_cell_heads; // cells stay once used; an empty cell holds no_slot
    std::uint32_t                                    m_free_head{no_slot};
    std::size_t                                      m_count{0};
    mutable float                                    m_max_radius{0.0f};       // rescanned lazily by get_max_radius()
    mutable bool                                     m_max_radius_stale{false};
};

} // namespace editor
