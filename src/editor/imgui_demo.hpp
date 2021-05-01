#ifndef imhui_demo_hpp
#define imhui_demo_hpp

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/components/component.hpp"

#include "imgui.h"


namespace sample {

class Application;

class Imgui_demo
    : public erhe::toolkit::View
    , public erhe::components::Component
{
public:
    Imgui_demo();

    virtual ~Imgui_demo() = default;

    void connect(std::shared_ptr<Application>                          application,
                 std::shared_ptr<erhe::graphics::OpenGL_state_tracker> pipeline_state_tracker);

    void disconnect();

    void initialize_component() override;

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

#endif
