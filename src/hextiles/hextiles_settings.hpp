#pragma once

#include "erhe/imgui/imgui_renderer.hpp"

#include <string>

namespace erhe::toolkit {
    class Context_window;
}

namespace hextiles {

class Hextiles_settings
{
public:
    Hextiles_settings(erhe::toolkit::Context_window& context_window);

    erhe::imgui::Imgui_settings imgui;
};

}
