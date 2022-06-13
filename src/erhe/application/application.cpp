#include "erhe/application/application.hpp"
#include "erhe/application/view.hpp"

namespace erhe::application {

Application::Application()
    : erhe::components::Component{c_label}
{
}

void Application::run()
{
    auto view = get<View>();
    view->on_enter();
    view->run();
}

Application::~Application() noexcept
{
    m_components.cleanup_components();
}

} // namespace erhe::application
