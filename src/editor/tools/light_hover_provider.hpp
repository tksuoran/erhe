#pragma once

#include "scene/analytic_hover_provider.hpp"

#include <string>

namespace editor {

class App_context;

// Picks the lights a view visualizes (Debug_visualizations::is_light_visualized)
// by a sphere as large as their ray ring; the hit replaces the content hover,
// so a click selects the light.
class Light_hover_provider : public Analytic_hover_provider
{
public:
    explicit Light_hover_provider(App_context& context);

    // Implements Analytic_hover_provider
    [[nodiscard]] auto get_analytic_hover_name () const -> const std::string& override;
    [[nodiscard]] auto get_analytic_hover_slot () const -> uint32_t override;
    [[nodiscard]] auto get_analytic_hover_merge() const -> Analytic_hover_merge override;
    [[nodiscard]] auto pick_analytic_hover(
        Scene_view& scene_view,
        glm::vec3   ray_origin,
        glm::vec3   ray_direction
    ) -> std::optional<Hover_entry> override;
    void clear_analytic_hover(Scene_view& scene_view) override;

private:
    App_context& m_context;
};

}
