#pragma once

#include "app_message.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_property/expression.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace erhe {
    class Item_host;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::property {
    class Dependency_property;
}
namespace erhe::scene {
    class Light;
}

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

// Mute / solo: session-only light flags (doc/editor/light_mixer.md). Each
// call invalidates the scene's light sets, so every writer goes through these.
void set_light_muted(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, bool muted);
void set_light_solo (Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, bool solo);
[[nodiscard]] auto is_light_muted(const erhe::scene::Light& light) -> bool;
[[nodiscard]] auto is_light_solo (const erhe::scene::Light& light) -> bool;

// One table row per light of the active scene (doc/editor/light_mixer.md).
class Light_mixer_window : public erhe::imgui::Imgui_window
{
public:
    Light_mixer_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void row_imgui(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Light>& light, bool any_solo);
    void float_slider(const std::shared_ptr<erhe::scene::Light>& light, const erhe::property::Dependency_property& property, bool degrees);
    void color_button(const std::shared_ptr<erhe::scene::Light>& light);
    void track_edit  (const std::shared_ptr<erhe::scene::Light>& light, const erhe::property::Dependency_property& property, bool changed);
    void begin_edit  (const std::shared_ptr<erhe::scene::Light>& light, const erhe::property::Dependency_property& property);
    void end_edit    ();
    void on_close_scene  (const erhe::Item_host* closing_host);
    void on_items_removed(const Removed_items& removed);

    App_context& m_context;

    // Per draw scratch, cleared after each draw so no light outlives it.
    std::vector<std::shared_ptr<erhe::scene::Light>> m_lights;

    // Drag session: the before state captured at activation, committed as
    // one Property_set_operation when the widget deactivates.
    std::shared_ptr<erhe::scene::Light>        m_edit_light;
    const erhe::property::Dependency_property* m_edit_property{nullptr};
    std::optional<erhe::property::Local_state> m_edit_before;

    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
};

} // namespace editor
