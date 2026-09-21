#include "brushes/brush_geometry_slot.hpp"

#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

namespace editor {

auto to_string(const Brush_geometry_state state) -> std::string_view
{
    switch (state) {
        case Brush_geometry_state::unprepared: return "unprepared";
        case Brush_geometry_state::queued:     return "queued";
        case Brush_geometry_state::preparing:  return "preparing";
        case Brush_geometry_state::ready:      return "ready";
        case Brush_geometry_state::failed:     return "failed";
        default:                               return "?";
    }
}

Brush_geometry_slot::Brush_geometry_slot(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    Geometry_generator                               generator
)
    : m_state    {geometry ? Brush_geometry_state::ready : Brush_geometry_state::unprepared}
    , m_geometry {geometry}
    , m_generator{geometry ? Geometry_generator{} : std::move(generator)}
{
}

void Brush_geometry_slot::set_prepared_callback(std::function<void(const erhe::geometry::Geometry&)> callback)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_prepared_callback = std::move(callback);
}

auto Brush_geometry_slot::get_state() const -> Brush_geometry_state
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_state;
}

auto Brush_geometry_slot::get_geometry_if_ready() const -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return (m_state == Brush_geometry_state::ready) ? m_geometry : std::shared_ptr<erhe::geometry::Geometry>{};
}

auto Brush_geometry_slot::get_generator() const -> Geometry_generator
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_generator;
}

auto Brush_geometry_slot::request() -> Brush_geometry_request_outcome
{
    std::lock_guard<std::mutex> lock{m_mutex};
    switch (m_state) {
        case Brush_geometry_state::unprepared: {
            m_state = Brush_geometry_state::queued;
            return Brush_geometry_request_outcome::newly_queued;
        }
        case Brush_geometry_state::queued: {
            return Brush_geometry_request_outcome::already_queued;
        }
        default: {
            return Brush_geometry_request_outcome::not_applicable;
        }
    }
}

void Brush_geometry_slot::prepare_locked(std::unique_lock<std::mutex>& lock, const std::string_view name)
{
    m_state = Brush_geometry_state::preparing;
    const Geometry_generator generator = m_generator;
    lock.unlock();

    std::shared_ptr<erhe::geometry::Geometry> geometry;
    try {
        if (generator) {
            geometry = generator();
        }
    } catch (...) {
        // The state machine must not be left in `preparing`: every waiter on
        // the condition variable would block forever. Mark the slot `failed`,
        // wake the waiters and let the exception continue to the caller.
        lock.lock();
        m_state     = Brush_geometry_state::failed;
        m_generator = {};
        m_condition_variable.notify_all();
        throw;
    }

    const bool geometry_ok = geometry && (geometry->get_mesh().facets.nb() > 0);

    lock.lock();
    m_generator = {};
    if (geometry_ok) {
        m_geometry = geometry;
        if (m_prepared_callback) {
            m_prepared_callback(*geometry);
        }
        m_state = Brush_geometry_state::ready;
    } else {
        m_geometry.reset();
        m_state = Brush_geometry_state::failed;
        log_brush->warn("Brush '{}': geometry preparation failed - the brush has no geometry", name);
    }
    m_condition_variable.notify_all();
}

auto Brush_geometry_slot::get_geometry(const std::string_view name) -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::unique_lock<std::mutex> lock{m_mutex};
    for (;;) {
        switch (m_state) {
            case Brush_geometry_state::ready: {
                return m_geometry;
            }
            case Brush_geometry_state::failed: {
                return std::shared_ptr<erhe::geometry::Geometry>{};
            }
            case Brush_geometry_state::preparing: {
                m_condition_variable.wait(lock);
                break;
            }
            case Brush_geometry_state::unprepared:
            case Brush_geometry_state::queued:
            default: {
                prepare_locked(lock, name);
                break;
            }
        }
    }
}

}
