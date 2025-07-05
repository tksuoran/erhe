#pragma once

#include "erhe_commands/command.hpp"

namespace erhe::imgui { class Imgui_host; };

namespace editor {

class App_context;

class Imgui_builtin_windows
{
public:
    bool demo         {false};
    bool metrics      {false};
    bool debug_log    {false};
    bool id_stack_tool{false};
    bool about        {false};
    bool style_editor {false};
    bool user_guide   {false};
};

class App_windows
{
public:
    App_windows(App_context& context, erhe::commands::Commands& commands);

    void viewport_menu(erhe::imgui::Imgui_host& imgui_host);

private:
    void builtin_imgui_window_menu();

    void renderdoc_capture();

    App_context&                   m_context;
    Imgui_builtin_windows          m_imgui_builtin_windows;
    erhe::commands::Lambda_command m_renderdoc_capture_command;
};

}
