#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

namespace erhe::geometry {
    class Geometry;
}

namespace editor {

using Geometry_generator = std::function<std::shared_ptr<erhe::geometry::Geometry>()>;

// The geometry state of one brush (doc/plans/deferred_brush_geometry.md R2).
// `ready` means the Geometry is built, Geometry::process() has run and the
// owner's facet statistics are filled; `failed` means the generator produced
// no usable geometry and every consumer gets a null geometry from now on.
enum class Brush_geometry_state : unsigned int
{
    unprepared = 0,
    queued,
    preparing,
    ready,
    failed
};

[[nodiscard]] auto to_string(Brush_geometry_state state) -> std::string_view;

// What a tier 2 request (Brush_geometry_slot::request) did, so that the
// preparation queue can act on it. The queue itself arrives in a later phase;
// this is the seam it plugs into.
enum class Brush_geometry_request_outcome : unsigned int
{
    newly_queued,   // unprepared -> queued: the queue takes the brush
    already_queued, // already queued: the queue moves the brush to the front
    not_applicable  // preparing, ready or failed: nothing for the queue to do
};

// What a preparation task found when it reached the brush
// (Brush_geometry_slot::prepare_if_queued): a brush that the main thread or
// another task has already taken is left alone, so every brush is prepared
// exactly once whichever side gets there first (D4).
enum class Brush_geometry_worker_outcome : unsigned int
{
    prepared, // the slot was `queued`: this call ran the generator
    skipped   // the slot was unprepared, preparing, ready or failed
};

// The geometry of a brush plus the state machine that prepares it exactly
// once, whichever thread gets there first (D3, R8). Holds the one mutex and
// the one condition variable of its owning brush; the owner delegates
// get_geometry() / request_geometry() / get_geometry_state() to it and uses
// the prepared callback to fill state that is derived from the geometry
// (the facet statistics), under that same mutex.
//
// This class deliberately knows nothing about the editor beyond the brush
// logger, so that the state machine is testable without an App_context.
class Brush_geometry_slot final
{
public:
    // A slot created with a finished geometry (the floor brush, an imported or
    // a user-created brush) starts `ready` and ignores the generator; a slot
    // created with only a generator starts `unprepared`.
    Brush_geometry_slot(const std::shared_ptr<erhe::geometry::Geometry>& geometry, Geometry_generator generator);

    Brush_geometry_slot           (const Brush_geometry_slot&) = delete;
    Brush_geometry_slot& operator=(const Brush_geometry_slot&) = delete;
    Brush_geometry_slot           (Brush_geometry_slot&&)      = delete;
    Brush_geometry_slot& operator=(Brush_geometry_slot&&)      = delete;

    // Called under the slot mutex, right after the geometry has been stored
    // and before the state becomes `ready`. Set once, by the owner, before
    // the slot can be reached from another thread.
    void set_prepared_callback(std::function<void(const erhe::geometry::Geometry&)> callback);

    [[nodiscard]] auto get_state() const -> Brush_geometry_state;

    // Tier 1 (R3): returns the geometry once it is `ready`, preparing it on
    // the calling thread when the slot is `unprepared` or `queued` and waiting
    // on the condition variable when another thread is already `preparing`.
    // Returns null when the slot is `failed`; `name` names the brush in the
    // log line that reports the failure.
    [[nodiscard]] auto get_geometry(std::string_view name) -> std::shared_ptr<erhe::geometry::Geometry>;

    // Tier 2 (R3): asks for preparation and returns at once.
    auto request() -> Brush_geometry_request_outcome;

    // The preparation queue's worker entry point (D4): prepares the geometry
    // only while the slot is still `queued` and skips it in every other state,
    // so a brush a tier 1 consumer has already taken is left alone. `name`
    // names the brush in the failure log line; it is a copy the requesting
    // thread made, not the brush's own string.
    auto prepare_if_queued(std::string_view name) -> Brush_geometry_worker_outcome;

    // The geometry only if it is already `ready`; never prepares, never waits.
    [[nodiscard]] auto get_geometry_if_ready() const -> std::shared_ptr<erhe::geometry::Geometry>;

    // The generator, for an owner that hands its recipe to a copy of itself.
    [[nodiscard]] auto get_generator() const -> Geometry_generator;

private:
    // Runs the generator with `lock` released and stores the result under it.
    void prepare_locked(std::unique_lock<std::mutex>& lock, std::string_view name);

    mutable std::mutex                                 m_mutex;
    std::condition_variable                            m_condition_variable;
    Brush_geometry_state                               m_state{Brush_geometry_state::unprepared};
    std::shared_ptr<erhe::geometry::Geometry>          m_geometry;
    Geometry_generator                                 m_generator;
    std::function<void(const erhe::geometry::Geometry&)> m_prepared_callback;
};

}
