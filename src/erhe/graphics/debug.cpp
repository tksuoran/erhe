#include "erhe/graphics/debug.hpp"

#include "erhe/gl/gl.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/log/log_fmt.hpp"

#include <fmt/ostream.h>
#include <gsl/gsl>

#include <sstream>
#include <thread>

namespace erhe::graphics
{

Scoped_debug_group::Scoped_debug_group(
    const std::string_view debug_label
)
{
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(debug_label.length()),
        debug_label.data()
    );
}

Scoped_debug_group::~Scoped_debug_group()
{
    gl::pop_debug_group();
}

std::mutex              Gpu_timer::s_mutex;
std::vector<Gpu_timer*> Gpu_timer::s_all_gpu_timers;
Gpu_timer*              Gpu_timer::s_active_timer{nullptr};
size_t                  Gpu_timer::s_index       {0};

void Gpu_timer::on_thread_enter()
{
    const std::lock_guard lock{s_mutex};

    // Workaround for https://github.com/fmtlib/fmt/issues/2897
    std::stringstream this_thread_id;
    this_thread_id << std::this_thread::get_id();
    const std::string this_thread_id_string = this_thread_id.str();
    for (auto* gpu_timer : s_all_gpu_timers)
    {
        std::stringstream owner;
        owner << gpu_timer->m_owner_thread;
        log_threads->trace(
            "{}: on thread enter: Gpu_timer @ {} owned by thread {}",
            this_thread_id_string,
            fmt::ptr(gpu_timer),
            owner.str()
        );
        if (gpu_timer->m_owner_thread == std::thread::id{})
        {
            gpu_timer->create();
        }
    }
}

void Gpu_timer::on_thread_exit()
{
    const std::lock_guard lock{s_mutex};

    auto this_thread_id = std::this_thread::get_id();

    // Workaround for https://github.com/fmtlib/fmt/issues/2897
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    const std::string this_thread_id_string = this_thread_id_ss.str();
    for (auto* gpu_timer : s_all_gpu_timers)
    {
        std::stringstream owner;
        owner << gpu_timer->m_owner_thread;
        log_threads->trace(
            "{}: on thread exit: Gpu_timer @ {} owned by thread {}",
            this_thread_id_string,
            //std::this_thread::get_id(),
            fmt::ptr(gpu_timer),
            //gpu_timer->m_owner_thread
            owner.str()
        );
        if (gpu_timer->m_owner_thread == this_thread_id)
        {
            gpu_timer->reset();
        }
    }
}

Gpu_timer::Gpu_timer(const char* label)
    : m_label{label}
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_all_gpu_timers.push_back(this);

    create();
}

Gpu_timer::~Gpu_timer()
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_all_gpu_timers.erase(
        std::remove(
            s_all_gpu_timers.begin(),
            s_all_gpu_timers.end(),
            this
        ),
        s_all_gpu_timers.end()
    );
}

void Gpu_timer::create()
{
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    //log_threads.trace("{}: create @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    log_threads->trace(
        "{}: create @ {}",
        this_thread_id_ss.str(),
        fmt::ptr(this)
    );

    for (auto& query : m_queries)
    {
        if (query.query_object.has_value())
        {
            Expects(m_owner_thread == std::this_thread::get_id());
        }
        else
        {
            query.query_object.emplace(
                Gl_query{gl::Query_target::time_elapsed}
            );
        }
    }
    m_owner_thread = std::this_thread::get_id();
}

void Gpu_timer::reset()
{
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    //log_threads.trace("{}: reset @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    log_threads->trace(
        "{}: reset @ {}",
        this_thread_id_ss.str(),
        fmt::ptr(this)
    );
    m_owner_thread = {};
    for (auto& query : m_queries)
    {
        query.query_object.reset();
    }
}

void Gpu_timer::begin()
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    Expects(m_owner_thread == std::this_thread::get_id());
    Expects(s_active_timer == nullptr);

    auto& query = m_queries[s_index % s_count];

    gl::begin_query(
        gl::Query_target::time_elapsed,
        query.query_object.value().gl_name()
    );

    query.begin    = true;
    s_active_timer = this;
}

void Gpu_timer::end()
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    Expects(m_owner_thread == std::this_thread::get_id());
    Expects(s_active_timer == this);

    auto& query = m_queries[s_index % s_count];

    gl::end_query(gl::Query_target::time_elapsed);

    query.end      = true;
    query.pending  = true;
    s_active_timer = nullptr;
}

void Gpu_timer::read()
{
    Expects(m_owner_thread == std::this_thread::get_id());

    for (size_t i = 0; i <= s_count; ++i)
    {
        const auto poll_index = (s_index + 1 + i) % s_count;
        auto&      query      = m_queries[poll_index];
        const auto name       = query.query_object.value().gl_name();
        if (!query.pending)
        {
            continue;
        }
        GLint result_available{};
        gl::get_query_object_iv(
            name,
            gl::Query_object_parameter_name::query_result_available,
            &result_available
        );
        if (result_available != 0)
        {
            GLuint64 time_value{};
            gl::get_query_object_ui_64v(
                name,
                gl::Query_object_parameter_name::query_result,
                &time_value
            );
            m_last_result = time_value;
            query.pending = false;
        }
    }
    for (size_t i = 0; i < s_count; ++i)
    {
        const auto poll_index = (s_index + 1 + i) % s_count;
        auto&      query      = m_queries[poll_index];
        query.begin = false;
        query.end   = false;
    }
}

auto Gpu_timer::last_result() const -> uint64_t
{
    return m_last_result;
}

auto Gpu_timer::label() const -> const char*
{
    return (m_label != nullptr) ? m_label : "(unnamed)";
}

void Gpu_timer::end_frame()
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    for (auto* timer : s_all_gpu_timers)
    {
        if (timer->m_owner_thread == std::this_thread::get_id())
        {
            timer->read();
        }
    }
    ++s_index;
}

auto Gpu_timer::all_gpu_timers() -> std::vector<Gpu_timer*>
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    return s_all_gpu_timers;
}

Scoped_gpu_timer::Scoped_gpu_timer(Gpu_timer& timer)
    : m_timer{timer}
{
    m_timer.begin();
}

Scoped_gpu_timer::~Scoped_gpu_timer()
{
    m_timer.end();
}

} // namespace erhe::graphics
