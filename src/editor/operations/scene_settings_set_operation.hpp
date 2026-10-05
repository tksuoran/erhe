#pragma once

#include "operations/operation.hpp"

#include "scene/generated/scene_settings.hpp"

#include <memory>

namespace editor {

class Scene_root;

// Replaces a scene's per-scene setting overrides (the codegen
// Scene_settings, MCP set_scene_settings) with before / after copies.
// Execute and undo assign the struct and then notify, directly, each
// consumer that keeps derived state from a field that changed
// (doc/editor/operations.md "Scene_settings_set_operation"):
//
// - camera_controls: Fly_camera_tool re-adopts the controls when the scene
//   is the hovered one.
// - lightmap_tile_overrides: Lightmap_window re-prepares a live partition
//   (as Lightmap_tile_overrides_operation does).
//
// The other override fields (sky, grid, physics, shadow_frustum_fit) are
// read through scene_settings_resolve.hpp where they are used, and
// clear_color / post_processing have no reader, so nothing caches them.
// scene_id and variant_selections are managed by the scene (side-data
// identity, Variant_select_operation): before and after must agree on them
// (verified), since no consumer here could follow a change, and execute /
// undo keep their live values (scene_id is assigned lazily, possibly after
// the copies were taken).
class Scene_settings_set_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<Scene_root> scene_root;
        Scene_settings              before;
        Scene_settings              after;
    };

    explicit Scene_settings_set_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const Scene_settings& settings);

    Parameters m_parameters;
    bool       m_camera_controls_changed        {false};
    bool       m_lightmap_tile_overrides_changed{false};
};

} // namespace editor
