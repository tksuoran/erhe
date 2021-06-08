#include "application.hpp"
#include "log.hpp"
#include "erhe/toolkit/window.hpp"

#include <openxr/openxr.h>

namespace editor {

using std::shared_ptr;
using View = erhe::toolkit::View;

Application::Application(RENDERDOC_API_1_1_2* renderdoc_api)
    : erhe::components::Component{"Application"}
    , m_renderdoc_api{renderdoc_api}
{
}

Application::~Application()
{
    m_components.cleanup_components();
}

void Application::begin_renderdoc_capture()
{
    if (!m_renderdoc_api)
    {
        return;
    }
    log_renderdoc.trace("RenderDoc: StartFrameCapture()\n");
    m_renderdoc_api->StartFrameCapture(nullptr, nullptr);
}

void Application::end_renderdoc_capture()
{
    if (!m_renderdoc_api)
    {
        return;
    }
    log_renderdoc.trace("RenderDoc: EndFrameCapture()\n");
    m_renderdoc_api->EndFrameCapture(nullptr, nullptr);
    log_renderdoc.trace("RenderDoc: LaunchReplayUI(1, nullptr)\n");
    m_renderdoc_api->LaunchReplayUI(1, nullptr);
}

}
