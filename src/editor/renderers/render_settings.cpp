#include "renderers/render_settings.hpp"
#include "renderers/render_settings_access.hpp"

#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

void Render_settings::snapshot(const Editor_settings_config& editor_settings, const App_settings& app_settings)
{
    ERHE_PROFILE_FUNCTION();

    sky                              = editor_settings.sky;
    shadow_frustum_fit               = editor_settings.shadow_frustum_fit;
    selection_outline                = editor_settings.selection_outline;
    render_style_appearance          = editor_settings.render_style_appearance;
    selected_render_style_appearance = editor_settings.selected_render_style_appearance;
    debug_visualizations_style       = editor_settings.debug_visualizations_style;
    ddgi                             = editor_settings.ddgi;
    radiance_cascades                = editor_settings.radiance_cascades;
    ray_trace                        = editor_settings.ray_trace;
    content_edge_lines               = editor_settings.content_edge_lines;
    exclude_unlit_primitives         = editor_settings.exclude_unlit_primitives;
    indirect_diffuse_source          = editor_settings.indirect_diffuse_source;
    culling_classification           = editor_settings.culling_classification;
    lightmap_bicubic_sampling        = editor_settings.lightmap.bicubic_sampling;

    graphics_preset                  = app_settings.graphics.current_graphics_preset;
    use_draw_lists                   = app_settings.get_use_draw_lists();
    headlight_when_unlit             = app_settings.graphics.headlight_when_unlit;
    sky_enabled                      = app_settings.graphics.sky_enabled;
    shadow_maps_frozen               = app_settings.shadow_maps_frozen_session;
    shadow_map_cache                 = app_settings.shadow_map_cache_session;
}

void Render_settings_frames::initialize(erhe::graphics::Device& device)
{
    erhe::utility::Thread_roles::assert_main_thread();
    ERHE_VERIFY(m_frames.empty());
    const std::size_t frames_in_flight = device.get_number_of_frames_in_flight();
    ERHE_VERIFY(frames_in_flight > 0);
    m_device = &device;
    m_frames.resize(frames_in_flight);
    m_latest_copy = m_frames.size();
}

void Render_settings_frames::snapshot(
    const uint64_t                frame_index,
    const Editor_settings_config& editor_settings,
    const App_settings&           app_settings
)
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY(!m_frames.empty());
    const std::size_t copy_index = static_cast<std::size_t>(frame_index % m_frames.size());
    Frame_copy&       copy       = m_frames[copy_index];
    copy.settings.snapshot(editor_settings, app_settings);
    copy.frame_index = frame_index;
    m_latest_copy    = copy_index;
}

auto Render_settings_frames::get_for_calling_role() const -> const Render_settings&
{
    ERHE_VERIFY(!m_frames.empty()); // initialized
    if (erhe::utility::Thread_roles::is_render_role()) {
        // m_latest_copy is the main thread's: with frame pipelining M2 of
        // the next frame writes it while this frame renders.
        const uint64_t    frame_index = m_device->get_frame_index();
        const Frame_copy& copy        = m_frames[frame_index % m_frames.size()];
        // The prepare phase of the rendered frame filled the copy, and the
        // prepare phase of no later frame has reused it yet.
        ERHE_VERIFY(copy.frame_index == frame_index);
        return copy.settings;
    }
    ERHE_VERIFY(m_latest_copy < m_frames.size()); // snapshot at least once
    return m_frames[m_latest_copy].settings;
}

auto Render_settings_access::operator->() const -> const Render_settings*
{
    ERHE_VERIFY(frames != nullptr);
    return &frames->get_for_calling_role();
}

auto Render_settings_access::operator*() const -> const Render_settings&
{
    ERHE_VERIFY(frames != nullptr);
    return frames->get_for_calling_role();
}

} // namespace editor
