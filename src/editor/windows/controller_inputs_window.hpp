#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <cstdint>
#include <unordered_map>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
#if defined(ERHE_XR_LIBRARY_OPENXR)
namespace erhe::xr {
    class Xr_action_boolean;
}
#endif

namespace editor {

class App_context;

// Live view of the XR controller input action states (diagnostic window):
// every boolean / float / vector2 / pose action of the right-hand
// controller as reported by OpenXR each frame. Booleans keep a
// state-change counter so intermittent blips (e.g. a capacitive touch
// sensor toggling from a resting thumb) remain visible after the fact.
class Controller_inputs_window : public erhe::imgui::Imgui_window
{
public:
    Controller_inputs_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    App_context& m_context;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    class Boolean_history
    {
    public:
        bool          last_state  {false};
        bool          initialized {false};
        std::uint64_t change_count{0};
    };
    std::unordered_map<const erhe::xr::Xr_action_boolean*, Boolean_history> m_boolean_histories;

    void boolean_row(const char* label, const erhe::xr::Xr_action_boolean* action);
#endif
};

}
