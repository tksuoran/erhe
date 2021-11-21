#include "application.hpp"
#include "log.hpp"

#include "erhe/scene/transform.hpp"

namespace editor {

using std::shared_ptr;
using View = erhe::toolkit::View;

Application::Application()
    : erhe::components::Component{c_name}
{
}

Application::~Application()
{
    m_components.cleanup_components();
}

}
