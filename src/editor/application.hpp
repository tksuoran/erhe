#pragma once

#include "erhe/components/component.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

#include "renderdoc_app.h"

namespace editor {

class Application
    : public erhe::components::Component
    , public std::enable_shared_from_this<Application>
{
public:
    Application(RENDERDOC_API_1_1_2* renderdoc_api = nullptr);

    virtual ~Application();

    void run();

    auto on_load()
    -> bool;

    void component_initialization_complete(bool initialization_succeeded);

    auto get_context_window()
    -> erhe::toolkit::Context_window*
    {
        return m_context_window.get();
    }

    void begin_renderdoc_capture();

    void end_renderdoc_capture();

private:
    auto create_gl_window()
    -> bool;

    auto launch_component_initialization()
    -> bool;

    std::unique_ptr<erhe::toolkit::Context_window> m_context_window;
    erhe::components::Components                   m_components;
    RENDERDOC_API_1_1_2*                           m_renderdoc_api;
};

}
