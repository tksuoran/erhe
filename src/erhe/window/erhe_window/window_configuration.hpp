#pragma once

#include <glm/glm.hpp>

#include <string>

namespace erhe::window {

class Context_widdow;

class Window_configuration
{
public:
    bool            use_depth               {false};
    bool            use_stencil             {false};
    int             msaa_sample_count       {0};
    int             swap_interval           {1};

    bool            show                    {true};
    bool            fullscreen              {false};
    bool            framebuffer_transparency{false};
    bool            enable_joystick         {true};
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    bool            use_finish              {false};
    int             gl_major                {4};
    int             gl_minor                {6};
    Context_window* share                   {nullptr};
#endif
    glm::ivec2      size                    {1920, 1080};
    std::string     title                   {};
    int             initial_clear           {3};
};

} // namespace erhe::window
