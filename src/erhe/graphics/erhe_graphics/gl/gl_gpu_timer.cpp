#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/gl/gl_gpu_timer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>
#include <thread>

// Comment this out disable timer queries
#define ERHE_USE_TIME_QUERY 1

namespace gl {

extern std::shared_ptr<spdlog::logger> log_gl;

}

namespace erhe::graphics {

ERHE_PROFILE_MUTEX(std::mutex, Gpu_timer_impl::s_mutex);
std::vector<Gpu_timer_impl*>   Gpu_timer_impl::s_all_gpu_timers;
Gpu_timer_impl*                Gpu_timer_impl::s_active_timer{nullptr};
size_t                         Gpu_timer_impl::s_index       {0};

void Gpu_timer_impl::on_thread_enter()
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard lock{s_mutex};

    // Workaround for https://github.com/fmtlib/fmt/issues/2897
    std::stringstream this_thread_id;
    this_thread_id << std::this_thread::get_id();
    const std::string this_thread_id_string = this_thread_id.str();
    for (auto* gpu_timer : s_all_gpu_timers) {
        std::stringstream owner;
        owner << gpu_timer->m_owner_thread;
        log_threads->trace(
            "{}: on thread enter: Gpu_timer_impl @ {} owned by thread {}",
            this_thread_id_string,
            fmt::ptr(gpu_timer),
            owner.str()
        );
        if (gpu_timer->m_owner_thread == std::thread::id{}) {
            gpu_timer->create();
        }
    }
#endif
}

void Gpu_timer_impl::on_thread_exit()
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard lock{s_mutex};

    auto this_thread_id = std::this_thread::get_id();

    // Workaround for https://github.com/fmtlib/fmt/issues/2897
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    const std::string this_thread_id_string = this_thread_id_ss.str();
    for (auto* gpu_timer : s_all_gpu_timers) {
        std::stringstream owner;
        owner << gpu_timer->m_owner_thread;
        log_threads->trace(
            "{}: on thread exit: Gpu_timer_impl @ {} owned by thread {}",
            this_thread_id_string,
            //std::this_thread::get_id(),
            fmt::ptr(gpu_timer),
            //gpu_timer->m_owner_thread
            owner.str()
        );
        if (gpu_timer->m_owner_thread == this_thread_id) {
            gpu_timer->reset();
        }
    }
#endif
}

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_device{device}
    , m_label {label}
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_all_gpu_timers.push_back(this);

    create();
#endif
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    s_all_gpu_timers.erase(
        std::remove(
            s_all_gpu_timers.begin(),
            s_all_gpu_timers.end(),
            this
        ),
        s_all_gpu_timers.end()
    );
#endif
}

void Gpu_timer_impl::create()
{
#if defined(ERHE_USE_TIME_QUERY)
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    log_threads->trace("{}: create @ {}", this_thread_id_ss.str(), fmt::ptr(this));

    for (auto& query : m_queries) {
        if (query.query_object.has_value()) {
            ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
        } else {
            query.query_object.emplace(
                Gl_query{gl::Query_target::time_elapsed}
            );
        }
    }
#endif
    m_owner_thread = std::this_thread::get_id();
}

void Gpu_timer_impl::reset()
{
#if defined(ERHE_USE_TIME_QUERY)
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    log_threads->trace("{}: reset @ {}", this_thread_id_ss.str(), fmt::ptr(this));
    m_owner_thread = {};
    for (auto& query : m_queries) {
        query.query_object.reset();
    }
#endif
}

void Gpu_timer_impl::begin()
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
    ERHE_VERIFY(s_active_timer == nullptr);

    auto& query = m_queries[s_index % s_count];

    gl::begin_query(gl::Query_target::time_elapsed, query.query_object.value().gl_name());

    query.begin    = true;
    s_active_timer = this;
#endif
}

void Gpu_timer_impl::end()
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
    ERHE_VERIFY(s_active_timer == this);

    auto& query = m_queries[s_index % s_count];

    gl::end_query(gl::Query_target::time_elapsed);

    query.end      = true;
    query.pending  = true;
    s_active_timer = nullptr;
#endif
}

void Gpu_timer_impl::read()
{
#if defined(ERHE_USE_TIME_QUERY)
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());

    for (size_t i = 0; i <= s_count; ++i) {
        const auto poll_index = (s_index + 1 + i) % s_count;
        auto&      query      = m_queries[poll_index];
        const auto name       = query.query_object.value().gl_name();
        if (!query.pending) {
            continue;
        }
        GLint result_available{};
        gl::get_query_object_iv(name, gl::Query_object_parameter_name::query_result_available, &result_available);
        if (result_available != 0) {
            GLuint64 time_value{};
            gl::get_query_object_ui_64v(name, gl::Query_object_parameter_name::query_result, &time_value);
            m_last_result = time_value;
            query.pending = false;
        }
    }
    for (size_t i = 0; i < s_count; ++i) {
        const auto poll_index = (s_index + 1 + i) % s_count;
        auto&      query      = m_queries[poll_index];
        query.begin = false;
        query.end   = false;
    }
#endif
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    uint64_t result = m_last_result;
    m_last_result = 0;
    return result;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return (m_label != nullptr) ? m_label : "(unnamed)";
}

void Gpu_timer_impl::end_frame()
{
#if defined(ERHE_USE_TIME_QUERY)
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    for (auto* timer : s_all_gpu_timers) {
        if (timer->m_owner_thread == std::this_thread::get_id()) {
            timer->read();
        }
    }
    ++s_index;
#endif
}

auto Gpu_timer_impl::all_gpu_timers() -> std::vector<Gpu_timer_impl*>
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{s_mutex};

    return s_all_gpu_timers;
}

} // namespace erhe::graphics
