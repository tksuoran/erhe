#pragma once

#include "config/generated/content_edge_lines_config.hpp"
#include "config/generated/culling_classification.hpp"
#include "config/generated/ddgi_config.hpp"
#include "config/generated/debug_visualizations_style.hpp"
#include "config/generated/graphics_preset_entry.hpp"
#include "config/generated/indirect_diffuse_source.hpp"
#include "config/generated/radiance_cascades_config.hpp"
#include "config/generated/ray_trace_config.hpp"
#include "config/generated/render_style_appearance.hpp"
#include "config/generated/selection_outline_style.hpp"
#include "config/generated/shadow_frustum_fit_config.hpp"
#include "config/generated/sky_config.hpp"

#include "erhe_scene/camera_snapshot.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

struct Editor_settings_config;

namespace erhe::graphics { class Device; }

namespace editor {

class App_settings;
class Scene_root;

// The settings the render phase reads, copied from the live editor and
// graphics settings by the main thread at the end of M2
// (Editor::snapshot_render_settings; doc/editor/editor.md "Frame phases and
// the render thread"). The windows edit the live settings; the render phase
// reads a copy only, so an edit made while the render phase runs reaches
// it at the next snapshot. Render_settings_frames owns one copy per frame
// in flight; App_context::render_settings (Render_settings_access) resolves
// to the calling role's copy. The DDGI, radiance cascades and ray trace
// renderers fetch their config sections through it at each use.
class Render_settings
{
public:
    void snapshot(const Editor_settings_config& editor_settings, const App_settings& app_settings);

    // Editor_settings_config sections
    Sky_config                 sky                            {};
    Shadow_frustum_fit_config  shadow_frustum_fit             {};
    Selection_outline_style    selection_outline              {};
    Render_style_appearance    render_style_appearance        {};
    Render_style_appearance    selected_render_style_appearance{};
    Debug_visualizations_style debug_visualizations_style     {};
    Ddgi_config                ddgi                           {};
    Radiance_cascades_config   radiance_cascades              {};
    Ray_trace_config           ray_trace                      {};
    Content_edge_lines_config  content_edge_lines             {};
    bool                       exclude_unlit_primitives       {true};
    Indirect_diffuse_source    indirect_diffuse_source        {Indirect_diffuse_source::ambient};
    Culling_classification     culling_classification         {Culling_classification::cpu};
    bool                       lightmap_bicubic_sampling      {true}; // Lightmap_config::bicubic_sampling

    // App_settings::graphics
    Graphics_preset_entry      graphics_preset                {}; // the name string keeps its capacity across snapshots
    bool                       use_draw_lists                 {false}; // App_settings::get_use_draw_lists() (session override applied)
    bool                       headlight_when_unlit           {true};
    bool                       sky_enabled                    {true};
    bool                       shadow_maps_frozen             {false}; // App_settings::shadow_maps_frozen_session
};

// One Render_settings copy per frame in flight, owned by Editor. M2's
// snapshot() fills the copy of the frame being prepared, so the prepare
// phase of the next frame never rewrites the copy the render phase of this
// frame reads. get_for_calling_role() (what App_context::render_settings
// resolves through) returns, on the render role (dedicated render thread),
// the copy of the frame it renders (role-keyed Device::get_frame_index(),
// verified to be the frame that filled it), and on the main role the copy
// of the latest snapshot: M1 and M2 before the snapshot read the previous
// frame's, M2 after it and M3 this frame's, and the inline render phase the
// frame it renders.
class Render_settings_frames
{
public:
    // Sizes one copy per Device frame in flight. Main thread, before the
    // first snapshot.
    void initialize(erhe::graphics::Device& device);

    // Fills the copy of frame_index (the prepare frame, role-keyed
    // Device::get_frame_index() on the main thread) and makes it the
    // latest. Main thread, M2 (or outside any frame: start-up).
    void snapshot(
        uint64_t                      frame_index,
        const Editor_settings_config& editor_settings,
        const App_settings&           app_settings
    );

    [[nodiscard]] auto get_for_calling_role() const -> const Render_settings&;

private:
    class Frame_copy
    {
    public:
        // The frame the copy was last filled for.
        uint64_t        frame_index{std::numeric_limits<uint64_t>::max()};
        Render_settings settings   {};
    };

    erhe::graphics::Device* m_device     {nullptr};
    // Indexed by frame index modulo their count; sized once by initialize().
    std::vector<Frame_copy> m_frames     {};
    // The index into m_frames of the latest snapshot; m_frames.size() until
    // the first snapshot. Main thread only: the render role reads the copy
    // of its frame index.
    std::size_t             m_latest_copy{0};
};

// The per-scene settings and scene facts the render phase reads, resolved
// from the scene's overrides (Scene_settings, doc/editor/scene.md) or the
// editor-global defaults, and copied from the scene, by
// Scene_root::snapshot_render_settings in M2. Scene_root owns one per frame
// in flight, filled for the frame being prepared; the render phase reads its
// frame's copy through Scene_root::get_render_settings(frame_index). The light
// set and the joint poses of the same snapshot are
// Scene_root::get_light_set(frame_index) and
// Scene_root::get_render_joint_poses(frame_index).
class Scene_render_settings
{
public:
    Sky_config                   sky               {};
    Shadow_frustum_fit_config    shadow_frustum_fit{};
    glm::vec4                    clear_color       {0.0f, 0.0f, 0.0f, 0.4f};
    // Scene::get_ambient_light()
    glm::vec3                    ambient_light     {0.0f, 0.0f, 0.0f};
    // The scene has lights in its light layer (else the shadow node lights
    // the view with its headlight) / meshes in its content layer (else the
    // shadow node has nothing to do).
    bool                         has_lights        {false};
    bool                         has_content_meshes{false};
    // The scene's last directional light as the toward-sun direction of the
    // sky (Sky_renderer::resolve_sun_direction); none without one.
    bool                         has_sun_light     {false};
    glm::vec3                    sun_direction     {0.0f, 1.0f, 0.0f};
    // The scene's first camera, which the indirect diffuse producers (DDGI,
    // radiance cascades) fit their light projections to; none without a
    // camera.
    bool                         has_trace_camera  {false};
    erhe::scene::Camera_snapshot trace_camera      {};
};

} // namespace editor
