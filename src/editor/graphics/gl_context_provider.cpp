#include "graphics/gl_context_provider.hpp"
#include "log.hpp"
#include "window.hpp"

#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/toolkit/tracy_client.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/window.hpp"

#include <fmt/ostream.h>

#include <functional>

namespace editor {

using namespace std;

Gl_context_provider::Gl_context_provider()
    : erhe::components::Component{c_name}
    , m_main_thread_id           {std::this_thread::get_id()}
{
}

Gl_context_provider::~Gl_context_provider() = default;

void Gl_context_provider::provide_worker_contexts(
    const shared_ptr<erhe::graphics::OpenGL_state_tracker>& opengl_state_tracker,
    erhe::toolkit::Context_window*                          main_window,
    function<bool()>                                        worker_contexts_still_needed_callback
)
{
    ZoneScoped;

    log_startup.info("Starting to provide worked GL contexts\n");

    VERIFY(m_main_thread_id == this_thread::get_id());
    m_opengl_state_tracker = opengl_state_tracker;
    m_main_window = main_window;

    {
        ZoneScopedN("main_window->clear_current()");

        main_window->clear_current();
    }

    auto max_count = 2u; //thread::hardware_concurrency();
    for (auto end = max_count, i = 0u; i < end; ++i)
    {
        ZoneScopedN("Worker context");

        if (!worker_contexts_still_needed_callback())
        {
            TracyMessageL("No more GL worker thread contexts needed");
            log_startup.info("No more GL worker thread contexts needed\n");
            break;
        }

        TracyMessageL("Creating another GL worker thread context");

        auto context = make_shared<erhe::toolkit::Context_window>(main_window);
        m_contexts.push_back(context);
        context->clear_current();
        Gl_worker_context context_wrapper{
            static_cast<int>(i),
            context.get()
        };
        m_worker_context_pool.enqueue(context_wrapper);
        m_condition_variable.notify_one();
    }

    TracyMessageL("Done creating GL worker thread contexts");
    log_startup.info("Done creating GL worker thread contexts\n");

    {
        ZoneScopedN("main_window->make_current()");
        main_window->make_current();
    }

}

auto Gl_context_provider::acquire_gl_context() -> Gl_worker_context
{
    ZoneScopedC(0x223344);

    if (this_thread::get_id() == m_main_thread_id)
    {
        return {};
    }

    Gl_worker_context context;
    while (!m_worker_context_pool.try_dequeue(context))
    {
        TracyMessageL("Waiting for available GL context");
        unique_lock<mutex> lock(m_mutex);
        m_condition_variable.wait(lock);
    }
    string text = fmt::format("Got GL context {}", context.id);
    TracyMessage(text.c_str(), text.length());
    ZoneValue(context.id);
    VERIFY(context.context != nullptr);
    context.context->make_current();
    return context;
}

void Gl_context_provider::release_gl_context(Gl_worker_context context)
{
    ZoneScoped;

    if (this_thread::get_id() == m_main_thread_id)
    {
        VERIFY(context.id == 0);
        VERIFY(context.context == nullptr);
        return;
    }

    VERIFY(context.context != nullptr);
    string text = fmt::format("Releasing GL context {}", context.id);
    TracyMessage(text.c_str(), text.length());
    ZoneValue(context.id);
    m_opengl_state_tracker->on_thread_exit();
    context.context->clear_current();
    m_worker_context_pool.enqueue(context);
    m_condition_variable.notify_one();
}

Scoped_gl_context::Scoped_gl_context(Gl_context_provider* context_provider)
    : m_context_provider{context_provider}
    , m_context         {context_provider->acquire_gl_context()}
{
}

Scoped_gl_context::~Scoped_gl_context()
{
    ZoneScoped;

    m_context_provider->release_gl_context(m_context);
}


}
