#include "render_thread.hpp"

#include "editor_log.hpp"

#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <chrono>
#include <exception>

namespace editor {

Render_thread::Render_thread(
    const Render_thread_mode                                mode,
    std::function<void(const Frame_packet&, Frame_result&)> render_phase
)
    : m_mode        {mode}
    , m_render_phase{std::move(render_phase)}
{
    ERHE_VERIFY(m_render_phase);
    if (m_mode == Render_thread_mode::inline_main_thread) {
        m_state = State::idle;
        return;
    }

    m_thread = std::thread{[this]() { thread_main(); }};

    // Wait until the thread has published itself as the render role, so
    // every later read of Thread_roles on the main thread happens after
    // that write (the mutex is the happens-before edge).
    std::unique_lock<std::mutex> lock{m_mutex};
    m_done_cv.wait(lock, [this]{ return m_state != State::starting; });
}

Render_thread::~Render_thread() noexcept
{
    stop();
}

auto Render_thread::get_mode() const -> Render_thread_mode
{
    return m_mode;
}

auto Render_thread::take_result(
    const std::chrono::steady_clock::time_point wait_begin,
    const std::chrono::steady_clock::time_point wait_end
) -> Frame_result
{
    ERHE_VERIFY(m_state == State::done);
    m_state = State::idle;
    Frame_result result = m_result;
    result.join_wait_begin = wait_begin;
    result.join_wait_end   = wait_end;
    if (m_mode == Render_thread_mode::dedicated_thread) {
        // The wake latency: from the later of the render phase's end and
        // the start of the wait (a render phase that finished before the
        // main thread came to join adds no latency).
        const std::chrono::steady_clock::time_point latency_begin = std::max(m_done_time, wait_begin);
        result.join_latency_us = std::chrono::duration<double, std::micro>{wait_end - latency_begin}.count();
    }
    return result;
}

auto Render_thread::kick(const Frame_packet& packet) -> std::optional<Frame_result>
{
    ERHE_PROFILE_FUNCTION();
    std::optional<Frame_result> joined{};
    if (m_mode == Render_thread_mode::inline_main_thread) {
        if (m_state == State::done) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            joined = take_result(now, now);
        }
        ERHE_VERIFY(m_state == State::idle);
        m_result = Frame_result{};
        m_state  = State::running;
        m_render_phase(packet, m_result);
        m_state  = State::done;
        return joined;
    }

    {
        std::unique_lock<std::mutex> lock{m_mutex};
        if ((m_state == State::running) || (m_state == State::done)) {
            // The outstanding frame: the render phase must finish it before
            // the next packet replaces m_packet (the queue has depth one).
            const std::chrono::steady_clock::time_point wait_begin = std::chrono::steady_clock::now();
            m_done_cv.wait(lock, [this]{ return m_state == State::done; });
            joined = take_result(wait_begin, std::chrono::steady_clock::now());
        }
        ERHE_VERIFY(m_state == State::idle);
        m_packet    = packet; // fixed-size copy, no allocation
        m_result    = Frame_result{};
        m_state     = State::running;
        m_kick_time = std::chrono::steady_clock::now();
    }
    m_kick_cv.notify_one();
    return joined;
}

auto Render_thread::join() -> Frame_result
{
    ERHE_PROFILE_FUNCTION();
    if (m_mode == Render_thread_mode::inline_main_thread) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        return take_result(now, now);
    }

    std::unique_lock<std::mutex> lock{m_mutex};
    const std::chrono::steady_clock::time_point wait_begin = std::chrono::steady_clock::now();
    m_done_cv.wait(lock, [this]{ return m_state == State::done; });
    return take_result(wait_begin, std::chrono::steady_clock::now());
}

void Render_thread::publish_draw_data(const uint64_t frame_number)
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        ERHE_VERIFY((m_draw_data_newest == 0) || (frame_number > m_draw_data_newest));
        m_draw_data_published[frame_number % s_draw_data_slot_count] = frame_number;
        m_draw_data_newest = frame_number;
    }
    m_draw_data_cv.notify_all();
}

void Render_thread::wait_for_draw_data(const uint64_t frame_number)
{
    ERHE_PROFILE_FUNCTION();
    const std::size_t            slot = frame_number % s_draw_data_slot_count;
    std::unique_lock<std::mutex> lock{m_mutex};
    m_draw_data_cv.wait(
        lock,
        [this, frame_number, slot]{
            return (m_draw_data_published[slot] == frame_number) || (m_draw_data_newest >= frame_number + s_draw_data_slot_count);
        }
    );
    ERHE_VERIFY(m_draw_data_published[slot] == frame_number); // not overwritten by a later frame
}

void Render_thread::stop()
{
    if (m_mode == Render_thread_mode::inline_main_thread) {
        m_state = State::exiting;
        return;
    }

    {
        std::unique_lock<std::mutex> lock{m_mutex};
        if (m_state == State::exiting) {
            return; // already stopped
        }
        // A frame still in flight finishes first; its result is dropped.
        m_done_cv.wait(lock, [this]{ return m_state != State::running; });
        m_state = State::exiting;
    }
    m_kick_cv.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // The packet copy holds strong references (the indirect diffuse scene
    // root, the lightmap atlas); released here, on the main thread, while
    // the editor parts those reach are still alive.
    m_packet = Frame_packet{};
}

void Render_thread::thread_main()
{
    erhe::utility::Thread_roles::set_render_thread(std::this_thread::get_id());
    ERHE_PROFILE_THREAD_NAME("Render thread");
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_state = State::idle;
    }
    m_done_cv.notify_one();

    for (;;) {
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            m_kick_cv.wait(lock, [this]{ return (m_state == State::running) || (m_state == State::exiting); });
            if (m_state == State::exiting) {
                break;
            }
        }

        // m_packet and m_result belong to this thread while the state is
        // running: the main thread touches them only with the state idle
        // (kick() writing the next packet) or done (the join in join() or
        // kick() reading the result).
        m_result.kick_latency_us = std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - m_kick_time}.count();
        try {
            // Drains what this frame's render phase autoreleased (Metal; a
            // no-op elsewhere). See Scoped_transient_object_pool.
            erhe::graphics::Scoped_transient_object_pool object_pool{};
            m_render_phase(m_packet, m_result);
        } catch (const std::exception& e) {
            log_frame->critical("Render thread: exception in render phase of frame {}: {}", m_packet.frame_number, e.what());
            throw;
        } catch (...) {
            log_frame->critical("Render thread: unknown exception in render phase of frame {}", m_packet.frame_number);
            throw;
        }

        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_state     = State::done;
            m_done_time = std::chrono::steady_clock::now();
        }
        m_done_cv.notify_one();
    }
}

} // namespace editor
