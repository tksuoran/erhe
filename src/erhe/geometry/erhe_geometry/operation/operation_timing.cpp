#include "erhe_geometry/operation/operation_timing.hpp"

#include "erhe_log/log.hpp"

namespace erhe::geometry::operation {

namespace {

thread_local Operation_timing* s_active_collector = nullptr;

} // anonymous namespace

auto Operation_timing::install(Operation_timing* collector) -> Operation_timing*
{
    Operation_timing* previous = s_active_collector;
    s_active_collector = collector;
    return previous;
}

auto Operation_timing::active() -> Operation_timing*
{
    return s_active_collector;
}

void Operation_timing::add(const char* phase, const std::chrono::steady_clock::duration duration)
{
    m_entries.emplace_back(phase, duration);
}

void Operation_timing::clear()
{
    m_entries.clear();
}

auto Operation_timing::get_milliseconds(const std::string_view phase) const -> double
{
    std::chrono::steady_clock::duration sum{};
    for (const std::pair<const char*, std::chrono::steady_clock::duration>& entry : m_entries) {
        if (phase == entry.first) {
            sum += entry.second;
        }
    }
    return std::chrono::duration<double, std::milli>{sum}.count();
}

auto Operation_timing::entries() const -> const std::vector<std::pair<const char*, std::chrono::steady_clock::duration>>&
{
    return m_entries;
}

Scoped_phase_timer::Scoped_phase_timer(const char* phase)
    : m_phase    {phase}
    , m_collector{Operation_timing::active()}
{
    // Phase scopes double as watchdog breadcrumbs: a stall inside a phase is
    // then attributed to that phase instead of whatever set a breadcrumb
    // last. Phases are per-operation coarse, so the mutex lock in
    // set_breadcrumb() is negligible.
    erhe::log::set_breadcrumb(m_phase);
    if (m_collector != nullptr) {
        m_start = std::chrono::steady_clock::now();
    }
}

Scoped_phase_timer::~Scoped_phase_timer()
{
    if (m_collector != nullptr) {
        m_collector->add(m_phase, std::chrono::steady_clock::now() - m_start);
    }
}

} // namespace erhe::geometry::operation
