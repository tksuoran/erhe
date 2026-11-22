#include "level/foliage_system.hpp"
#include "level/kit_piece.hpp"
#include "editor_log.hpp"

#include "erhe_item/item.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/thread_roles.hpp"

#include <cmath>

namespace editor {

namespace {

using Lock = std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)>;

[[nodiscard]] auto same_owner(const std::weak_ptr<erhe::Item_base>& lhs, const std::weak_ptr<erhe::Item_base>& rhs) -> bool
{
    return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
}

[[nodiscard]] auto is_valid(const glm::vec3 position, const float radius) -> bool
{
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) && std::isfinite(radius) && (radius >= 0.0f);
}

} // anonymous namespace

Foliage_system::Foliage_system() = default;

Foliage_system::~Foliage_system() noexcept = default;

auto Foliage_system::capture(erhe::scene::Node& node) -> Captured
{
    return Captured{
        .node     = node.weak_from_this(),
        .node_id  = node.get_id(),
        .position = glm::vec3{node.position_in_world()},
        .radius   = node.get_value(Kit_piece::foliage_radius_property)
    };
}

void Foliage_system::on_node_registered(erhe::scene::Node& node)
{
    if (is_foliage_mesh(node)) {
        add_or_update(node);
    }
}

void Foliage_system::on_node_unregistered(erhe::scene::Node& node)
{
    remove(node);
}

void Foliage_system::on_flags_changed(erhe::scene::Node& node)
{
    if (is_foliage_mesh(node)) {
        add_or_update(node);
    } else {
        remove(node);
    }
}

void Foliage_system::on_values_changed(erhe::scene::Node& node, const erhe::property::Dependency_property& property)
{
    if (&property != &Kit_piece::foliage_radius_property.get()) {
        return;
    }
    // The read may evaluate an expression, which may call this hook again: it completes before the lock is taken.
    const float                          radius = node.get_value(Kit_piece::foliage_radius_property);
    const std::weak_ptr<erhe::Item_base> owner  = node.weak_from_this();
    const Lock lock{m_mutex};
    const Record_map::iterator i = m_records.find(&node);
    if ((i == m_records.end()) || !same_owner(i->second.node, owner)) {
        return;
    }
    i->second.radius = radius;
    queue_values(&node, i->second);
}

void Foliage_system::on_node_active_changed(erhe::scene::Node& node)
{
    static_cast<void>(node);
}

void Foliage_system::on_node_moved(erhe::scene::Xformable& node)
{
    const glm::vec3                      position = glm::vec3{node.position_in_world()};
    const std::weak_ptr<erhe::Item_base> owner    = node.weak_from_this();
    const Lock lock{m_mutex};
    const Record_map::iterator i = m_records.find(&node);
    if ((i == m_records.end()) || !same_owner(i->second.node, owner)) {
        return;
    }
    i->second.position = position;
    queue_values(&node, i->second);
}

void Foliage_system::add_or_update(erhe::scene::Node& node)
{
    const Captured captured = capture(node);
    // Made and released outside m_mutex; destroyed after the lock when not taken.
    erhe::scene::Transform_observer_token observer = node.add_transform_observer(
        [this](erhe::scene::Xformable& moved) {
            on_node_moved(moved);
        }
    );
    erhe::scene::Transform_observer_token released;
    const Lock lock{m_mutex};
    Record_map::iterator i = m_records.find(&node);
    if ((i != m_records.end()) && !same_owner(i->second.node, captured.node)) {
        erase_record(i, released); // a dead node's record at a reused address
        i = m_records.end();
    }
    if (i == m_records.end()) {
        Record record;
        record.node               = captured.node;
        record.node_id            = captured.node_id;
        record.generation         = m_next_generation++;
        record.transform_observer = std::move(observer);
        i = m_records.emplace(&node, std::move(record)).first;
    }
    i->second.position = captured.position;
    i->second.radius   = captured.radius;
    queue_values(&node, i->second);
}

void Foliage_system::remove(erhe::scene::Node& node)
{
    const std::weak_ptr<erhe::Item_base>  owner = node.weak_from_this();
    erhe::scene::Transform_observer_token released;
    const Lock lock{m_mutex};
    const Record_map::iterator i = m_records.find(&node);
    if ((i != m_records.end()) && same_owner(i->second.node, owner)) {
        erase_record(i, released);
    }
}

void Foliage_system::queue_values(const erhe::scene::Node* const key, Record& record)
{
    if (record.queued) {
        return;
    }
    record.queued = true;
    m_pending.push_back(Pending{.change = Change::values, .key = key, .generation = record.generation});
}

void Foliage_system::erase_record(const Record_map::iterator i, erhe::scene::Transform_observer_token& out_released)
{
    if (i->second.state == Record_state::indexed) {
        m_pending.push_back(Pending{.change = Change::remove_entry, .key = i->first, .generation = i->second.generation, .handle = i->second.handle});
    }
    out_released = std::move(i->second.transform_observer);
    m_records.erase(i);
}

auto Foliage_system::apply_pending(std::uint64_t& out_first_invalid_id) -> std::size_t
{
    std::size_t invalid_count = 0;
    if (m_pending.empty()) {
        return invalid_count;
    }
    std::swap(m_pending, m_processing);
    for (const Pending& pending : m_processing) {
        if (pending.change == Change::remove_entry) {
            m_index.remove(pending.handle);
            m_handle_nodes[pending.handle].reset();
            continue;
        }
        const Record_map::iterator i = m_records.find(pending.key);
        if ((i == m_records.end()) || (i->second.generation != pending.generation)) {
            continue; // the record left; a later record at the key has an entry of its own
        }
        Record& record = i->second;
        record.queued = false;
        if (!is_valid(record.position, record.radius)) {
            if (record.state == Record_state::indexed) {
                m_index.remove(record.handle);
                m_handle_nodes[record.handle].reset();
            }
            if (record.state != Record_state::not_indexable) {
                if (invalid_count == 0) {
                    out_first_invalid_id = record.node_id;
                }
                ++invalid_count;
            }
            record.state = Record_state::not_indexable;
            continue;
        }
        if (record.state == Record_state::indexed) {
            m_index.move      (record.handle, record.position);
            m_index.set_radius(record.handle, record.radius);
            continue;
        }
        record.handle = m_index.insert(Foliage_index_entry{.position = record.position, .radius = record.radius, .key = record.node_id});
        record.state  = Record_state::indexed;
        if (m_handle_nodes.size() <= record.handle) {
            m_handle_nodes.resize(static_cast<std::size_t>(record.handle) + 1);
        }
        m_handle_nodes[record.handle] = record.node;
    }
    m_processing.clear();
    return invalid_count;
}

void Foliage_system::log_invalid(const std::size_t count, const std::uint64_t first_id)
{
    if (count > 0) {
        log_scene->warn(
            "{} foliage item(s) left the foliage index: a world position that is not finite or a radius that is not a finite number >= 0 (the first: node id {})",
            count, first_id
        );
    }
}

auto Foliage_system::get_index() -> const Foliage_index&
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread();

    std::uint64_t first_invalid_id = 0;
    std::size_t   invalid_count    = 0;
    {
        const Lock lock{m_mutex};
        invalid_count = apply_pending(first_invalid_id);
    }
    log_invalid(invalid_count, first_invalid_id);
    return m_index;
}

void Foliage_system::find_nodes_within(const glm::vec3 center, const float distance, std::vector<std::shared_ptr<erhe::scene::Node>>& out_nodes)
{
    out_nodes.clear();
    const Foliage_index& index = get_index();
    index.find_within(center, distance, m_handle_scratch);
    for (const Foliage_index::Handle handle : m_handle_scratch) {
        std::shared_ptr<erhe::scene::Node> node = get_node(handle);
        if (node) {
            out_nodes.push_back(std::move(node));
        }
    }
}

void Foliage_system::get_nodes(std::vector<std::shared_ptr<erhe::scene::Node>>& out_nodes)
{
    out_nodes.clear();
    static_cast<void>(get_index());
    for (const std::weak_ptr<erhe::Item_base>& weak : m_handle_nodes) {
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(weak.lock());
        if (node) {
            out_nodes.push_back(std::move(node));
        }
    }
}

auto Foliage_system::get_node(const Foliage_index::Handle handle) const -> std::shared_ptr<erhe::scene::Node>
{
    erhe::utility::Thread_roles::assert_main_thread();
    if (handle >= m_handle_nodes.size()) {
        return {};
    }
    return std::dynamic_pointer_cast<erhe::scene::Node>(m_handle_nodes[handle].lock());
}

auto Foliage_system::find_handle(const erhe::scene::Node& node) const -> std::optional<Foliage_index::Handle>
{
    const Lock lock{m_mutex};
    const Record_map::const_iterator i = m_records.find(&node);
    if ((i == m_records.end()) || (i->second.state != Record_state::indexed)) {
        return {};
    }
    return i->second.handle;
}

auto Foliage_system::get_queue_state() const -> Foliage_queue_state
{
    const Lock lock{m_mutex};
    return Foliage_queue_state{
        .pending_count       = m_pending.size(),
        .pending_capacity    = m_pending.capacity(),
        .processing_capacity = m_processing.capacity()
    };
}

} // namespace editor
