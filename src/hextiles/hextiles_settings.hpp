#pragma once

#include "erhe_imgui/imgui_renderer.hpp"

#include <string>

namespace erhe::window {
    class Context_window;
}

namespace hextiles {

class Hextiles_settings
{
public:
    Hextiles_settings(erhe::window::Context_window& context_window);

    erhe::imgui::Imgui_settings imgui;
};

}
