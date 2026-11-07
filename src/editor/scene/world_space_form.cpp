#include "scene/world_space_form.hpp"

#include "editor_item_bits.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "config/generated/lod_config_serialization.hpp"

#include "erhe_item/item_flags.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <fmt/format.h>

namespace editor {

World_space_form::World_space_form() = default;

World_space_form::~World_space_form() noexcept = default;

void World_space_form::install(Scene_root& scene_root, std::vector<World_space_form_mesh>&& meshes)
{
    ERHE_PROFILE_FUNCTION();
    uninstall(scene_root);

    const std::shared_ptr<erhe::scene::Node> root  = scene_root.get_scene().get_root_node();
    const std::shared_ptr<erhe::scene::Node> group = std::make_shared<erhe::scene::Xform>("World Space Form");
    group->enable_flag_bits(
        erhe::Item_flags::content             |
        erhe::Item_flags::render_proxy        |
        erhe::Item_flags::session_only        |
        erhe::Item_flags::no_transform_update |
        Editor_item_flags::lock_viewport_transform
    );
    std::vector<World_space_validity_entry>         entries;
    std::vector<std::weak_ptr<erhe::scene::Mesh>>   sources;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> proxies;
    for (World_space_form_mesh& form_mesh : meshes) {
        const std::shared_ptr<erhe::scene::Mesh>& mesh = form_mesh.mesh;
        if (!mesh) {
            continue;
        }
        World_space_validity_entry entry{
            .mesh            = mesh.get(),
            .world_from_node = form_mesh.world_from_node,
            .shapes          = form_mesh.shapes,
            .complete        = true
        };
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        std::vector<erhe::scene::Mesh_primitive> proxy_primitives;
        for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
            if (!primitives[i].primitive || !primitives[i].primitive->render_shape) {
                continue;
            }
            if ((i >= form_mesh.world_primitives.size()) || !form_mesh.world_primitives[i]) {
                entry.complete = false;
                break;
            }
            proxy_primitives.emplace_back(form_mesh.world_primitives[i], primitives[i].material);
        }
        entry.complete = entry.complete && !proxy_primitives.empty();
        if (entry.complete) {
            std::shared_ptr<erhe::scene::Mesh> proxy = std::make_shared<erhe::scene::Mesh>(fmt::format("{} (world space)", mesh->get_name()));
            proxy->layer_id = mesh->layer_id;
            proxy->set_primitives(proxy_primitives);
            proxy->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::render_proxy | Editor_item_flags::world_space_form);
            // The source meshes cast the shadows of both forms.
            proxy->set_value(erhe::scene::Mesh::shadow_cast_property, false);
            proxies.push_back(std::move(proxy));
            sources.push_back(mesh);
        }
        entries.push_back(std::move(entry));
    }
    const std::size_t admitted_count = entries.size();
    const std::size_t proxy_count    = proxies.size();
    {
        // Before the proxies enter the scene: their PVS identity is their source's (Scene_root::get_pvs_identity()).
        std::lock_guard<std::mutex> lock{m_mutex};
        m_source_by_proxy.clear();
        for (std::size_t i = 0; i < proxy_count; ++i) {
            m_source_by_proxy.emplace(proxies[i].get(), sources[i]);
        }
    }
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
        for (const std::shared_ptr<erhe::scene::Mesh>& proxy : proxies) {
            erhe::scene::set_mesh_parent(proxy, group);
        }
        for (const std::weak_ptr<erhe::scene::Mesh>& source : sources) {
            source.lock()->enable_flag_bits(Editor_item_flags::world_space_source);
        }
        if (root) {
            group->set_parent(root);
        }
    }
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_group_node = group;
        m_sources    = std::move(sources);
    }
    m_validity.reset(root.get(), std::move(entries), make_world_space_settings_signature(scene_root.get_scene_settings().export_settings));
    log_scene->info(
        "World-space form of '{}': {} admitted mesh node(s), {} drawn from World-space mesh items",
        scene_root.get_name(), admitted_count, proxy_count
    );
}

void World_space_form::uninstall(Scene_root& scene_root)
{
    m_validity.clear();
    std::shared_ptr<erhe::scene::Node>            group;
    std::vector<std::weak_ptr<erhe::scene::Mesh>> sources;
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        group   = std::move(m_group_node);
        sources = std::move(m_sources);
        m_group_node.reset();
        m_sources.clear();
        m_source_by_proxy.clear();
    }
    if (!group && sources.empty()) {
        return;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};
    for (const std::weak_ptr<erhe::scene::Mesh>& weak_source : sources) {
        const std::shared_ptr<erhe::scene::Mesh> source = weak_source.lock();
        if (source) {
            source->disable_flag_bits(Editor_item_flags::world_space_source);
        }
    }
    if (group) {
        group->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    }
}

auto World_space_form::get_validity() -> World_space_validity&
{
    return m_validity;
}

auto World_space_form::get_status() const -> World_space_form_status
{
    return m_validity.get_status();
}

auto World_space_form::find_source(const erhe::scene::Mesh& proxy) const -> std::shared_ptr<erhe::scene::Mesh>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = m_source_by_proxy.find(&proxy);
    return (i != m_source_by_proxy.end()) ? i->second.lock() : std::shared_ptr<erhe::scene::Mesh>{};
}

auto make_world_space_settings_signature(const std::optional<Scene_export_settings>& settings) -> std::string
{
    if (!settings.has_value()) {
        return "none";
    }
    return serialize(settings->lod, 0) + "|" + std::to_string(static_cast<unsigned int>(settings->instancing)) + "|" + std::to_string(settings->instancing_budget_mib);
}

}
