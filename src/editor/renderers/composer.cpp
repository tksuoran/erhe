#include "renderers/composer.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/render_context.hpp"
#include "renderers/render_settings.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

namespace editor {

// TODO Do deep copy instead / use Hierarchy

Composer::Composer(const Composer& other)
    : composition_passes{other.composition_passes}
{
}

Composer& Composer::operator=(const Composer& other)
{
    composition_passes = other.composition_passes;
    return *this;
}

Composer::Composer(Composer&& old) noexcept
    : composition_passes{std::move(old.composition_passes)}
{
}

Composer& Composer::operator=(Composer&& old) noexcept
{
    if (this != &old) {
        composition_passes = std::move(old.composition_passes);
    }
    return *this;
}

Composer::~Composer() noexcept
{
}

Composer::Composer(const std::string_view name)
    : Item{name}
{
}

namespace {

[[nodiscard]] auto is_drawn_for_content(const Composition_pass& composition_pass, const Render_content content) -> bool
{
    if (
        (content != Render_content::scene_and_editor_aids) &&
        (composition_pass.data.kind == Composition_pass_kind::editor_aid)
    ) {
        return false;
    }
    if (
        (content == Render_content::scene_surfaces) &&
        (composition_pass.data.kind == Composition_pass_kind::background)
    ) {
        return false;
    }
    // The occlusion test runs in the viewports only (doc/plans/
    // occlusion_culling.md); a scene image render leaves it out.
    if (
        (content != Render_content::scene_and_editor_aids) &&
        (composition_pass.data.kind == Composition_pass_kind::occlusion_test)
    ) {
        return false;
    }
    return true;
}

}

auto Composer_frame_slots::begin_prepare(const uint64_t frame_index, const std::size_t frames_in_flight) -> Composer_prepared&
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY(frames_in_flight > 0);
    if (m_slots.size() != frames_in_flight) {
        m_slots.resize(frames_in_flight);
    }
    Composer_prepared& slot = m_slots[frame_index % frames_in_flight];
    slot.frame_index      = frame_index;
    slot.prepared         = false;
    slot.cull_views_added = false;
    slot.id_pass.prepared = false;
    slot.id_pass.scan.reset();
    slot.wide_line_feed.clear();
    return slot;
}

auto Composer_frame_slots::get_prepared(const uint64_t frame_index) const -> const Composer_prepared&
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    ERHE_VERIFY(!m_slots.empty());
    const Composer_prepared& slot = m_slots[frame_index % m_slots.size()];
    // The prepare phase of this frame filled the slot, and the prepare phase
    // of no later frame has reused it yet.
    ERHE_VERIFY(slot.prepared && (slot.frame_index == frame_index));
    return slot;
}

auto Composer_frame_slots::get_render_prepared(const uint64_t frame_index) -> Composer_prepared&
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    ERHE_VERIFY(!m_slots.empty());
    Composer_prepared& slot = m_slots[frame_index % m_slots.size()];
    ERHE_VERIFY(slot.prepared && (slot.frame_index == frame_index));
    return slot;
}

void Composer::add_cull_views(const Render_context& context, Composer_prepared& prepared)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    ERHE_VERIFY(prepared.prepared);
    ERHE_VERIFY(!prepared.cull_views_added);
    ERHE_VERIFY(prepared.passes.size() == composition_passes.size());
    erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters view_parameters = get_color_view_parameters(context);
    // The PVS query box the prepare phase computed and registered.
    view_parameters.pvs_query_min = prepared.pvs_query_min;
    view_parameters.pvs_query_max = prepared.pvs_query_max;
    for (std::size_t i = 0, end = composition_passes.size(); i < end; ++i) {
        const Composition_pass&    composition_pass = *composition_passes[i].get();
        Composition_pass_prepared& pass_prepared    = prepared.passes[i];
        pass_prepared.cull_view         = erhe::scene_renderer::Cull_view::none;
        pass_prepared.command_selection = erhe::scene_renderer::Command_selection_id::none;
        // The occlusion test pass (doc/plans/occlusion_culling.md): its view
        // is the view's color culling view (the one the view's draw-list
        // passes share), made an occlusion view of the frame.
        if (composition_pass.data.kind == Composition_pass_kind::occlusion_test) {
            if (pass_prepared.result != Composition_pass_result::submitted) {
                continue;
            }
            const std::shared_ptr<Scene_root>& occlusion_scene_root = composition_pass.data.override_scene_root
                ? composition_pass.data.override_scene_root
                : context.scene_root;
            ERHE_VERIFY(occlusion_scene_root);
            erhe::scene_renderer::Draw_list_scene* draw_list_scene = occlusion_scene_root->get_draw_list_scene();
            ERHE_VERIFY(draw_list_scene != nullptr);
            const erhe::scene_renderer::Cull_view cull_view = erhe::scene_renderer::Draw_list_renderer::add_color_cull_view(*draw_list_scene, prepared.frame_index, view_parameters);
            pass_prepared.cull_view = erhe::scene_renderer::Draw_list_renderer::add_color_occlusion_view(
                *draw_list_scene,
                prepared.frame_index,
                cull_view,
                view_parameters,
                get_gpu_command_key(composition_pass, context.scene_view)
            );
            continue;
        }
        // The passes Composition_pass::render() draws through draw lists.
        if ((pass_prepared.result != Composition_pass_result::submitted) || !pass_prepared.use_draw_lists) {
            continue;
        }
        const std::shared_ptr<Scene_root>& pass_scene_root = composition_pass.data.override_scene_root
            ? composition_pass.data.override_scene_root
            : context.scene_root;
        ERHE_VERIFY(pass_scene_root);
        erhe::scene_renderer::Draw_list_scene* draw_list_scene = pass_scene_root->get_draw_list_scene();
        ERHE_VERIFY(draw_list_scene != nullptr);
        // Add_cull_view() returns the view an earlier pass of this scene
        // added (same purpose, same planes).
        pass_prepared.cull_view = erhe::scene_renderer::Draw_list_renderer::add_color_cull_view(*draw_list_scene, prepared.frame_index, view_parameters);
        // What the pass draws, for the frame's GPU command generation
        // (none for an unculled pass).
        pass_prepared.command_selection = context.app_context.draw_list_renderer->add_color_command_selection(
            *draw_list_scene,
            prepared.frame_index,
            pass_prepared.cull_view,
            composition_pass.data.filter,
            composition_pass.data.mesh_layers,
            get_draw_blending_selection(composition_pass.data),
            get_gpu_command_key(composition_pass, context.scene_view)
        );
    }
    prepared.cull_views_added = true;
}

void Composer::add_immediate_cull_views(
    const Render_context&                  context,
    erhe::scene_renderer::Draw_list_scene& draw_list_scene,
    const erhe::scene_renderer::Cull_view  cull_view,
    const glm::vec4&                       occlusion_near_plane,
    const std::size_t                      view_count,
    std::vector<Composition_pass_views>&   out
)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    // Leaf-function scratch (AGENTS.md): the gating decisions of one pass,
    // read right after prepare() wrote them. gating_only leaves the buckets
    // empty.
    static thread_local Composition_pass_prepared s_gating;

    const uint64_t frame_index = context.app_context.graphics_device->get_frame_index();
    out.resize(composition_passes.size());
    for (std::size_t i = 0, end = composition_passes.size(); i < end; ++i) {
        const Composition_pass& composition_pass = *composition_passes[i].get();
        Composition_pass_views& pass_views = out[i];
        pass_views = Composition_pass_views{};
        if ((cull_view == erhe::scene_renderer::Cull_view::none) || !is_drawn_for_content(composition_pass, context.content)) {
            continue;
        }
        composition_pass.prepare(context, context.app_context.render_phase_host_system_time_ns, s_gating, Composition_prepare_scope::gating_only);
        if (s_gating.result != Composition_pass_result::submitted) {
            continue;
        }
        const std::shared_ptr<Scene_root>& pass_scene_root = composition_pass.data.override_scene_root
            ? composition_pass.data.override_scene_root
            : context.scene_root;
        if (!pass_scene_root || (pass_scene_root->get_draw_list_scene() != &draw_list_scene)) {
            continue; // another draw list scene: unculled, CPU-written commands
        }
        const erhe::scene_renderer::Gpu_command_key key = get_gpu_command_key(composition_pass, context.scene_view);
        if (composition_pass.data.kind == Composition_pass_kind::occlusion_test) {
            pass_views.cull_view = draw_list_scene.add_occlusion_view(frame_index, cull_view, occlusion_near_plane, key, view_count);
            continue;
        }
        if (!s_gating.use_draw_lists) {
            continue;
        }
        pass_views.cull_view         = cull_view;
        pass_views.command_selection = context.app_context.draw_list_renderer->add_color_command_selection(
            draw_list_scene,
            frame_index,
            cull_view,
            composition_pass.data.filter,
            composition_pass.data.mesh_layers,
            get_draw_blending_selection(composition_pass.data),
            key
        );
    }
}

void Composer::prepare(const Render_context& context, const int64_t host_system_time_ns, Composer_prepared& out)
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    if (out.passes.size() != composition_passes.size()) {
        out.passes.resize(composition_passes.size());
    }
    for (std::size_t i = 0, end = composition_passes.size(); i < end; ++i) {
        const Composition_pass& composition_pass = *composition_passes[i].get();
        Composition_pass_prepared& prepared = out.passes[i];
        prepared.cull_view         = erhe::scene_renderer::Cull_view::none;
        prepared.command_selection = erhe::scene_renderer::Command_selection_id::none;
        if (!is_drawn_for_content(composition_pass, context.content)) {
            prepared.result = Composition_pass_result::never_rendered;
            prepared.buckets.clear();
            continue;
        }
        composition_pass.prepare(context, host_system_time_ns, prepared);
    }
    prepare_pvs_query(context, out);
    out.prepared = true;
}

auto get_pvs_view_key(const Scene_view& scene_view) -> uint64_t
{
    return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(&scene_view));
}

void Composer::prepare_pvs_query(const Render_context& context, Composer_prepared& out)
{
    // The PVS query box (doc/plans/pvs.md D5, "Runtime consumption"): a
    // viewport's perspective camera position +- the query margin. A scene
    // image capture, an orthographic camera (no single position) and a
    // multiview pass get none.
    out.pvs_query_min = glm::vec3{ 0.0f};
    out.pvs_query_max = glm::vec3{-1.0f};
    if (
        (context.viewport_scene_view == nullptr) ||
        (context.content != Render_content::scene_and_editor_aids) ||
        (context.views.size() != 1) ||
        (context.views.front().camera == nullptr) ||
        context.views.front().camera->projection().is_orthographic()
    ) {
        return;
    }
    const glm::vec3 position = glm::vec3{context.views.front().camera->position_in_world()};
    const float     margin   = context.app_context.render_settings->pvs_query_margin;
    out.pvs_query_min = position - glm::vec3{margin};
    out.pvs_query_max = position + glm::vec3{margin};
    // The residency policy's change site ("Residency and streaming"): the
    // view's box, registered with the scene's draw lists in the prepare
    // phase; the view is named by its Scene_view across frames.
    if (!context.scene_root) {
        return;
    }
    erhe::scene_renderer::Draw_list_scene* draw_list_scene = context.scene_root->get_draw_list_scene();
    if (draw_list_scene == nullptr) {
        return;
    }
    draw_list_scene->register_pvs_view(get_pvs_view_key(context.scene_view), out.frame_index, out.pvs_query_min, out.pvs_query_max);
}

void Composer::render(const Render_context& context, const bool include_content, const bool include_overlay)
{
    //log_composer->trace("Composer::render()");
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    const Composer_prepared* const prepared = context.composer_prepared;
    if (prepared != nullptr) {
        ERHE_VERIFY(prepared->passes.size() == composition_passes.size());
        // The view's pre-execute hook added the frame's culling views.
        ERHE_VERIFY(prepared->cull_views_added);
    }
    for (std::size_t i = 0, end = composition_passes.size(); i < end; ++i) {
        Composition_pass& composition_pass = *composition_passes[i].get();
        const bool is_overlay = composition_pass.data.overlay;
        if (is_overlay ? !include_overlay : !include_content) {
            continue;
        }
        if (!is_drawn_for_content(composition_pass, context.content)) {
            continue;
        }
        // log_composer->trace("  rp: {}", composition_pass->describe());
        if (prepared != nullptr) {
            composition_pass.render(context, prepared->passes[i]);
        } else {
            composition_pass.render_immediate(context, (i < context.immediate_pass_views.size()) ? &context.immediate_pass_views[i] : nullptr);
        }
    }
}

void Composer::imgui()
{
    if (!ImGui::TreeNodeEx("Composer", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    int pass_index = 0;
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    for (const auto& composition_pass : composition_passes) {
        ImGui::PushID(pass_index++);
        if (ImGui::TreeNodeEx(composition_pass->describe().c_str(), ImGuiTreeNodeFlags_Framed)) {
            composition_pass->imgui();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
}

}
