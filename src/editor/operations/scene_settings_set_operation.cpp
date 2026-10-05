#include "operations/scene_settings_set_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/generated/scene_settings_serialization.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"
#include "windows/lightmap_window.hpp"

#include "erhe_verify/verify.hpp"

#include <nlohmann/json.hpp>

namespace editor {

namespace {

// One field of a serialized Scene_settings; null when the serialization
// leaves it out (a disengaged optional).
[[nodiscard]] auto get_field(const nlohmann::json& settings, const char* const key) -> nlohmann::json
{
    const nlohmann::json::const_iterator i = settings.find(key);
    return (i != settings.end()) ? *i : nlohmann::json{};
}

} // anonymous namespace

Scene_settings_set_operation::Scene_settings_set_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    ERHE_VERIFY(m_parameters.scene_root);
    // The codegen struct has no comparison; its serialization compares
    // field by field. Once, here, not on every execute.
    const nlohmann::json before = nlohmann::json::parse(serialize(m_parameters.before, 0));
    const nlohmann::json after  = nlohmann::json::parse(serialize(m_parameters.after,  0));
    ERHE_VERIFY(get_field(before, "scene_id")           == get_field(after, "scene_id"));
    ERHE_VERIFY(get_field(before, "variant_selections") == get_field(after, "variant_selections"));
    m_camera_controls_changed         = (get_field(before, "camera_controls")         != get_field(after, "camera_controls"));
    m_lightmap_tile_overrides_changed = (get_field(before, "lightmap_tile_overrides") != get_field(after, "lightmap_tile_overrides"));
    set_description(fmt::format("[{}] Scene settings of '{}'", get_serial(), m_parameters.scene_root->get_name()));
}

void Scene_settings_set_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_parameters.after);
}

void Scene_settings_set_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_parameters.before);
}

void Scene_settings_set_operation::apply(App_context& context, const Scene_settings& settings)
{
    Scene_root& scene_root = *m_parameters.scene_root.get();
    // The managed fields stay as they are live: scene_id may have been
    // assigned lazily (Scene_root::get_scene_id, a lightmap manifest) after
    // the before / after copies were taken, and a restored copy would drop
    // it and orphan the saved side data.
    Scene_settings& live = scene_root.get_scene_settings();
    Scene_settings  next = settings;
    next.scene_id           = live.scene_id;
    next.variant_selections = live.variant_selections;
    live = std::move(next);
    if (m_camera_controls_changed && (context.fly_camera_tool != nullptr)) {
        context.fly_camera_tool->on_scene_camera_controls_changed(scene_root);
    }
    if (m_lightmap_tile_overrides_changed && (context.lightmap_window != nullptr)) {
        context.lightmap_window->on_tile_overrides_changed(scene_root);
    }
}

} // namespace editor
