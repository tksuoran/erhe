#include "renderers/composer.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"

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
    const erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters view_parameters = get_color_view_parameters(context);
    for (std::size_t i = 0, end = composition_passes.size(); i < end; ++i) {
        const Composition_pass&    composition_pass = *composition_passes[i].get();
        Composition_pass_prepared& pass_prepared    = prepared.passes[i];
        pass_prepared.cull_view         = erhe::scene_renderer::Cull_view::none;
        pass_prepared.command_selection = erhe::scene_renderer::Command_selection_id::none;
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
    out.prepared = true;
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
            composition_pass.render_immediate(context);
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
