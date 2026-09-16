#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>

namespace editor {

class Hover_entry;
class Scene_view;

// A hover source for tools that have no scene meshes and are hit tested
// analytically against the control ray (the transform gizmo). Providers are
// registered in App_context::analytic_hover_providers and consulted by
// Scene_view::update_hover_with_analytic_tools(), which merges each returned
// entry into Hover_entry::tool_slot by ray-t, next to the raytrace and ID
// render sources. Analytic entries carry valid / position / normal and name
// their provider; they carry no mesh.
class Analytic_hover_provider
{
public:
    virtual ~Analytic_hover_provider() noexcept = default;

    // Shown by hover readouts in place of a mesh name.
    [[nodiscard]] virtual auto get_analytic_hover_name() const -> const std::string& = 0;

    // Hit tests the provider against the control ray of scene_view and
    // returns the hover entry, or nullopt when nothing of the provider is
    // under the ray. Runs once per hover update of scene_view, before any
    // hover slot reader of that update, so a provider may keep per-pick state
    // for its own later use (drag start).
    [[nodiscard]] virtual auto pick_analytic_hover(
        Scene_view& scene_view,
        glm::vec3   ray_origin,
        glm::vec3   ray_direction
    ) -> std::optional<Hover_entry> = 0;

    // The hover slots of scene_view were cleared without a pick (pointer left
    // the view, no control ray, ...): drop any per-pick state kept for it.
    virtual void clear_analytic_hover(Scene_view& scene_view) = 0;
};

}
