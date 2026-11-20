#include "background_apply_meter.hpp"

#include "config/generated/threading_config.hpp"
#include "editor_log.hpp"

#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <cstring>
#include <iterator>

namespace editor {

namespace {

[[nodiscard]] auto to_ms(const std::chrono::steady_clock::duration duration) -> double
{
    return std::chrono::duration<double, std::milli>{duration}.count();
}

} // anonymous namespace

Background_apply_meter::Scope::Scope(Background_apply_meter& meter, const char* const owner)
    : m_meter{meter}
    , m_owner{owner}
    , m_begin{std::chrono::steady_clock::now()}
{
    erhe::utility::Thread_roles::assert_main_thread();
}

Background_apply_meter::Scope::~Scope() noexcept
{
    m_meter.add(m_owner, std::chrono::steady_clock::now() - m_begin);
}

Background_apply_meter::Background_apply_meter(const Threading_config& config)
    : m_config{config}
{
}

void Background_apply_meter::add(const char* const owner, const std::chrono::steady_clock::duration duration)
{
    m_total += duration;
    for (std::size_t i = 0; i < m_entry_count; ++i) {
        Entry& entry = m_entries[i];
        if ((entry.owner == owner) || (std::strcmp(entry.owner, owner) == 0)) {
            entry.duration += duration;
            return;
        }
    }
    ERHE_VERIFY(m_entry_count < c_max_owners);
    m_entries[m_entry_count] = Entry{.owner = owner, .duration = duration};
    ++m_entry_count;
}

void Background_apply_meter::end_frame()
{
    erhe::utility::Thread_roles::assert_main_thread();
    const double total_ms = to_ms(m_total);
    const double limit_ms = static_cast<double>(m_config.background_apply_soft_limit_ms);
    if ((m_entry_count > 0) && (total_ms > limit_ms)) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (!m_last_warning_time.has_value() || ((now - m_last_warning_time.value()) >= std::chrono::seconds{1})) {
            m_last_warning_time = now;
            // Allocates; runs at most once per second and only over the limit.
            fmt::memory_buffer owners;
            for (std::size_t i = 0; i < m_entry_count; ++i) {
                fmt::format_to(std::back_inserter(owners), "{}{} {:.2f} ms", (i == 0) ? "" : ", ", m_entries[i].owner, to_ms(m_entries[i].duration));
            }
            log_frame->warn(
                "Background apply took {:.2f} ms on the main thread this frame (soft limit {:.2f} ms): {}",
                total_ms,
                limit_ms,
                fmt::to_string(owners)
            );
        }
    }
    m_entry_count = 0;
    m_total       = std::chrono::steady_clock::duration{};
}

} // namespace editor
