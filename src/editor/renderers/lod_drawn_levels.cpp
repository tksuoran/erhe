#include "renderers/lod_drawn_levels.hpp"

#include "erhe_scene_renderer/draw_list.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <mutex>

namespace editor {

auto get_lod_view_key(const Scene_view& scene_view) -> uint64_t
{
    return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(&scene_view));
}

Lod_drawn_levels::Lod_drawn_levels(const std::size_t frames_in_flight)
{
    ERHE_VERIFY(frames_in_flight > 0);
    m_query_slots.resize(frames_in_flight);
    m_results.resize(frames_in_flight);
}

void Lod_drawn_levels::request(
    const Lod_drawn_levels_requester                          requester,
    const std::shared_ptr<Scene_root>&                        scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes
)
{
    erhe::utility::Thread_roles::assert_main_thread();
    ERHE_VERIFY(requester < Lod_drawn_levels_requester::count);
    Request& request = m_requests[static_cast<std::size_t>(requester)];
    request.pending    = true;
    request.scene_root = scene_root;
    request.meshes.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        request.meshes.push_back(mesh);
    }
}

void Lod_drawn_levels::prepare(const uint64_t frame_index)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    Query_slot& slot = m_query_slots[frame_index % m_query_slots.size()];
    slot.frame_index = frame_index;
    slot.prepared    = true;
    slot.queries.clear();
    for (Request& request : m_requests) {
        if (!request.pending) {
            continue;
        }
        request.pending            = false;
        request.served_frame_index = frame_index;
        const std::shared_ptr<Scene_root> scene_root = request.scene_root.lock();
        if (!scene_root) {
            continue;
        }
        const erhe::scene_renderer::Draw_list_scene* const draw_list_scene = scene_root->get_draw_list_scene();
        if (draw_list_scene == nullptr) {
            continue;
        }
        // The draw lists' object map and the meshes' primitives are main
        // thread state the finalize commits write under the item host lock.
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
        for (const std::weak_ptr<erhe::scene::Mesh>& weak_mesh : request.meshes) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = weak_mesh.lock();
            if (!mesh || (mesh->get_item_host() != scene_root.get())) {
                continue;
            }
            for (std::size_t primitive_index = 0, end = mesh->get_primitives().size(); primitive_index < end; ++primitive_index) {
                const erhe::scene_renderer::Draw_list_published_entry entry = draw_list_scene->find_published_color_entry(frame_index, mesh.get(), primitive_index);
                if (!entry.is_valid()) {
                    continue;
                }
                // Two requesters may name the same mesh: one query each.
                const bool listed = std::any_of(
                    slot.queries.begin(), slot.queries.end(),
                    [draw_list_scene, &entry](const Query& query) {
                        return
                            (query.draw_list_scene == draw_list_scene) &&
                            (query.entry.draw_list_index == entry.draw_list_index) &&
                            (query.entry.entry_index == entry.entry_index);
                    }
                );
                if (listed) {
                    continue;
                }
                slot.queries.push_back(
                    Query{
                        .draw_list_scene = draw_list_scene,
                        .mesh_id         = mesh->get_id(),
                        .entry           = entry
                    }
                );
            }
        }
    }
}

void Lod_drawn_levels::record_view(
    const uint64_t                               frame_index,
    const Scene_view&                            scene_view,
    const erhe::scene_renderer::Draw_list_scene& draw_list_scene,
    const erhe::scene_renderer::Cull_view        cull_view
)
{
    erhe::utility::Thread_roles::assert_render_thread();
    const Query_slot& slot = m_query_slots[frame_index % m_query_slots.size()];
    if (!slot.prepared || (slot.frame_index != frame_index)) {
        return; // the frame's prepare phase did not prepare queries
    }
    // The frame's result: started by its first view.
    Lod_drawn_levels_frame& result = m_results.write(frame_index);
    if (result.frame_index != frame_index) {
        result.frame_index = frame_index;
        result.levels.clear();
    }
    const uint64_t view_key = get_lod_view_key(scene_view);
    for (const Query& query : slot.queries) {
        if (query.draw_list_scene != &draw_list_scene) {
            continue;
        }
        result.levels.push_back(
            Lod_drawn_level{
                .view_key             = view_key,
                .mesh_id              = query.mesh_id,
                .mesh_primitive_index = query.entry.mesh_primitive_index,
                .drawn                = draw_list_scene.get_published_entry_lod_level(frame_index, cull_view, query.entry)
            }
        );
    }
}

auto Lod_drawn_levels::find(const std::optional<uint64_t> frame_index) const -> const Lod_drawn_levels_frame*
{
    return m_results.find(frame_index);
}

auto Lod_drawn_levels::get_served_frame_index(const Lod_drawn_levels_requester requester) const -> std::optional<uint64_t>
{
    ERHE_VERIFY(requester < Lod_drawn_levels_requester::count);
    return m_requests[static_cast<std::size_t>(requester)].served_frame_index;
}

} // namespace editor
