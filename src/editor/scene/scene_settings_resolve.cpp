#include "scene/scene_settings_resolve.hpp"

#include "scene/scene_root.hpp"

// Full definition of the editor-global settings struct. The per-scene override
// config structs (Sky_config, Grid_config, ...) come in transitively via
// scene_root.hpp -> scene/generated/scene_settings.hpp.
#include "config/generated/editor_settings_config.hpp"

namespace editor {

auto get_effective_sky(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Sky_config&
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.sky.has_value() ? overrides.sky.value() : editor_settings.sky;
}

auto get_effective_grid(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Grid_config&
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.grid.has_value() ? overrides.grid.value() : editor_settings.grid;
}

auto get_effective_physics(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Physics_config&
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.physics.has_value() ? overrides.physics.value() : editor_settings.physics;
}

auto get_effective_shadow_frustum_fit(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Shadow_frustum_fit_config&
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.shadow_frustum_fit.has_value() ? overrides.shadow_frustum_fit.value() : editor_settings.shadow_frustum_fit;
}

auto get_effective_camera_controls(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Camera_controls_config&
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.camera_controls.has_value() ? overrides.camera_controls.value() : editor_settings.camera_controls;
}

auto get_effective_clear_color(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const glm::vec4&
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.clear_color.has_value() ? overrides.clear_color.value() : editor_settings.clear_color;
}

auto get_effective_post_processing(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> bool
{
    const Scene_settings& overrides = scene_root.get_scene_settings();
    return overrides.post_processing.has_value() ? overrides.post_processing.value() : editor_settings.post_processing;
}

} // namespace editor
