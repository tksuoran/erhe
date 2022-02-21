#include "application.hpp"
#include "editor_view.hpp"
#include "window.hpp"

namespace editor {

Application::Application()
    : erhe::components::Component{c_name}
{
}

void Application::run()
{
    auto view = get<Editor_view>();
    view->on_enter();
    view->run();
    //get<Window>()->get_context_window()->enter_event_loop();
}

Application::~Application()
{
    m_components.cleanup_components();
}

}
