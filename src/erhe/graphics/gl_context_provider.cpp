#include "erhe/graphics/graphics_log.hpp"
#include "erhe/graphics/gl_context_provider.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/window.hpp"

#include <functional>

namespace erhe::graphics {

Gl_context_provider::Gl_context_provider(
    erhe::graphics::Instance& graphics_instance,
    OpenGL_state_tracker&     opengl_state_tracker
)
    : m_graphics_instance   {graphics_instance}
    , m_opengl_state_tracker{opengl_state_tracker}
    , m_main_thread_id      {std::this_thread::get_id()}
{
}

void Gl_context_provider::provide_worker_contexts(
    erhe::toolkit::Context_window* main_window,
    std::function<bool()>          worker_contexts_still_needed_callback
)
{
    ERHE_PROFILE_FUNCTION();

    log_context->info("Starting to provide worked GL contexts");

    ERHE_VERIFY(m_main_thread_id == std::this_thread::get_id());
    m_main_window = main_window;

    {
        ERHE_PROFILE_SCOPE("main_window->clear_current()");

        main_window->clear_current();
    }

    auto max_count = std::min(std::thread::hardware_concurrency(), 8u);
    for (auto end = max_count, i = 0u; i < end; ++i) {
        ERHE_PROFILE_SCOPE("Worker context");

        if (!worker_contexts_still_needed_callback()) {
            ERHE_PROFILE_MESSAGE_LITERAL("No more GL worker thread contexts needed");
            log_context->info("No more GL worker thread contexts needed");
            break;
        }

        ERHE_PROFILE_MESSAGE_LITERAL("Creating another GL worker thread context");

        auto context = std::make_shared<erhe::toolkit::Context_window>(main_window);
        m_contexts.push_back(context);
        context->clear_current();
        Gl_worker_context context_wrapper{
            static_cast<int>(i),
            context.get()
        };
        m_worker_context_pool.enqueue(context_wrapper);
        m_condition_variable.notify_one();
    }

    ERHE_PROFILE_MESSAGE_LITERAL("Done creating GL worker thread contexts");
    log_context->info("Done creating GL worker thread contexts");

    {
        ERHE_PROFILE_SCOPE("main_window->make_current()");
        main_window->make_current();
    }

}

auto Gl_context_provider::acquire_gl_context() -> Gl_worker_context
{
    ERHE_PROFILE_COLOR("acquire_gl_context", 0x444444);

    if (std::this_thread::get_id() == m_main_thread_id) {
        log_context->trace("acquire_gl_context() called from main thread - immediate return");
        return {};
    }

    Gl_worker_context context;
    while (!m_worker_context_pool.try_dequeue(context)) {
        log_context->trace("Waiting for available GL context");
        //ERHE_PROFILE_MESSAGE_LITERAL("Waiting for available GL context");
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition_variable.wait(lock);
    }
    //const std::string text = fmt::format("Got GL context {}", context.id);
    log_context->trace("Got GL context {}", context.id);
    //ERHE_PROFILE_MESSAGE(text.c_str(), text.length());
    //ZoneValue(context.id);
    ERHE_VERIFY(context.context != nullptr);
    context.context->make_current();
    log_context->trace("Made current GL context {}", context.id);
    return context;
}

void Gl_context_provider::release_gl_context(Gl_worker_context context)
{
    ERHE_PROFILE_FUNCTION();

    if (std::this_thread::get_id() == m_main_thread_id) {
        ERHE_VERIFY(context.id == 0);
        ERHE_VERIFY(context.context == nullptr);
        return;
    }

    ERHE_VERIFY(context.context != nullptr);
    log_context->trace("Releasing GL context {}", context.id);
    //const std::string text = fmt::format("Releasing GL context {}", context.id);
    //ERHE_PROFILE_MESSAGE(text.c_str(), text.length());
    //ZoneValue(context.id);
    m_opengl_state_tracker.on_thread_exit();
    context.context->clear_current();
    m_worker_context_pool.enqueue(context);
    m_condition_variable.notify_one();
    log_context->trace("Released GL context {}", context.id);
}

Scoped_gl_context::Scoped_gl_context(Gl_context_provider& context_provider)
    : m_context_provider{context_provider}
    , m_context{context_provider.acquire_gl_context()}
{
}

Scoped_gl_context::~Scoped_gl_context() noexcept
{
    ERHE_PROFILE_FUNCTION();

    m_context_provider.release_gl_context(m_context);
}

} // namespace erhe::graphics
