#pragma once

#include "level/foliage_index.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/node_system.hpp"
#include "erhe_scene/transform_observer.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace erhe { class Item_base; }

namespace editor {

// The queue of a Foliage_system, for tests.
class Foliage_queue_state
{
public:
    std::size_t pending_count      {0};
    std::size_t pending_capacity   {0};
    std::size_t processing_capacity{0};
};

// The per-scene node system that keeps the scene's Foliage_index over its foliage meshes
// (doc/editor/level_tools.md "Foliage paint"). The hooks capture values; the queries apply them and read no node.
class Foliage_system : public erhe::scene::INode_system
{
public:
    Foliage_system();
    ~Foliage_system() noexcept override;
    Foliage_system(const Foliage_system&) = delete;
    auto operator=(const Foliage_system&) -> Foliage_system& = delete;

    // Implements INode_system
    void on_node_registered    (erhe::scene::Node& node) override;
    void on_node_unregistered  (erhe::scene::Node& node) override;
    void on_values_changed     (erhe::scene::Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(erhe::scene::Node& node) override;

    // Scene_root::on_mesh_flags_changed() forwards a change of the foliage flag here.
    void on_flags_changed(erhe::scene::Node& node);

    // Main thread; each applies the captured changes first.
    [[nodiscard]] auto get_index() -> const Foliage_index&;
    // The nodes of the index entries within the distance; clears the buffer first.
    void find_nodes_within(glm::vec3 center, float distance, std::vector<std::shared_ptr<erhe::scene::Node>>& out_nodes);
    // Every indexed node; clears the buffer first.
    void get_nodes(std::vector<std::shared_ptr<erhe::scene::Node>>& out_nodes);
    // The node of an entry of get_index(); null when it is gone.
    [[nodiscard]] auto get_node(Foliage_index::Handle handle) const -> std::shared_ptr<erhe::scene::Node>;

    // Any thread: the entry of a node as of the last application; none when the index does not hold it.
    [[nodiscard]] auto find_handle(const erhe::scene::Node& node) const -> std::optional<Foliage_index::Handle>;
    [[nodiscard]] auto get_queue_state() const -> Foliage_queue_state;

private:
    enum class Change : unsigned int {
        values = 0,  // the record's captured position and radius go to the index
        remove_entry // the record left; its index entry goes
    };

    class Pending
    {
    public:
        Change                   change    {Change::values};
        const erhe::scene::Node* key       {nullptr}; // never dereferenced; the record's map key
        std::uint64_t            generation{0};       // of the record it was queued for; a later record at the key ignores it
        Foliage_index::Handle    handle    {0};       // remove_entry only
    };

    enum class Record_state : unsigned int {
        not_applied = 0,
        indexed,
        not_indexable // a position or radius the index cannot take; indexed again when it becomes valid
    };

    class Record
    {
    public:
        std::weak_ptr<erhe::Item_base>        node{};
        std::uint64_t                         node_id{0};
        std::uint64_t                         generation{0};
        Record_state                          state{Record_state::not_applied};
        Foliage_index::Handle                 handle{0};
        glm::vec3                             position{0.0f}; // captured at the latest hook
        float                                 radius{0.0f};
        erhe::scene::Transform_observer_token transform_observer{};
        bool                                  queued{false};
    };

    using Record_map = std::unordered_map<const erhe::scene::Node*, Record>;

    // A node's values, read outside m_mutex.
    class Captured
    {
    public:
        std::weak_ptr<erhe::Item_base> node{};
        std::uint64_t                  node_id{0};
        glm::vec3                      position{0.0f};
        float                          radius{0.0f};
    };
    [[nodiscard]] static auto capture(erhe::scene::Node& node) -> Captured;

    void add_or_update(erhe::scene::Node& node);
    void remove       (erhe::scene::Node& node);
    void on_node_moved(erhe::scene::Xformable& node);

    // Under m_mutex.
    void queue_values (const erhe::scene::Node* key, Record& record);
    void erase_record (Record_map::iterator i, erhe::scene::Transform_observer_token& out_released);
    // Under m_mutex: applies the queue; returns the count of records that left the index for invalid values.
    auto apply_pending(std::uint64_t& out_first_invalid_id) -> std::size_t;
    static void log_invalid(std::size_t count, std::uint64_t first_id);

    mutable ERHE_PROFILE_MUTEX(std::mutex,      m_mutex);
    std::vector<Pending>                        m_pending;
    std::vector<Pending>                        m_processing;
    Record_map                                  m_records;
    std::uint64_t                               m_next_generation{1};
    std::vector<std::weak_ptr<erhe::Item_base>> m_handle_nodes;   // indexed by handle; written by the queries (main thread)
    Foliage_index                               m_index;          // written by the queries only
    std::vector<Foliage_index::Handle>          m_handle_scratch;
};

} // namespace editor
