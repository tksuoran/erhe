#pragma once

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/components/component.hpp"

#include "imgui.h"


namespace editor {

class Application;

class Imgui_demo
    : public erhe::components::Component
    , public erhe::toolkit::View
{
public:
    static constexpr const char* c_name = "Imgui_demo";
    Imgui_demo();
    virtual ~Imgui_demo() = default;

    // Implements Componets
    void connect() override;
    void initialize_component() override;

    void disconnect();

    void on_load();

    void update() override;

    void on_enter() override;

private:
    std::shared_ptr<Application>                          m_application;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    ImGuiContext*                                         m_imgui_context{nullptr};
    bool                                                  m_show_demo_window   {true};
    bool                                                  m_show_another_window{true};
    ImVec4                                                m_clear_color{0.45f, 0.55f, 0.60f, 1.00f};
};

}
