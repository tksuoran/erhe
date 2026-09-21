#include "app_scenes.hpp"

#include "app_context.hpp"
#include "assets/asset_manager.hpp"
#include "editor_log.hpp"
#include "app_settings.hpp"
#include "items.hpp"
#include "operations/mesh_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/draw_mode_system.hpp"
#include "scene/scene_commit_queue.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_settings_resolve.hpp"
#include "time.hpp"
#include "config/generated/physics_config.hpp"

#include "content_library/content_library.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <utility>

namespace editor {

App_scenes::App_scenes(App_context& context)
    : m_context{context}
{
}

App_scenes::~App_scenes() noexcept
{
    // ~Scene_root calls unregister_from_editor_scenes() which modifies
    // m_scene_roots. Swap to a local so the member is empty before any
    // element destructors run, avoiding mutation of m_scene_roots while it
    // is being torn down.
    std::vector<std::shared_ptr<Scene_root>> roots;
    roots.swap(m_scene_roots);
    // Detach each scene_root from this registry up front. Without this the
    // later ~Scene_root would still believe it is registered and call
    // unregister_scene_root() on the now-empty member, which fails the
    // lookup and logs a spurious "not registered in App_scenes" error.
    for (const std::shared_ptr<Scene_root>& scene_root : roots) {
        scene_root->detach_from_editor_scenes(*this);
    }
}

void App_scenes::register_scene_root(const std::shared_ptr<Scene_root>& scene_root)
{
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

        const auto i = std::find(m_scene_roots.begin(), m_scene_roots.end(), scene_root);
        if (i != m_scene_roots.end()) {
            log_scene->error("Scene '{}' is already in registered in App_scenes", scene_root->get_name());
            return;
        }
        m_scene_roots.push_back(scene_root);
    }

    // R5.3: every registered scene gets an identity container record. The
    // manager pointer is null only outside a full editor run (App_scenes is
    // constructed before Asset_manager; scenes register at runtime, when
    // App_context is populated).
    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->on_scene_registered(scene_root);
    }
}

void App_scenes::unregister_scene_root(Scene_root* scene_root)
{
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

        const auto i = std::remove_if(
            m_scene_roots.begin(), m_scene_roots.end(),
            [scene_root](const std::shared_ptr<Scene_root>& entry) {
                return entry.get() == scene_root;
            }
        );
        if (i == m_scene_roots.end()) {
            log_scene->error("Scene '{}' not registered in App_scenes", scene_root->get_name());
        } else {
            m_scene_roots.erase(i, m_scene_roots.end());
        }
    }

    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->on_scene_unregistered(scene_root);
    }

    // If this scene's own item (issue #240) is currently selected, drop it from
    // the selection now: a torn-down Scene_root would otherwise leave the
    // Selection holding a Scene whose get_item_host() back-pointer dangles.
    const std::shared_ptr<erhe::scene::Scene> scene_item = scene_root->get_scene_item();
    if ((m_context.selection != nullptr) && scene_item && scene_item->is_selected()) {
        m_context.selection->remove_from_selection(scene_item);
    }
}

void App_scenes::notify_scene_source_path_changed(Scene_root& scene_root)
{
    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->on_scene_source_path_changed(scene_root);
    }
}

void App_scenes::imgui()
{
    for (const auto& scene_root : m_scene_roots) {
        scene_root->imgui();
    }
}

void App_scenes::update_physics_simulation_fixed_step(const Time_context& time_context)
{
    ERHE_PROFILE_FUNCTION();

    // Physics enable is resolved per scene (#239): a scene may override it off
    // while others keep simulating, so the gate is inside the per-scene loop.
    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        if (!physics.static_enable || !physics.dynamic_enable) {
            continue;
        }
        scene_root->update_physics_simulation_fixed_step(time_context.simulation_dt_s, physics);
    }
}

void App_scenes::before_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        const bool running = physics.static_enable && physics.dynamic_enable;
        // Edge-triggered: clears no_transform_update from body-driven nodes
        // on pause and restores it on resume, so paused bodies follow
        // hierarchy edits again (the flag otherwise sticks - no deactivation
        // events fire while the simulation is not stepping).
        scene_root->set_physics_simulation_running(running);
        if (!running) {
            continue;
        }
        scene_root->before_physics_simulation_steps();
    }
}

void App_scenes::flush_draw_lists()
{
    ERHE_PROFILE_FUNCTION();

    // Copy under m_mutex, flush outside it: flush_draw_lists() takes the
    // scene root's item_host_mutex, and m_mutex must never be held while
    // waiting on that (is_host_registered() is called from item-host code).
    std::vector<std::shared_ptr<Scene_root>> scene_roots;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        scene_roots = m_scene_roots;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
        scene_root->flush_draw_lists();
    }
}

namespace {

// The rebuilt primitive's color stream carries an authored color, whichever
// build it came from: the constant is the mesh's own displayColor. Both builds
// are marked - the optimized variant is a copy of the same vertices.
void mark_vertex_colored(erhe::primitive::Primitive& primitive)
{
    static constexpr erhe::primitive::Mesh_variant variants[2] = {
        erhe::primitive::Mesh_variant::original,
        erhe::primitive::Mesh_variant::optimized
    };
    for (const erhe::primitive::Mesh_variant variant : variants) {
        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape = primitive.get_render_shape(variant);
        if (shape) {
            shape->get_mutable_renderable_mesh().has_vertex_colors = true;
        }
    }
}

} // anonymous namespace

// The one color of a surface is vertex data, so a display color change is a
// rebuild of the mesh's primitives. Driven by the change (Scene_root's queue),
// never by a scan: a frame in which nothing was written does nothing here.
//
// This is the kickoff only: the builds run on executor workers and swap in
// through Scene_commit_queue, so an import that recolors hundreds of meshes
// costs this tick the grouping walk, not the builds.
void App_scenes::rebuild_display_colors()
{
    ERHE_PROFILE_FUNCTION();

    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        m_display_color_roots = m_scene_roots;
    }
    const bool defer = can_defer_display_color_rebuild();
    for (const std::shared_ptr<Scene_root>& scene_root : m_display_color_roots) {
        scene_root->take_display_color_meshes(m_display_color_meshes);
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_display_color_meshes) {
            if (!mesh) {
                continue;
            }
            if (defer && queue_display_color_build(*scene_root.get(), mesh)) {
                continue;
            }
            rebuild_display_color(*scene_root.get(), mesh);
        }
        dispatch_display_color_builds(scene_root);
        m_display_color_meshes.clear();
    }
    m_display_color_roots.clear();
}

auto App_scenes::can_defer_display_color_rebuild() const -> bool
{
    return
        (m_context.mesh_memory        != nullptr) &&
        (m_context.executor           != nullptr) &&
        (m_context.scene_commit_queue != nullptr) &&
        (m_context.graphics_device    != nullptr) &&
        m_context.graphics_device->supports_worker_contexts();
}

auto App_scenes::queue_display_color_build(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Mesh>& mesh_shared) -> bool
{
    erhe::scene::Mesh& mesh = *mesh_shared.get();

    // async_for_nodes_with_mesh dispatches content nodes of one item host.
    // Anything else - a mesh of a palette, a mesh that left this root between
    // the write and this tick - is the caller's synchronous case.
    if (!erhe::utility::test_bit_set(mesh.get_flag_bits(), erhe::Item_flags::content)) {
        return false;
    }
    if (mesh.get_item_host() != static_cast<erhe::Item_host*>(&scene_root)) {
        return false;
    }

    const glm::vec3 display_color = mesh.get_display_color();
    const glm::vec4 color{display_color.x, display_color.y, display_color.z, 1.0f};
    // Skinned meshes must rebuild into the skinned vertex format or the GPU
    // streams silently lose their joints, so the format is part of the key.
    const bool skinned = static_cast<bool>(mesh.skin);

    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    for (std::size_t index = 0, end = primitives.size(); index < end; ++index) {
        const erhe::primitive::Primitive* const source = primitives[index].primitive.get();
        if ((source == nullptr) || !source->render_shape) {
            continue;
        }
        const erhe::primitive::Primitive_render_shape&        render_shape  = *source->render_shape.get();
        const erhe::primitive::Normal_style                   normal_style  = render_shape.get_normal_style();
        const std::shared_ptr<erhe::geometry::Geometry>       geometry      = render_shape.get_geometry_const();
        std::shared_ptr<erhe::primitive::Triangle_soup>       triangle_soup {};
        const void*                                           source_key    {nullptr};
        if (geometry) {
            source_key = static_cast<const void*>(geometry.get());
        } else {
            triangle_soup = render_shape.get_triangle_soup();
            if (!triangle_soup) {
                continue;
            }
            source_key = static_cast<const void*>(triangle_soup.get());
        }

        Display_color_build* build = nullptr;
        for (Display_color_build& candidate : m_display_color_builds) {
            if (
                (candidate.source_key   == source_key  ) &&
                (candidate.color        == color       ) &&
                (candidate.normal_style == normal_style) &&
                (candidate.skinned      == skinned     )
            ) {
                build = &candidate;
                break;
            }
        }
        if (build == nullptr) {
            Display_color_build new_build;
            new_build.source_key    = source_key;
            new_build.color         = color;
            new_build.normal_style  = normal_style;
            new_build.skinned       = skinned;
            new_build.geometry      = geometry;
            new_build.triangle_soup = triangle_soup;
            m_display_color_builds.push_back(std::move(new_build));
            build = &m_display_color_builds.back();
        }
        Display_color_target target;
        target.mesh            = mesh_shared;
        target.primitive_index = index;
        build->targets.push_back(std::move(target));
    }
    return true;
}

void App_scenes::dispatch_display_color_builds(const std::shared_ptr<Scene_root>& scene_root)
{
    for (Display_color_build& build : m_display_color_builds) {
        // The task's items are the record's target meshes, so
        // async_for_nodes_with_mesh chains it after any task still pending for
        // one of them: a mesh recolored again while this build is in flight
        // gets a task that runs - and so commits - after this one, and the
        // later color is the one that stays.
        m_display_color_items.clear();
        for (const Display_color_target& target : build.targets) {
            m_display_color_items.push_back(target.mesh);
        }
        const erhe::primitive::Build_info build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info = build.skinned
                ? m_context.mesh_memory->make_skinned_primitive_buffer_info()
                : m_context.mesh_memory->make_primitive_buffer_info(),
            .constant_color = GEO::vec4f{build.color.x, build.color.y, build.color.z, build.color.w}
        };
        async_for_nodes_with_mesh(
            m_context,
            m_display_color_items,
            [scene_root, build = std::move(build), build_info](Mesh_operation_parameters&& parameters)
            {
                build_display_color_primitive(parameters.context, scene_root, build, build_info);
            },
            // The build manages its own NARROW worker-context scope around the
            // GPU buffer build only, so the raytrace build below it does not
            // hold a pool context.
            /*op_builds_gpu_meshes=*/false
        );
    }
    m_display_color_builds.clear();
    m_display_color_items.clear();
}

// Executor worker. Builds the record's Primitive once - the geometry and the
// triangle soup a primitive was built from are shared with every other mesh
// built from them, so neither is edited: a geometry build takes the color as
// Build_info::constant_color (the value the builder writes wherever the
// geometry authors no color of its own), and a soup build gets a recolored
// copy of the soup.
void App_scenes::build_display_color_primitive(
    App_context&                       context,
    const std::shared_ptr<Scene_root>& scene_root,
    const Display_color_build&         build,
    const erhe::primitive::Build_info& build_info
)
{
    ERHE_PROFILE_FUNCTION();

    std::shared_ptr<erhe::primitive::Primitive> new_primitive;
    if (build.geometry) {
        new_primitive = std::make_shared<erhe::primitive::Primitive>(build.geometry);
    } else {
        const std::shared_ptr<erhe::primitive::Triangle_soup> colored_soup =
            erhe::primitive::make_triangle_soup_with_constant_color(*build.triangle_soup.get(), build.color);
        if (!colored_soup) {
            return;
        }
        new_primitive = std::make_shared<erhe::primitive::Primitive>(colored_soup);
    }
    {
        // On GL a worker needs a share context for the GPU buffer allocation
        // inside the build. Scope is deliberately NARROW - around the GPU
        // build only, not the raytrace BVH build below - so the fixed context
        // pool does not cap the CPU-heavy part.
        const erhe::graphics::Scoped_worker_context worker_context{*context.graphics_device};
        if (!new_primitive->make_renderable_mesh(build_info, build.normal_style)) {
            log_scene->warn("display color rebuild failed to build a renderable mesh");
            return;
        }
    }
    static_cast<void>(new_primitive->make_raytrace());
    // The constant is the mesh's authored color, so the renderable mesh says
    // so: an unbound mesh with vertex colors renders with the vertex-colored
    // default material (Material_set).
    mark_vertex_colored(*new_primitive.get());

    context.scene_commit_queue->enqueue(
        [scene_root, targets = build.targets, new_primitive]()
        {
            ERHE_PROFILE_SCOPE("display color commit");
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            for (const Display_color_target& target : targets) {
                const std::shared_ptr<erhe::scene::Mesh>& mesh_shared = target.mesh;
                // The mesh's item host is re-read here: a mesh that left this
                // scene since the task was created (scene closed, import
                // undone, re-hosted) is skipped and its build dropped.
                if (mesh_shared->get_item_host() != static_cast<erhe::Item_host*>(scene_root.get())) {
                    continue;
                }
                std::vector<erhe::scene::Mesh_primitive> new_primitives = mesh_shared->get_primitives();
                if (target.primitive_index >= new_primitives.size()) {
                    continue;
                }
                new_primitives[target.primitive_index].primitive = new_primitive;
                // The raytrace instances follow the primitives, so the swap
                // happens between the root's rt update brackets. The mesh stays
                // in its host throughout: entering a host is itself one of the
                // writes that queue this rebuild (Mesh::handle_item_host_update),
                // so a rebuild that left and re-entered the host would queue
                // itself again every frame.
                scene_root->begin_mesh_rt_update(mesh_shared);
                mesh_shared->set_primitives(new_primitives);
                scene_root->end_mesh_rt_update(mesh_shared);
            }
        }
    );
}

// The generated quad geometry a `cards` draw mode supplies. Driven by the
// change (each scene's draw-mode system queue) for the same reasons the
// display colors are: the write can land on a worker thread or inside a tree
// walk, and the build inserts a prim into the tree.
void App_scenes::rebuild_draw_mode_proxies()
{
    ERHE_PROFILE_FUNCTION();

    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        m_draw_mode_roots = m_scene_roots;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : m_draw_mode_roots) {
        scene_root->get_draw_mode_system().flush_proxy_rebuilds(m_context);
    }
    m_draw_mode_roots.clear();
}

// One mesh's rebuild, on the main thread. This is the path of a backend
// without worker contexts (GL, null window), where a worker may not build GPU
// meshes, and of a queued mesh the async dispatcher does not take. The
// geometry and the triangle soup a primitive was built
// from are shared with every other mesh built from them, so neither is edited:
// a geometry build takes the color as Build_info::constant_color (the value the
// builder writes wherever the geometry authors no color of its own), and a soup
// build gets a recolored copy of the soup.
void App_scenes::rebuild_display_color(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Mesh>& mesh_shared)
{
    if (m_context.mesh_memory == nullptr) {
        return;
    }
    erhe::scene::Mesh& mesh          = *mesh_shared.get();
    const glm::vec3    display_color = mesh.get_display_color();
    const glm::vec4 color{display_color.x, display_color.y, display_color.z, 1.0f};

    erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        // Skinned meshes must rebuild into the skinned vertex format or the
        // GPU streams silently lose their joints.
        .buffer_info = mesh.skin
            ? m_context.mesh_memory->make_skinned_primitive_buffer_info()
            : m_context.mesh_memory->make_primitive_buffer_info(),
        .constant_color = GEO::vec4f{color.x, color.y, color.z, color.w}
    };

    std::vector<erhe::scene::Mesh_primitive> new_primitives = mesh.get_primitives();
    bool                                     any_rebuilt    = false;
    for (erhe::scene::Mesh_primitive& mesh_primitive : new_primitives) {
        const erhe::primitive::Primitive* const source = mesh_primitive.primitive.get();
        if ((source == nullptr) || !source->render_shape) {
            continue;
        }
        const erhe::primitive::Primitive_render_shape& render_shape = *source->render_shape.get();
        const erhe::primitive::Normal_style            normal_style = render_shape.get_normal_style();
        const std::shared_ptr<erhe::geometry::Geometry> geometry     = render_shape.get_geometry_const();
        std::shared_ptr<erhe::primitive::Primitive>     new_primitive;
        if (geometry) {
            new_primitive = std::make_shared<erhe::primitive::Primitive>(geometry);
        } else {
            const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = render_shape.get_triangle_soup();
            if (!soup) {
                continue;
            }
            const std::shared_ptr<erhe::primitive::Triangle_soup> colored_soup =
                erhe::primitive::make_triangle_soup_with_constant_color(*soup.get(), color);
            if (!colored_soup) {
                continue;
            }
            new_primitive = std::make_shared<erhe::primitive::Primitive>(colored_soup);
        }
        if (!new_primitive->make_renderable_mesh(build_info, normal_style)) {
            log_scene->warn("display color rebuild of '{}' failed to build a renderable mesh", mesh.get_name());
            continue;
        }
        static_cast<void>(new_primitive->make_raytrace());
        // The constant is the mesh's authored color, so the renderable mesh
        // says so: an unbound mesh with vertex colors renders with the
        // vertex-colored default material (Material_set).
        mark_vertex_colored(*new_primitive.get());
        mesh_primitive.primitive = std::move(new_primitive);
        any_rebuilt              = true;
    }
    if (!any_rebuilt) {
        return;
    }

    // The raytrace instances follow the primitives, so the swap happens
    // between the root's rt update brackets. The mesh stays in its host
    // throughout: entering a host is itself one of the writes that queue
    // this rebuild (Mesh::handle_item_host_update), so a rebuild that left
    // and re-entered the host would queue itself again every frame.
    scene_root.begin_mesh_rt_update(mesh_shared);
    mesh.set_primitives(new_primitives);
    scene_root.end_mesh_rt_update(mesh_shared);
}

void App_scenes::update_material_sets(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    // Copy under m_mutex for the same documented reason flush_draw_lists()
    // does: the work below takes scene-root locks, and m_mutex must never be
    // held while waiting on those.
    std::vector<std::shared_ptr<Scene_root>> scene_roots;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        scene_roots = m_scene_roots;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
        const std::shared_ptr<Content_library>& content_library = scene_root->get_content_library();
        if (!content_library) {
            continue;
        }
        const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials =
            content_library->get_all<erhe::primitive::Material>();
        const std::span<const std::shared_ptr<erhe::primitive::Material>> material_span{materials};

        // A root with a draw list carries TWO sets, and the two reconcile the
        // same library independently: a material normally holds a different
        // slot in each, and each render path resolves through the set it
        // binds.
        erhe::scene_renderer::Material_set& forward_set = scene_root->get_material_set();
        forward_set.sync_library(material_span);
        forward_set.flush_pending();
        if (forward_set.has_gpu()) {
            forward_set.update(command_buffer);
        }

        erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
        if (draw_list_scene != nullptr) {
            erhe::scene_renderer::Material_set& draw_list_set = draw_list_scene->get_material_set();
            draw_list_set.sync_library(material_span);
            if (draw_list_set.has_gpu()) {
                draw_list_set.update(command_buffer);
            }
        }
    }
}

void App_scenes::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION();

    // Scene::update_node_transforms() locks the scene's Item_host mutex
    // itself, synchronizing against async workers.
    for (const auto& scene_root : m_scene_roots) {
        scene_root->get_scene().update_node_transforms();
    }
}

void App_scenes::update_layout_nodes()
{
    ERHE_PROFILE_FUNCTION();

    // Each Scene keeps its layout nodes in its own Layout_system (the node
    // system of the Layout value group), so this touches only the layout
    // nodes - no per-frame hierarchy scan.
    for (const std::shared_ptr<Scene_root>& scene_root : m_scene_roots) {
        scene_root->get_scene().update_layouts();
    }
}

void App_scenes::after_physics_simulation_steps()
{
    ERHE_PROFILE_FUNCTION();

    for (const auto& scene_root : m_scene_roots) {
        const Physics_config& physics = get_effective_physics(*m_context.editor_settings, *scene_root);
        if (!physics.static_enable || !physics.dynamic_enable) {
            continue;
        }
        scene_root->after_physics_simulation_steps();
    }
}

auto App_scenes::get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&
{
    return m_scene_roots;
}

auto App_scenes::is_host_registered(const erhe::Item_host* item_host) -> bool
{
    if (item_host == nullptr) {
        return false;
    }
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    for (const std::shared_ptr<Scene_root>& scene_root : m_scene_roots) {
        if (static_cast<const erhe::Item_host*>(scene_root.get()) == item_host) {
            return true;
        }
    }
    return false;
}

auto App_scenes::get_single_scene_root() -> std::shared_ptr<Scene_root>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    if (m_scene_roots.size() == 1) {
        return m_scene_roots.front();
    }
    return {};
}

void App_scenes::sanity_check()
{
#if !defined(NDEBUG)
    for (const auto& scene_root : m_scene_roots) {
        scene_root->sanity_check();
    }
#endif
}

auto App_scenes::scene_combo(const char* label, std::shared_ptr<Scene_root>& in_out_selected_entry, const bool empty_option) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<Scene_root>> entries;
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    if (empty_entry) {
        names.push_back("(none)");
        entries.push_back({});
        ++index;
    }
    for (const auto& entry : m_scene_roots) {
        names.push_back(entry->get_name().c_str());
        entries.push_back(entry);
        if (in_out_selected_entry == entry) {
            selection_index = index;
        }
        ++index;
    }

    // TODO Move to begin / end combo
    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (in_out_selected_entry != entries.at(selection_index));
    if (selection_changed) {
        in_out_selected_entry = entries.at(selection_index);
    }
    return selection_changed;
}

} // namespace editor
