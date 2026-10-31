#include "renderers/composition_pass.hpp"
#include "editor_item_bits.hpp"

#include "app_context.hpp"
#include "renderers/render_settings.hpp"
#include "app_rendering.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/render_style.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_render_state.hpp"
#include "time.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_imgui/windows/pipelines.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/draw_list_renderer.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <imgui/imgui.h>

#include <chrono>

namespace editor {

namespace {

// The layers whose draw lists show the view's shader debug mode.
constexpr erhe::scene::Layer_id shader_debug_content_layers[] = { Mesh_layer_id::content };

} // anonymous namespace

Composition_pass::Composition_pass(const Composition_pass&)            = default;
Composition_pass& Composition_pass::operator=(const Composition_pass&) = default;
Composition_pass::~Composition_pass() noexcept                         = default;

Composition_pass::Composition_pass(const std::string_view name)
    : Item{name}
{
}

Composition_pass::Composition_pass(const std::string_view name, const std::size_t frames_in_flight)
    : Item{name}
{
    m_frame_stats.resize(frames_in_flight);
}

auto Composition_pass::get_frame_stats(const std::optional<uint64_t> frame_index) const -> const Composition_pass_stats*
{
    erhe::utility::Thread_roles::assert_main_thread();
    return m_frame_stats.find(frame_index);
}

auto Composition_pass::get_total_cpu_time_us(const Composition_pass_stats& stats) const -> double
{
    return stats.total_cpu_time_us - m_total_cpu_time_baseline_us;
}

auto Composition_pass::get_render_call_count(const Composition_pass_stats& stats) const -> std::size_t
{
    return stats.render_call_count - m_render_call_count_baseline;
}

void Composition_pass::reset_cpu_time_stats(const std::optional<uint64_t> frame_index)
{
    const Composition_pass_stats* const stats = get_frame_stats(frame_index);
    if (stats == nullptr) {
        return; // not rendered in that frame: the previous baseline stays
    }
    m_total_cpu_time_baseline_us = stats->total_cpu_time_us;
    m_render_call_count_baseline = stats->render_call_count;
}

auto mix(float x, float y, float a) -> float
{
    return x * (1.0f - a) + y * a;
}

auto triangle_wave(float t, float p) -> float
{
    return 2.0f * std::abs(2.0f * (t / p - std::floor(t / p + 0.5f))) - 1.0f;
}

auto make_selection_outline_settings(const App_context& context, const int64_t host_system_time_ns) -> erhe::scene_renderer::Primitive_interface_settings
{
    // Selection outline appearance is editor-global (Selection_outline_style),
    // shared by all scene views; edited in the Settings window.
    const Selection_outline_style& outline = context.render_settings->selection_outline;
    // The prepared frame's host time from the packet, not Time, which M1 of
    // the next frame advances meanwhile.
    const int64_t t0_ns  = host_system_time_ns;
    const double  t0     = static_cast<double>(t0_ns) / 1'000'000'000.0;
    const float   period = 1.0f / outline.selection_highlight_frequency;
    const float   t1     = static_cast<float>(::fmod(t0, period));
    const float   t2     = static_cast<float>(0.5f + triangle_wave(t1, period) * 0.5f);
    return erhe::scene_renderer::Primitive_interface_settings{
        .color_source    = erhe::scene_renderer::Primitive_color_source::constant_color,
        .constant_color0 = glm::mix(
            outline.selection_highlight_low,
            outline.selection_highlight_high,
            t2
        ),
        .constant_color1 = glm::vec4{0.2f, 0.5, 1.0f, 1.0f},
        // The active item of the selection pulses in its own color
        // (doc/editor/active_item.md D5), on the same phase.
        .constant_color_active = glm::mix(
            outline.active_highlight_low,
            outline.active_highlight_high,
            t2
        ),
        .size_source     = erhe::scene_renderer::Primitive_size_source::constant_size,
        .constant_size   = mix(
            outline.selection_highlight_width_low,
            outline.selection_highlight_width_high,
            t2
        ),
        .hovered_flag_bits = editor::Editor_item_flags::hovered,
        .active_item_flag_bits = editor::Editor_item_flags::active_item
    };
}

auto get_pass_primitive_settings(const Composition_pass_data& data, const Render_context& context, const int64_t host_system_time_ns) -> erhe::scene_renderer::Primitive_interface_settings
{
    if (data.selection_outline_pulse) {
        return make_selection_outline_settings(context.app_context, host_system_time_ns);
    }
    if (data.primitive_settings.has_value()) {
        return data.primitive_settings.value();
    }
    if (data.get_appearance) {
        return get_primitive_settings(data.get_appearance(context), data.primitive_mode);
    }
    return erhe::scene_renderer::Primitive_interface_settings{.hovered_flag_bits = editor::Editor_item_flags::hovered, .active_item_flag_bits = editor::Editor_item_flags::active_item};
}

auto c_str(const Composition_pass_result result) -> const char*
{
    switch (result) {
        case Composition_pass_result::never_rendered:          return "never_rendered";
        case Composition_pass_result::disabled:                return "disabled";
        case Composition_pass_result::is_enabled_false:        return "is_enabled_false";
        case Composition_pass_result::no_scene_root:           return "no_scene_root";
        case Composition_pass_result::primitive_mode_disabled: return "primitive_mode_disabled";
        case Composition_pass_result::no_mesh_layers:          return "no_mesh_layers";
        case Composition_pass_result::submitted:               return "submitted";
        case Composition_pass_result::submitted_draw_lists:    return "submitted_draw_lists";
        case Composition_pass_result::no_occlusion_view:       return "no_occlusion_view";
        case Composition_pass_result::submitted_occlusion_test: return "submitted_occlusion_test";
        default:                                               return "?";
    }
}

void Composition_pass::prepare(const Render_context& context, const int64_t host_system_time_ns, Composition_pass_prepared& out, const Composition_prepare_scope scope) const
{
    ERHE_PROFILE_FUNCTION();

    out.result             = Composition_pass_result::never_rendered;
    out.mesh_count         = 0;
    out.use_wide_lines     = false;
    out.use_draw_lists     = false;
    out.overdraw           = erhe::scene_renderer::Overdraw_count::off;
    out.shader_debug       = erhe::scene_renderer::Shader_debug::none;
    out.primitive_settings = erhe::scene_renderer::Primitive_interface_settings{};
    // The grid pass takes this view's grid (Viewport_render_state::grid).
    const bool view_grid = data.grid_from_view && (context.render_state != nullptr);
    const bool enabled   = view_grid ? context.render_state->grid.enabled : data.enabled;
    out.grid_parameters    = view_grid ? context.render_state->grid.parameters : data.grid_parameters;
    out.sky_parameters     = data.sky_parameters;
    out.buckets.clear();

    if (!enabled) {
        out.result = Composition_pass_result::disabled;
        return;
    }

    // Activation predicate (e.g. the selection stencil-mask pass is active
    // only when selection polygon fill is disabled).
    if (data.is_enabled && !data.is_enabled(context)) {
        out.result = Composition_pass_result::is_enabled_false;
        return;
    }

    const std::shared_ptr<Scene_root>& scene_root = data.override_scene_root
        ? data.override_scene_root
        : context.scene_root;
    if (!scene_root) {
        out.result = Composition_pass_result::no_scene_root;
        return;
    }

    // The raster occlusion test (doc/plans/occlusion_culling.md): its gates
    // are its is_enabled predicate (App_rendering); it draws the scene's draw
    // lists, so it needs the scene's Draw_list_scene. Whether the frame runs
    // the test (gpu classification on a capable device) is decided in the
    // render phase, when the view's pre-execute hook registers its view.
    if (data.kind == Composition_pass_kind::occlusion_test) {
        out.result = (scene_root->get_draw_list_scene() != nullptr)
            ? Composition_pass_result::submitted
            : Composition_pass_result::no_scene_root;
        return;
    }

    const Render_style_data* render_style = data.get_render_style
        ? &data.get_render_style(context)
        : nullptr;
    if (
        (render_style != nullptr) &&
        !is_primitive_mode_enabled(*render_style, data.primitive_mode)
    ) {
        out.result = Composition_pass_result::primitive_mode_disabled;
        return;
    }

    if (data.mesh_layers.empty()) {
        // Fullscreen pass (sky, grid): nothing to bucket.
        out.result = Composition_pass_result::submitted;
        return;
    }

    // Leaf-function scratch (AGENTS.md): the spans point into the scene's
    // mesh layers only while this call runs.
    static thread_local std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> s_mesh_spans;
    s_mesh_spans.clear();
    erhe::scene::Scene* scene = scene_root->get_hosted_scene();
    for (const auto id : data.mesh_layers) {
        const auto mesh_layer = scene->get_mesh_layer_by_id(id);
        if (mesh_layer) {
            s_mesh_spans.push_back(mesh_layer->meshes);
            out.mesh_count += mesh_layer->meshes.size();
        } else {
            log_composer->warn("mesh layer not found for id {}", id);
        }
    }
    if (s_mesh_spans.empty()) {
        out.result = Composition_pass_result::no_mesh_layers;
        return;
    }
    out.result = Composition_pass_result::submitted;

    // Edge-line composition passes go through Content_wide_line_renderer
    // (both the compute backend on devices with compute shaders and the
    // geometry-shader backend without). The meshes are fed in the prepare
    // phase (App_rendering::prepare_wide_line_feed()) and the compute
    // pre-pass is dispatched by viewport_scene_view.cpp / headset_view.cpp
    // before the render pass begins.
    const erhe::scene_renderer::Content_wide_line_renderer* content_wide_line_renderer = context.app_context.content_wide_line_renderer;
    if (
        (data.primitive_mode == erhe::primitive::Primitive_mode::edge_lines) &&
        (content_wide_line_renderer != nullptr) &&
        content_wide_line_renderer->is_enabled()
    ) {
        out.use_wide_lines = true;
        s_mesh_spans.clear();
        return;
    }

    out.primitive_settings = get_pass_primitive_settings(data, context, host_system_time_ns);
    out.overdraw           = counts_overdraw(data) ? context.overdraw : erhe::scene_renderer::Overdraw_count::off;

    // Draw-list path (doc/erhe/draw_list_renderer.md section 9.4): route to
    // the scene's persistent draw lists when the gate is on and this pass is
    // fully expressible with them - polygon fill, no per-pass shader debug
    // override (the view's shader debug mode is a draw-list resolution axis,
    // Draw_color_parameters::shader_debug), no shader / blend overrides, no
    // forced shader bits, and a plain opaque / alpha tested / translucent /
    // allow_all blending policy. Everything else takes the bucket path,
    // prepared here.
    const erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
    const erhe::scene_renderer::Shader_debug effective_shader_debug = data.shader_debug_override.has_value()
        ? data.shader_debug_override.value()
        : context.shader_debug;
    out.use_draw_lists =
        context.app_context.render_settings->use_draw_lists &&
        (draw_list_scene != nullptr) &&
        (data.primitive_mode == erhe::primitive::Primitive_mode::polygon_fill) &&
        !data.shader_debug_override.has_value() &&
        (data.shader_stages == nullptr) &&
        (data.color_blend_override == nullptr) &&
        (data.shader_key_force_enable_mask == 0u) &&
        (data.shader_key_force_disable_mask == 0u) &&
        (
            (data.blending_mode_policy == erhe::scene_renderer::Blending_mode_policy::opaque_primitives_only) ||
            (data.blending_mode_policy == erhe::scene_renderer::Blending_mode_policy::alpha_tested_primitives_only) ||
            (data.blending_mode_policy == erhe::scene_renderer::Blending_mode_policy::translucent_primitives_only) ||
            (data.blending_mode_policy == erhe::scene_renderer::Blending_mode_policy::allow_all)
        );
    if (out.use_draw_lists) {
        out.shader_debug = effective_shader_debug;
    }
    if (out.use_draw_lists || (scope == Composition_prepare_scope::gating_only)) {
        s_mesh_spans.clear();
        return;
    }

    // log_draw->trace("Render pass {} filter = {}", get_name(), filter.describe());
    context.app_context.forward_renderer->prepare(
        erhe::scene_renderer::Forward_renderer::Prepare_parameters{
            .mesh_spans                            = s_mesh_spans,
            // The root's FORWARD set - the one the draw binds (D5).
            .material_source                       = &scene_root->get_material_set(),
            .shader_key_boolean_mask_force_enable  = data.shader_key_force_enable_mask,
            .shader_key_boolean_mask_force_disable = data.shader_key_force_disable_mask,
            .blending_mode_policy                  = data.blending_mode_policy,
            .primitive_mode                        = data.primitive_mode,
            .primitive_settings                    = out.primitive_settings,
            .filter                                = data.filter,
            .shader_debug                          = effective_shader_debug,
            // Shader Debug override visualization applies to content meshes only; brush,
            // tool, controller and rendertarget meshes (no content flag) render normally.
            // A pass that forces its own variant supplies its own filter instead.
            .shader_debug_filter                   = data.shader_debug_override.has_value()
                ? data.shader_debug_override_filter
                : erhe::Item_filter{ .require_all_bits_set = erhe::Item_flags::content },
            .overdraw                              = out.overdraw
        },
        out.buckets
    );
    s_mesh_spans.clear();
}

auto counts_overdraw(const Composition_pass_data& data) -> bool
{
    return
        (data.kind == Composition_pass_kind::scene_content) &&
        (data.shader_stages == nullptr) &&
        !data.shader_debug_override.has_value() &&
        (std::find(data.mesh_layers.begin(), data.mesh_layers.end(), Mesh_layer_id::content) != data.mesh_layers.end());
}

auto get_draw_blending_selection(const Composition_pass_data& data) -> erhe::scene_renderer::Draw_blending_selection
{
    switch (data.blending_mode_policy) {
        case erhe::scene_renderer::Blending_mode_policy::opaque_primitives_only:       return erhe::scene_renderer::Draw_blending_selection::opaque_only;
        case erhe::scene_renderer::Blending_mode_policy::alpha_tested_primitives_only: return erhe::scene_renderer::Draw_blending_selection::alpha_tested_only;
        case erhe::scene_renderer::Blending_mode_policy::translucent_primitives_only:  return erhe::scene_renderer::Draw_blending_selection::translucent_only;
        default:                                                                        return erhe::scene_renderer::Draw_blending_selection::all;
    }
}

auto get_gpu_command_key(const Composition_pass& composition_pass, const Scene_view& scene_view) -> erhe::scene_renderer::Gpu_command_key
{
    return erhe::scene_renderer::Gpu_command_key{.pass = &composition_pass, .view = &scene_view};
}

auto get_color_view_parameters(const Render_context& context) -> erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters
{
    return erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters{
        .views         = context.views,
        .reverse_depth = context.scene_view.get_reverse_depth(),
        .depth_range   = context.scene_view.get_depth_range(),
        .conventions   = context.scene_view.get_conventions()
    };
}

void Composition_pass::render_immediate(const Render_context& context, const Composition_pass_views* const pass_views)
{
    prepare(context, context.app_context.render_phase_host_system_time_ns, m_immediate_prepared);
    m_immediate_prepared.cull_view         = erhe::scene_renderer::Cull_view::none;
    m_immediate_prepared.command_selection = erhe::scene_renderer::Command_selection_id::none;
    // The views the context's pre-execute hook registered for this pass
    // (the headset's: its culling view and GPU command selection, or its
    // occlusion view; Composer::add_immediate_cull_views() from the same
    // gating, so a registered draw-list pass is a draw-list pass here).
    if (pass_views != nullptr) {
        if (
            (m_immediate_prepared.result == Composition_pass_result::submitted) &&
            (m_immediate_prepared.use_draw_lists || (data.kind == Composition_pass_kind::occlusion_test))
        ) {
            m_immediate_prepared.cull_view         = pass_views->cull_view;
            m_immediate_prepared.command_selection = pass_views->command_selection;
        }
        render(context, m_immediate_prepared);
        return;
    }
    // Otherwise a pass prepared where it renders has no prepared culling
    // view: it takes the context's when it draws the draw list scene that
    // view belongs to, and draws unculled otherwise (the previews).
    if (
        m_immediate_prepared.use_draw_lists &&
        (context.immediate_cull_view != erhe::scene_renderer::Cull_view::none)
    ) {
        const std::shared_ptr<Scene_root>& scene_root = data.override_scene_root
            ? data.override_scene_root
            : context.scene_root;
        ERHE_VERIFY(scene_root);
        if (scene_root->get_draw_list_scene() == context.immediate_cull_draw_list_scene) {
            m_immediate_prepared.cull_view = context.immediate_cull_view;
        }
    }
    render(context, m_immediate_prepared);
}

void Composition_pass::render(const Render_context& context, const Composition_pass_prepared& prepared)
{
    ERHE_PROFILE_FUNCTION();

    // Frame statistics: the render phase's calls of a pass that keeps them
    // record into the frame's slot (the previews render their own passes on
    // the main thread and record nothing).
    Composition_pass_stats* const stats =
        (!m_frame_stats.is_empty() && (erhe::utility::Thread_roles::get_frame_phase() == erhe::utility::Frame_phase::r_render))
            ? &m_frame_stats.write(context.app_context.graphics_device->get_frame_index())
            : nullptr;
    const auto record_result = [stats](const Composition_pass_result result) {
        if (stats != nullptr) {
            stats->result = result;
        }
    };

    // CPU timing of the whole render() (P4 measurement); the scope guard
    // records on every return path, into the frame's statistics and the
    // render phase's running totals.
    class Cpu_timer_scope
    {
    public:
        Cpu_timer_scope(Composition_pass& pass, Composition_pass_stats* const stats)
            : m_pass {pass}
            , m_stats{stats}
            , m_start{std::chrono::steady_clock::now()}
        {
        }
        ~Cpu_timer_scope() noexcept
        {
            if (m_stats == nullptr) {
                return;
            }
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(end - m_start).count();
            m_pass.m_total_cpu_time_us += us;
            ++m_pass.m_render_call_count;
            m_stats->cpu_time_us       = us;
            m_stats->total_cpu_time_us = m_pass.m_total_cpu_time_us;
            m_stats->render_call_count = m_pass.m_render_call_count;
        }
    private:
        Composition_pass&                     m_pass;
        Composition_pass_stats*               m_stats;
        std::chrono::steady_clock::time_point m_start;
    };
    const Cpu_timer_scope cpu_timer_scope{*this, stats};

    if (stats != nullptr) {
        stats->scene_view_name        = context.scene_view.get_settings_key();
        stats->mesh_count             = prepared.mesh_count;
        stats->draw_list_entry_count  = 0;
        stats->draw_list_culled_count = 0;
        stats->draw_list_counts_frame_index = 0;
        stats->occlusion_tested_count       = 0;
        stats->occlusion_occluded_count     = 0;
        stats->occlusion_untested_count     = 0;
        stats->occlusion_culled_count       = 0;
        stats->occlusion_counts_frame_index = 0;
        stats->pvs_tested_count             = 0;
        stats->pvs_culled_count             = 0;
    }

    // The gating ran in the prepare phase (prepare()); a pass that did not
    // pass it records why and draws nothing.
    if (prepared.result != Composition_pass_result::submitted) {
        record_result(prepared.result);
        return;
    }

    const std::shared_ptr<Scene_root>& scene_root = data.override_scene_root
        ? data.override_scene_root
        : context.scene_root;
    ERHE_VERIFY(scene_root);

    // Slot spaces, not lists: each branch below binds the set its own path
    // resolves records through, and the two are independent
    // (doc/erhe/draw_list_material_set.md D0). Both are already updated for
    // this frame by App_scenes::update_material_sets().

    // Tool / rendertarget overlay passes ignore camera exposure (issue #230).
    const float exposure = (!data.ignore_exposure && (context.camera_snapshot != nullptr))
        ? context.camera_snapshot->get_exposure()
        : 1.0f;

    if (data.kind == Composition_pass_kind::occlusion_test) {
        // The view's occlusion test (doc/plans/occlusion_culling.md), in the
        // view's render pass after its depth-writing color passes; its view
        // was registered by the view's pre-execute hook
        // (Composer::add_cull_views()), or by the headset's
        // (Composer::add_immediate_cull_views()). A multiview pass (the
        // headset's eyes) rasterizes the boxes in every view, each view with
        // its own matrix and camera.
        if (prepared.cull_view == erhe::scene_renderer::Cull_view::none) {
            record_result(Composition_pass_result::no_occlusion_view);
            return;
        }
        erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
        ERHE_VERIFY(draw_list_scene != nullptr);
        ERHE_VERIFY(context.render_pass != nullptr);
        erhe::scene_renderer::Draw_occlusion_parameters occlusion_parameters{
            .render_encoder = *context.encoder,
            .render_pass    = context.render_pass,
            .viewport       = context.viewport,
            .view_count     = context.views.size(),
            .cull_view      = prepared.cull_view,
            .frame_index    = context.app_context.graphics_device->get_frame_index()
        };
        ERHE_VERIFY((occlusion_parameters.view_count >= 1) && (occlusion_parameters.view_count <= occlusion_parameters.clip_from_world.size()));
        const erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters view_parameters = get_color_view_parameters(context);
        for (std::size_t view = 0; view < occlusion_parameters.view_count; ++view) {
            erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters one_view = view_parameters;
            one_view.views = context.views.subspan(view, 1);
            const std::optional<glm::mat4> clip_from_world = erhe::scene_renderer::Draw_list_renderer::get_color_clip_from_world(one_view);
            ERHE_VERIFY(clip_from_world.has_value());
            const std::optional<glm::vec4> eye = erhe::scene_renderer::Draw_list_renderer::get_color_view_eye(one_view);
            ERHE_VERIFY(eye.has_value());
            occlusion_parameters.clip_from_world[view] = clip_from_world.value();
            occlusion_parameters.eye[view]             = eye.value();
        }
        draw_list_scene->draw_occlusion(occlusion_parameters);
        if (stats != nullptr) {
            const std::optional<erhe::scene_renderer::Retired_occlusion_counts> counts =
                draw_list_scene->get_retired_occlusion_counts(get_gpu_command_key(*this, context.scene_view));
            if (counts.has_value()) {
                stats->occlusion_tested_count       = counts->tested;
                stats->occlusion_occluded_count     = counts->occluded;
                stats->occlusion_untested_count     = counts->untested;
                stats->occlusion_culled_count       = counts->culled;
                stats->occlusion_counts_frame_index = counts->frame_index;
            }
        }
        record_result(Composition_pass_result::submitted_occlusion_test);
        return;
    }

    if (data.mesh_layers.empty()) {
        // log_composer->debug("render_fullscreen");
        context.app_context.forward_renderer->draw_primitives(
            erhe::scene_renderer::Forward_renderer::Primitive_render_parameters{
                .base = erhe::scene_renderer::Forward_renderer::Base_render_parameters{
                    .render_encoder    = *context.encoder,
                    .render_pass       = context.render_pass,
                    .viewport          = context.viewport,
                    .views             = context.views,
                    .exposure          = exposure,
                    .light_projections = nullptr,
                    .joint_poses       = {},
                    // No materials of its own: null selects the shared empty
                    // set (doc/erhe/draw_list_material_set.md D8).
                    .material_source   = nullptr,
                    .shader_key_boolean_mask_force_enable  = data.shader_key_force_enable_mask,
                    .shader_key_boolean_mask_force_disable = data.shader_key_force_disable_mask,
                    .reverse_depth     = context.scene_view.get_reverse_depth(),
                    .depth_range       = context.scene_view.get_depth_range(),
                    .conventions       = context.scene_view.get_conventions(),
                    .grid_parameters   = prepared.grid_parameters,
                    .sky_parameters    = prepared.sky_parameters,
                    .debug_label       = get_debug_label().string_view()
                },
                .vertex_count         = data.non_mesh_vertex_count,
                .base_render_pipeline = *data.base_render_pipelines.front(), // TODO
                .shader_stages = data.shader_stages
            },
            nullptr
        );
        record_result(Composition_pass_result::submitted);
        return;
    }

    if (prepared.use_wide_lines) {
        erhe::scene_renderer::Content_wide_line_renderer* content_wide_line_renderer = context.app_context.content_wide_line_renderer;
        ERHE_VERIFY(content_wide_line_renderer != nullptr);
        ERHE_VERIFY(context.render_pass != nullptr);
        const bool multiview = (context.views.size() >= 2);
        for (auto* base_render_pipeline : data.base_render_pipelines) {
            content_wide_line_renderer->render(
                *context.encoder,
                *base_render_pipeline,
                &erhe::graphics::Color_blend_state::color_blend_premultiplied, // TODO configurable?
                data.content_wide_line_group,
                multiview
            );
        }
        record_result(Composition_pass_result::submitted);
        return;
    }

    // Weight_display's joint weight ramp inputs: a viewport's copies of the
    // frame (Viewport_render_state), else the live values on the thread
    // that renders the view (previews, headset, scene images). The active
    // joint is held in a local so the node outlives the render call below,
    // which takes it as a raw pointer.
    const Viewport_render_state* const render_state = context.render_state;
    App_rendering&                     app_rendering = *context.app_context.app_rendering;
    const std::shared_ptr<erhe::scene::Node> debug_target_joint = (render_state != nullptr)
        ? render_state->debug_target_joint
        : app_rendering.debug_target_joint.lock();
    const glm::uvec4 debug_joint_indices = (render_state != nullptr)
        ? render_state->debug_joint_indices
        : app_rendering.debug_joint_indices;
    const std::span<const glm::vec4> debug_joint_colors = (render_state != nullptr)
        ? std::span<const glm::vec4>{render_state->debug_joint_colors}
        : std::span<const glm::vec4>{app_rendering.debug_joint_colors};

    // ERHE_SHADOW_DEPTH_BITS follows the format of the shadow map this
    // view's shadow node actually created (the preset's requested bit
    // count may resolve to another format, e.g. 24 -> D32_SFLOAT).
    const erhe::graphics::Texture* const shadow_texture = context.scene_view.get_shadow_texture();
    const uint32_t shadow_depth_bits = (shadow_texture != nullptr)
        ? erhe::scene_renderer::get_shadow_depth_bits_axis(shadow_texture->get_pixelformat())
        : 0u;

    // A counting pass draws with the overdraw variants, whose count buffer
    // the view's render phase bound into the context.
    const bool counting = (prepared.overdraw != erhe::scene_renderer::Overdraw_count::off);
    ERHE_VERIFY(!counting || (context.overdraw_count_buffer != nullptr));

    if (prepared.use_draw_lists) {
        erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
        ERHE_VERIFY(draw_list_scene != nullptr);
        const erhe::scene_renderer::Draw_blending_selection blending = get_draw_blending_selection(data);
        // The views and depth conventions the pass's culling view was built
        // from (Composer::add_cull_views()).
        const erhe::scene_renderer::Draw_list_renderer::Color_cull_view_parameters view_parameters = get_color_view_parameters(context);
        const erhe::scene_renderer::Draw_statistics statistics = context.app_context.draw_list_renderer->render(
            erhe::scene_renderer::Draw_list_renderer::Render_parameters{
                .base = erhe::scene_renderer::Base_render_parameters{
                    .render_encoder    = *context.encoder,
                    .render_pass       = context.render_pass,
                    .viewport          = context.viewport,
                    .views             = view_parameters.views,
                    .exposure          = exposure,
                    .ambient_light     = scene_root->get_render_settings(context.app_context.graphics_device->get_frame_index()).ambient_light,
                    .light_projections = context.scene_view.get_light_projections(),
                    .shadow_debug_light_index = context.shadow_debug_light_index,
                    .joint_poses       = scene_root->get_render_joint_poses(context.app_context.graphics_device->get_frame_index()),
                    .material_source   = &draw_list_scene->get_material_set(),
                    .shader_key_boolean_mask_force_enable  = 0u,
                    .shader_key_boolean_mask_force_disable = 0u,
                    .reverse_depth     = view_parameters.reverse_depth,
                    .depth_range       = view_parameters.depth_range,
                    .conventions       = view_parameters.conventions,
                    .overdraw_count_buffer = counting ? context.overdraw_count_buffer : nullptr,
                    .debug_label       = get_name(),
                },
                .draw_list_scene       = *draw_list_scene,
                .base_render_pipelines = data.base_render_pipelines,
                .layers                = data.mesh_layers,
                .blending              = blending,
                .primitive_settings    = prepared.primitive_settings,
                .filter                = data.filter,
                .shadow_filter         = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shadow_filter),
                .shadow_bias           = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shadow_bias),
                .shadow_technique      = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shadow_technique),
                .shadow_depth_bits     = shadow_depth_bits,
                .specular_anti_aliasing = static_cast<erhe::scene_renderer::Specular_anti_aliasing>(context.app_context.render_settings->graphics_preset.specular_anti_aliasing),
                .flat_draw_color       = context.app_context.render_settings->graphics_preset.flat_draw_color,
                .fp16_paths            = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shader_fp16_paths),
                .debug_joint_indices   = debug_joint_indices,
                .debug_joint_colors    = debug_joint_colors,
                .debug_target_joint    = debug_target_joint.get(),
                .color_blend_override  = nullptr,
                .cull_view             = prepared.cull_view,
                .command_selection     = prepared.command_selection,
                .overdraw              = prepared.overdraw,
                .shader_debug          = prepared.shader_debug,
                // The bucket path's default shader_debug_filter (content
                // meshes only): tool, brush and controller meshes keep
                // their normal variant.
                .shader_debug_layers   = shader_debug_content_layers
            }
        );
        if (stats != nullptr) {
            if (statistics.gpu_generated) {
                // The GPU-written counts arrive with a retired frame's
                // count readback.
                const std::optional<erhe::scene_renderer::Retired_gpu_color_counts> counts =
                    draw_list_scene->get_retired_gpu_color_counts(get_gpu_command_key(*this, context.scene_view));
                if (counts.has_value()) {
                    stats->draw_list_entry_count        = counts->drawn;
                    stats->draw_list_culled_count       = counts->filtered - counts->drawn;
                    stats->draw_list_counts_frame_index = counts->frame_index;
                }
            } else {
                stats->draw_list_entry_count  = statistics.entry_count;
                stats->draw_list_culled_count = statistics.culled_count;
            }
            // The PVS rule's counts of the pass's view in this frame
            // (doc/plans/pvs.md; the classification's, so the frame's own).
            const erhe::scene_renderer::Pvs_view_counts pvs_counts = draw_list_scene->get_cull_view_pvs_counts(
                context.app_context.graphics_device->get_frame_index(),
                prepared.cull_view
            );
            stats->pvs_tested_count = pvs_counts.tested;
            stats->pvs_culled_count = pvs_counts.culled;
        }
        record_result(Composition_pass_result::submitted_draw_lists);
        return;
    }

    // The bucket path: draw the snapshot prepare() bucketed. Reads no Mesh.
    context.app_context.forward_renderer->render(
        erhe::scene_renderer::Forward_renderer::Draw_parameters{
            .base = erhe::scene_renderer::Forward_renderer::Base_render_parameters{
                .render_encoder    = *context.encoder,
                .render_pass       = context.render_pass,
                .viewport          = context.viewport,
                .views             = context.views,
                .exposure          = exposure,
                .ambient_light     = scene_root->get_render_settings(context.app_context.graphics_device->get_frame_index()).ambient_light,
                .light_projections = context.scene_view.get_light_projections(),
                .shadow_debug_light_index = context.shadow_debug_light_index,
                .joint_poses       = scene_root->get_render_joint_poses(context.app_context.graphics_device->get_frame_index()),
                .material_source   = &scene_root->get_material_set(),
                .shader_key_boolean_mask_force_enable  = data.shader_key_force_enable_mask,
                .shader_key_boolean_mask_force_disable = data.shader_key_force_disable_mask,
                .reverse_depth     = context.scene_view.get_reverse_depth(),
                .depth_range       = context.scene_view.get_depth_range(),
                .conventions       = context.scene_view.get_conventions(),
                .overdraw_count_buffer = counting ? context.overdraw_count_buffer : nullptr,
                .debug_label       = get_name(),
            },
            .base_render_pipelines  = data.base_render_pipelines,
            .shadow_filter          = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shadow_filter),
            .shadow_bias            = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shadow_bias),
            .shadow_technique       = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shadow_technique),
            .shadow_depth_bits      = shadow_depth_bits,
            .specular_anti_aliasing = static_cast<erhe::scene_renderer::Specular_anti_aliasing>(context.app_context.render_settings->graphics_preset.specular_anti_aliasing),
            .flat_draw_color        = context.app_context.render_settings->graphics_preset.flat_draw_color,
            .fp16_paths             = static_cast<uint32_t>(context.app_context.render_settings->graphics_preset.shader_fp16_paths),
            .debug_joint_indices    = debug_joint_indices,
            .debug_joint_colors     = debug_joint_colors,
            .debug_target_joint     = debug_target_joint.get(),
            .shader_stages_override = data.shader_stages,
            .color_blend_override   = data.color_blend_override,
        },
        prepared.buckets
    );
    record_result(Composition_pass_result::submitted);
}

void Composition_pass::imgui()
{
    ImGui::Checkbox("Enabled", &data.enabled);
    if (ImGui::TreeNodeEx("Pipeline passes", ImGuiTreeNodeFlags_Framed)) {
        int pipeline_pass_index = 0;
        for (erhe::graphics::Base_render_pipeline* base_render_pipeline : data.base_render_pipelines) {
            ImGui::PushID(pipeline_pass_index++);
            erhe::imgui::pipeline_imgui(*base_render_pipeline);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }   
    ImGui::Text("Primitive Mode: %s", erhe::primitive::c_str(data.primitive_mode));

    if (ImGui::TreeNodeEx("Filter", ImGuiTreeNodeFlags_Framed)) {
        std::string require_all_bits_set           = erhe::Item_flags::to_string(data.filter.require_all_bits_set          );
        std::string require_at_least_one_bit_set   = erhe::Item_flags::to_string(data.filter.require_at_least_one_bit_set  );
        std::string require_all_bits_clear         = erhe::Item_flags::to_string(data.filter.require_all_bits_clear        );
        std::string require_at_least_one_bit_clear = erhe::Item_flags::to_string(data.filter.require_at_least_one_bit_clear);
        ImGui::Text("require_all_bits_set = %s",           require_all_bits_set.c_str());
        ImGui::Text("require_at_least_one_bit_set = %s",   require_at_least_one_bit_set.c_str());
        ImGui::Text("require_all_bits_clear = %s",         require_all_bits_clear.c_str());
        ImGui::Text("require_at_least_one_bit_clear = %s", require_at_least_one_bit_clear.c_str());
        ImGui::TreePop();
    }
    ImGui::Text("Primitive Mode: %s", erhe::primitive::c_str(data.primitive_mode));

    //ImGui::Checkbox("Allow shader stages override", &allow_shader_stages_override);
    //if (primitive_settings.has_value()) {
    //    //static void render_style_ui(Render_style_data& render_style);
    //}
}

}
