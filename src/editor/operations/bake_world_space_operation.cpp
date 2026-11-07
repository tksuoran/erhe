#include "operations/bake_world_space_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "prefabs/prefab_library.hpp"
#include "scene/scene_root.hpp"
#include "scene/world_space_bake.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_item/item_flags.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_task/task.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace editor {

namespace {

constexpr const char* c_world_space_node_name = "World Space";

// What the bake sets on a baked mesh and on the World Space node.
constexpr uint64_t c_baked_flag_bits = erhe::Item_flags::lock_channel_mask;

// Puts the c_baked_flag_bits of `item` back to `before_bits`.
void restore_flag_bits(erhe::Item_base& item, const uint64_t before_bits)
{
    item.disable_flag_bits(c_baked_flag_bits & ~before_bits);
    item.enable_flag_bits(before_bits);
}

[[nodiscard]] auto find_world_space_node(const erhe::scene::Node& root) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : root.get_children()) {
        const std::shared_ptr<erhe::scene::Xform> xform = std::dynamic_pointer_cast<erhe::scene::Xform>(child);
        if (xform && (xform->get_name() == c_world_space_node_name) && (xform->parent_from_node() == glm::mat4{1.0f})) {
            return xform;
        }
    }
    return {};
}

void set_mesh_primitives(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh, const std::vector<erhe::scene::Mesh_primitive>& primitives)
{
    erhe::raytrace::IScene* const rt_scene = mesh->get_rt_scene();
    mesh->detach_rt_from_scene();
    mesh->set_primitives(primitives);
    if (rt_scene != nullptr) {
        mesh->attach_rt_to_scene(rt_scene);
    }
    context.app_message_bus->mesh_geometry_changed.send_message(Mesh_geometry_changed_message{.mesh = mesh});
}

// The placeholder and the mesh share their transform: the child keeps its
// local transform bit for bit (set_parent() would recompute it from world).
void move_child_keeping_local(const std::shared_ptr<erhe::Hierarchy>& child, const std::shared_ptr<erhe::Hierarchy>& new_parent)
{
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
    if (!node) {
        child->set_parent(new_parent);
        return;
    }
    const erhe::scene::Trs_transform parent_from_node = node->parent_from_node_transform();
    node->set_parent(new_parent);
    node->set_parent_from_node(parent_from_node);
}

} // anonymous namespace

Bake_world_space_operation::Bake_world_space_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    ERHE_PROFILE_FUNCTION();

    Scene_root* const scene_root = m_parameters.scene_root.get();
    if (scene_root == nullptr) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> root = scene_root->get_scene().get_root_node();
    if (!root) {
        return;
    }

    std::unordered_set<const erhe::scene::Node*> external_asset_nodes;
    for (const auto& [node, external_asset] : collect_prefab_external_assets(*root, std::filesystem::path{})) {
        static_cast<void>(external_asset);
        external_asset_nodes.insert(node);
    }
    const World_space_bake_plan plan = plan_world_space_bake(*scene_root, m_parameters.settings, external_asset_nodes);
    m_kept_reasons = plan.kept_reasons;

    m_world_space = find_world_space_node(*root);
    if (!m_world_space) {
        m_world_space = std::make_shared<erhe::scene::Xform>(c_world_space_node_name);
        m_world_space->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        m_world_space_created = true;
    }
    m_world_space_before_flag_bits = m_world_space->get_flag_bits() & c_baked_flag_bits;

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root->layers().content()->meshes) {
        const auto planned = plan.bake.baked_meshes.find(mesh.get());
        if (planned == plan.bake.baked_meshes.end()) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::world_space) != 0) {
            continue; // already in world space
        }
        Entry entry{};
        entry.mesh                    = mesh;
        entry.world_from_node         = planned->second;
        entry.before_name             = mesh->get_name();
        entry.before_parent_from_node = mesh->parent_from_node_transform();
        entry.primitives_before       = mesh->get_primitives();
        entry.before_flag_bits        = mesh->get_flag_bits() & c_baked_flag_bits;
        for (std::shared_ptr<erhe::scene::Node> ancestor = mesh->get_parent_node(); ancestor; ancestor = ancestor->get_parent_node()) {
            ++entry.depth;
        }
        m_entries.push_back(std::move(entry));
    }
    std::stable_sort(
        m_entries.begin(), m_entries.end(),
        [](const Entry& lhs, const Entry& rhs) { return lhs.depth < rhs.depth; }
    );
    assign_placeholders();

    set_description(fmt::format("Bake to World Space: {} meshes, {} placeholders", m_entries.size(), get_placeholder_count()));
}

// Main thread, the first execute(), before the scene lock: the world-space
// copies of every planned mesh's primitives. An entry whose copy cannot be
// made is dropped (logged).
void Bake_world_space_operation::build_primitives()
{
    ERHE_PROFILE_FUNCTION();

    m_built = true;
    // The optimized variants are rebuilt in the background after execute()
    // where worker contexts exist (as the paint operations do).
    erhe::primitive::Build_info build_info = m_parameters.build_info;
    const App_context& context = m_parameters.context;
    if (
        build_info.buffer_info.optimize_meshes &&
        (context.graphics_device != nullptr) &&
        context.graphics_device->supports_worker_contexts()
    ) {
        build_info.buffer_info.optimize_meshes = false;
    }
    // One task per entry: every world-space primitive is a new shape, and
    // distinct shapes build concurrently (doc/editor/async_asset_loading.md).
    std::vector<const char*> failures(m_entries.size(), nullptr);
    erhe::graphics::Device* const graphics_device = context.graphics_device;
    const auto build_entry = [this, &build_info, &failures, graphics_device](const std::size_t entry_index) {
        Entry& entry = m_entries[entry_index];
        for (const erhe::scene::Mesh_primitive& mesh_primitive : entry.primitives_before) {
            erhe::scene::Mesh_primitive after = mesh_primitive;
            if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape) {
                const erhe::primitive::Primitive_render_shape& shape = *mesh_primitive.primitive->render_shape;
                const std::shared_ptr<erhe::primitive::Triangle_soup> soup = shape.get_triangle_soup();
                if (!soup) {
                    failures[entry_index] = "triangle soup could not be read";
                    return;
                }
                std::shared_ptr<erhe::primitive::Triangle_soup> world_soup = std::make_shared<erhe::primitive::Triangle_soup>(*soup);
                if (!erhe::primitive::transform_triangle_soup(*world_soup, entry.world_from_node)) {
                    failures[entry_index] = "vertex format not float";
                    return;
                }
                std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(world_soup);
                primitive->lod_settings = mesh_primitive.primitive->lod_settings;
                bool renderable_ok = false;
                {
                    // GL: the buffer allocations need a worker share context; no-op elsewhere.
                    std::optional<erhe::graphics::Scoped_worker_context> worker_context;
                    if (graphics_device != nullptr) {
                        worker_context.emplace(*graphics_device);
                    }
                    renderable_ok = primitive->make_renderable_mesh(build_info, shape.get_normal_style());
                }
                const bool raytrace_ok = primitive->make_raytrace();
                if (!renderable_ok || !raytrace_ok) {
                    failures[entry_index] = "primitive build failed";
                    return;
                }
                after.primitive = std::move(primitive);
            }
            entry.primitives_after.push_back(std::move(after));
        }
    };
    if (context.executor != nullptr) {
        tf::Taskflow taskflow;
        for (std::size_t i = 0, end = m_entries.size(); i < end; ++i) {
            taskflow.emplace([&build_entry, i]() { build_entry(i); });
        }
        erhe::task::run(*context.executor, taskflow).wait();
    } else {
        for (std::size_t i = 0, end = m_entries.size(); i < end; ++i) {
            build_entry(i);
        }
    }

    std::vector<Entry> built;
    built.reserve(m_entries.size());
    for (std::size_t i = 0, end = m_entries.size(); i < end; ++i) {
        Entry& entry = m_entries[i];
        if (failures[i] == nullptr) {
            built.push_back(std::move(entry));
        } else {
            ++m_kept_reasons[failures[i]];
            log_operations->warn("Bake to World Space: '{}' keeps its transform ({})", entry.mesh->get_name(), failures[i]);
        }
    }
    m_entries = std::move(built);
    assign_placeholders();
    set_description(fmt::format("Bake to World Space: {} meshes, {} placeholders", m_entries.size(), get_placeholder_count()));
}

// A baked mesh leaves a placeholder only when some descendant stays in place:
// a child that is not baked, or a baked child that leaves a placeholder itself.
void Bake_world_space_operation::assign_placeholders()
{
    std::unordered_map<const erhe::Hierarchy*, Entry*> baked;
    for (Entry& entry : m_entries) {
        baked.emplace(entry.mesh.get(), &entry);
    }
    // Children first, so each child's decision is known.
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        Entry& entry = *it;
        bool needed = false;
        for (const std::shared_ptr<erhe::Hierarchy>& child : entry.mesh->get_children()) {
            const auto child_entry = baked.find(child.get());
            if ((child_entry == baked.end()) || child_entry->second->placeholder) {
                needed = true;
                break;
            }
        }
        if (!needed) {
            entry.placeholder.reset();
        } else if (!entry.placeholder) {
            entry.placeholder = std::make_shared<erhe::scene::Xform>(entry.mesh->get_name());
            entry.placeholder->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        }
    }
}

auto Bake_world_space_operation::get_placeholder_count() const -> std::size_t
{
    std::size_t count = 0;
    for (const Entry& entry : m_entries) {
        if (entry.placeholder) {
            ++count;
        }
    }
    return count;
}

void Bake_world_space_operation::execute(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_built) {
        build_primitives();
    }
    if (m_entries.empty() || !m_parameters.scene_root) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> root = m_parameters.scene_root->get_scene().get_root_node();
    erhe::Item_host* const item_host = root->get_item_host();
    if (item_host == nullptr) {
        return;
    }
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{item_host->item_host_mutex};

        if (m_world_space_created) {
            m_world_space->set_parent(root);
        }
        m_world_space->enable_flag_bits(c_baked_flag_bits);
        // Parents first: a baked child is taken from its parent's placeholder.
        // set_parent() keeps world transforms, so the mesh moves (children
        // riding along) before its own transform becomes the identity.
        for (Entry& entry : m_entries) {
            entry.execute_parent   = entry.mesh->get_parent().lock();
            entry.execute_position = entry.mesh->get_index_in_parent();
            const erhe::scene::Trs_transform parent_from_node = entry.mesh->parent_from_node_transform();
            entry.mesh->set_parent(m_world_space);
            if (entry.placeholder) {
                entry.placeholder->set_parent(entry.execute_parent, entry.execute_position);
                entry.placeholder->set_parent_from_node(parent_from_node);
                const std::vector<std::shared_ptr<erhe::Hierarchy>> children = entry.mesh->get_children();
                for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
                    move_child_keeping_local(child, entry.placeholder);
                }
            }
            entry.mesh->set_parent_from_node(erhe::scene::Trs_transform{});
            set_mesh_primitives(context, entry.mesh, entry.primitives_after);
            entry.mesh->enable_flag_bits(c_baked_flag_bits);
        }
    }
    // The optimized variants (the dispatcher takes the item host mutex itself).
    for (const Entry& entry : m_entries) {
        kickoff_deferred_finalize(context, entry.mesh);
    }
    log_operations->info("{}", describe());
}

void Bake_world_space_operation::undo(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (m_entries.empty() || !m_parameters.scene_root) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> root = m_parameters.scene_root->get_scene().get_root_node();
    erhe::Item_host* const item_host = root->get_item_host();
    if (item_host == nullptr) {
        return;
    }
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{item_host->item_host_mutex};

        // The inverse of execute(), step by step in reverse order: each step
        // finds the state its execute() step left.
        for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
            Entry& entry = *it;
            restore_flag_bits(*entry.mesh, entry.before_flag_bits);
            set_mesh_primitives(context, entry.mesh, entry.primitives_before);
            entry.mesh->set_parent(entry.execute_parent, entry.execute_position);
            entry.mesh->set_parent_from_node(entry.before_parent_from_node);
            if (entry.placeholder) {
                const std::vector<std::shared_ptr<erhe::Hierarchy>> children = entry.placeholder->get_children();
                for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
                    move_child_keeping_local(child, entry.mesh);
                }
                entry.placeholder->set_parent(std::shared_ptr<erhe::Hierarchy>{});
            }
            if (entry.mesh->get_name() != entry.before_name) {
                entry.mesh->set_name(entry.before_name); // renamed beside a sibling of the same name
            }
        }
        restore_flag_bits(*m_world_space, m_world_space_before_flag_bits);
        if (m_world_space_created) {
            m_world_space->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
    }
}

} // namespace editor
