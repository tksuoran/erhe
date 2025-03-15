#pragma once

#include <string>

namespace erhe::window {

class Context_widdow;

class Window_configuration
{
public:
    bool            show                    {true};
    bool            fullscreen              {false};
    bool            use_finish              {false};
    bool            framebuffer_transparency{false};
    bool            use_depth               {false};
    bool            use_stencil             {false};
    bool            enable_joystick         {true};
    int             gl_major                {4};
    int             gl_minor                {6};
    int             width                   {1920};
    int             height                  {1080};
    int             msaa_sample_count       {0};
    int             swap_interval           {1};
    std::string     title                   {};
    Context_window* share                   {nullptr};
    int             initial_clear           {3};
};

} // namespace erhe::window
