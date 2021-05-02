#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

#include "renderdoc_app.h"

namespace editor {

class Programs;
class Textures;
class Imgui_demo;
class Editor;

class Application
    : public erhe::components::Component
    , public std::enable_shared_from_this<Application>
{
public:
    Application(RENDERDOC_API_1_1_2* renderdoc_api = nullptr);

    virtual ~Application();

    //void connect(std::shared_ptr<Menu> menu);

    void connect(std::shared_ptr<Imgui_demo> imgui_demo);

    void connect(std::shared_ptr<Editor> scene_view);

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
    //std::shared_ptr<Menu>                          m_menu;
    std::shared_ptr<Imgui_demo>                    m_imgui_demo;
    std::shared_ptr<Editor>                    m_scene_view;
    RENDERDOC_API_1_1_2*                           m_renderdoc_api;
};

}
