#pragma once

#include <glm/fwd.hpp>

// Editor-global settings and the per-scene override config structs are generated
// into the global namespace by erhe_codegen.
struct Editor_settings_config;
struct Sky_config;
struct Grid_config;
struct Physics_config;
struct Shadow_frustum_fit_config;
struct Viewport_config_data;
struct Camera_controls_config;

namespace editor {

class Scene_root;

// Effective per-scene setting resolution (issue #239). Each helper returns the
// scene's override when engaged, otherwise the editor-global default. The returned
// reference is valid for as long as both the editor settings and the scene root
// outlive the call.
[[nodiscard]] auto get_effective_sky               (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Sky_config&;
[[nodiscard]] auto get_effective_grid              (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Grid_config&;
[[nodiscard]] auto get_effective_physics           (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Physics_config&;
[[nodiscard]] auto get_effective_shadow_frustum_fit(const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Shadow_frustum_fit_config&;
[[nodiscard]] auto get_effective_viewport          (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Viewport_config_data&;
[[nodiscard]] auto get_effective_camera_controls   (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const Camera_controls_config&;
[[nodiscard]] auto get_effective_clear_color       (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> const glm::vec4&;
[[nodiscard]] auto get_effective_post_processing   (const Editor_settings_config& editor_settings, const Scene_root& scene_root) -> bool;

} // namespace editor
