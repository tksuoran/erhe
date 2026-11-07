#include "scene/world_space_validity.hpp"

#include "erhe_item/item_flags.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

namespace {

// Proxies and other derived meshes take no part in the validity.
[[nodiscard]] auto is_validity_input(const erhe::scene::Mesh& mesh) -> bool
{
    const uint64_t flags = mesh.get_flag_bits();
    return ((flags & erhe::Item_flags::content) != 0) && ((flags & erhe::Item_flags::render_proxy) == 0);
}

[[nodiscard]] auto shapes_equal(const erhe::scene::Mesh& mesh, const std::vector<const erhe::primitive::Primitive_render_shape*>& shapes) -> bool
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitives.size() != shapes.size()) {
        return false;
    }
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        const erhe::primitive::Primitive_render_shape* const shape = primitives[i].primitive ? primitives[i].primitive->render_shape.get() : nullptr;
        if (shape != shapes[i]) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

auto World_space_form_status::is_available() const -> bool
{
    return baked && (admitted_count > 0) && (missing_count == 0) && !scene_changed && !settings_changed;
}

auto World_space_validity::Entry::is_valid() const -> bool
{
    return complete && transform_matches && shapes_match;
}

auto World_space_validity::root_relative(const erhe::scene::Mesh& mesh) const -> glm::mat4
{
    // The bake's formula (plan_world_space_bake()): an unmoved node gives the same bits.
    if (m_root_node == nullptr) {
        return mesh.world_from_node();
    }
    return glm::inverse(m_root_node->world_from_node()) * mesh.world_from_node();
}

template <typename F>
void World_space_validity::change_entry(const erhe::scene::Mesh& mesh, F&& change)
{
    if (!is_validity_input(mesh)) {
        return;
    }
    std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_entries.find(&mesh);
    if (i == m_entries.end()) {
        return;
    }
    Entry& entry = i->second;
    const bool was_valid = entry.is_valid();
    change(entry);
    const bool is_valid = entry.is_valid();
    if (was_valid && !is_valid) {
        --m_valid_count;
    } else if (!was_valid && is_valid) {
        ++m_valid_count;
    }
}

void World_space_validity::reset(const erhe::scene::Node* const root_node, std::vector<World_space_validity_entry>&& entries, std::string settings_signature)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_entries.clear();
    m_valid_count = 0;
    for (World_space_validity_entry& entry : entries) {
        if (entry.mesh == nullptr) {
            continue;
        }
        const Entry new_entry{
            .world_from_node = entry.world_from_node,
            .shapes          = std::move(entry.shapes),
            .complete        = entry.complete
        };
        if (new_entry.is_valid()) {
            ++m_valid_count;
        }
        m_entries.insert_or_assign(entry.mesh, new_entry);
    }
    m_root_node          = root_node;
    m_baked              = true;
    m_added_meshes.clear();
    m_removed_count      = 0;
    m_settings_signature = std::move(settings_signature);
    m_settings_changed   = false;
}

void World_space_validity::clear()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_entries.clear();
    m_root_node        = nullptr;
    m_valid_count      = 0;
    m_baked            = false;
    m_added_meshes.clear();
    m_removed_count    = 0;
    m_settings_signature.clear();
    m_settings_changed = false;
}

void World_space_validity::on_mesh_transform_changed(const erhe::scene::Mesh& mesh)
{
    change_entry(mesh, [this, &mesh](Entry& entry) {
        entry.transform_matches = (root_relative(mesh) == entry.world_from_node);
    });
}

void World_space_validity::on_mesh_primitives_changed(const erhe::scene::Mesh& mesh)
{
    change_entry(mesh, [&mesh](Entry& entry) {
        entry.shapes_match = shapes_equal(mesh, entry.shapes);
    });
}

void World_space_validity::on_mesh_registered(const erhe::scene::Mesh& mesh)
{
    if (!is_validity_input(mesh)) {
        return;
    }
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_baked) {
        return;
    }
    const auto i = m_entries.find(&mesh);
    if (i == m_entries.end()) {
        m_added_meshes.insert(&mesh);
        return;
    }
    Entry& entry = i->second;
    if (!entry.registered) {
        entry.registered = true;
        --m_removed_count;
    }
    // The hooks of a mesh outside the scene do not reach here: its placement and primitives are read again as it enters.
    const bool was_valid = entry.is_valid();
    entry.transform_matches = (root_relative(mesh) == entry.world_from_node);
    entry.shapes_match      = shapes_equal(mesh, entry.shapes);
    const bool is_valid = entry.is_valid();
    if (was_valid && !is_valid) {
        --m_valid_count;
    } else if (!was_valid && is_valid) {
        ++m_valid_count;
    }
}

void World_space_validity::on_mesh_unregistered(const erhe::scene::Mesh& mesh)
{
    if (!is_validity_input(mesh)) {
        return;
    }
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_baked) {
        return;
    }
    const auto i = m_entries.find(&mesh);
    if (i == m_entries.end()) {
        m_added_meshes.erase(&mesh);
    } else if (i->second.registered) {
        i->second.registered = false;
        ++m_removed_count;
    }
}

void World_space_validity::on_export_settings_changed(const std::string_view settings_signature)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (m_baked) {
        m_settings_changed = (settings_signature != m_settings_signature);
    }
}

auto World_space_validity::get_status() const -> World_space_form_status
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return World_space_form_status{
        .baked            = m_baked,
        .admitted_count   = m_entries.size(),
        .valid_count      = m_valid_count,
        .missing_count    = m_entries.size() - m_valid_count,
        .scene_changed    = !m_added_meshes.empty() || (m_removed_count > 0),
        .settings_changed = m_settings_changed
    };
}

}
