#include "brushes/brush_geometry_slot.hpp"

#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using editor::Brush_geometry_request_outcome;
using editor::Brush_geometry_slot;
using editor::Brush_geometry_state;

// One triangle: the smallest geometry with a facet, which is what the slot
// checks to tell `ready` from `failed`.
[[nodiscard]] auto make_triangle_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("triangle");
    GEO::Mesh& mesh = geometry->get_mesh();
    mesh.vertices.set_dimension(3);
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(3);
    const float points[9] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    for (GEO::index_t vertex = 0; vertex < 3; ++vertex) {
        float* p = mesh.vertices.single_precision_point_ptr(vertex);
        p[0] = points[(3 * vertex) + 0];
        p[1] = points[(3 * vertex) + 1];
        p[2] = points[(3 * vertex) + 2];
    }
    mesh.facets.create_triangle(0, 1, 2);
    return geometry;
}

// A mutex / condition variable latch: the generator blocks until the test
// releases it. No sleeps and no atomics are used for synchronization.
class Latch final
{
public:
    void wait()
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_condition_variable.wait(lock, [this]() -> bool { return m_open; });
    }

    void open()
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_open = true;
        }
        m_condition_variable.notify_all();
    }

    void notify_entered()
    {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_entered = true;
        }
        m_condition_variable.notify_all();
    }

    void wait_entered()
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_condition_variable.wait(lock, [this]() -> bool { return m_entered; });
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_condition_variable;
    bool                    m_open   {false};
    bool                    m_entered{false};
};

// Counts generator runs under its own mutex (no atomics).
class Run_counter final
{
public:
    void increment()
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        ++m_count;
    }

    [[nodiscard]] auto get() const -> int
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_count;
    }

private:
    mutable std::mutex m_mutex;
    int                m_count{0};
};

} // anonymous namespace

TEST(Brush_geometry_slot_test, constructed_with_geometry_starts_ready)
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_triangle_geometry();
    Brush_geometry_slot slot{geometry, {}};
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
    EXPECT_EQ(slot.get_geometry_if_ready(), geometry);
    EXPECT_EQ(slot.get_geometry("ready brush"), geometry);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
}

TEST(Brush_geometry_slot_test, constructed_with_generator_starts_unprepared)
{
    Run_counter counter;
    Brush_geometry_slot slot{
        {},
        [&counter]() -> std::shared_ptr<erhe::geometry::Geometry>
        {
            counter.increment();
            return make_triangle_geometry();
        }
    };
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::unprepared);
    EXPECT_EQ(slot.get_geometry_if_ready(), nullptr);
    EXPECT_EQ(counter.get(), 0);

    const std::shared_ptr<erhe::geometry::Geometry> geometry = slot.get_geometry("lazy brush");
    ASSERT_NE(geometry, nullptr);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
    EXPECT_EQ(counter.get(), 1);
}

TEST(Brush_geometry_slot_test, prepared_callback_runs_once_before_ready)
{
    Run_counter callback_counter;
    Brush_geometry_slot slot{
        {},
        []() -> std::shared_ptr<erhe::geometry::Geometry> { return make_triangle_geometry(); }
    };
    slot.set_prepared_callback(
        [&callback_counter](const erhe::geometry::Geometry& geometry) -> void
        {
            EXPECT_EQ(geometry.get_mesh().facets.nb(), 1u);
            callback_counter.increment();
        }
    );
    static_cast<void>(slot.get_geometry("callback brush"));
    static_cast<void>(slot.get_geometry("callback brush"));
    EXPECT_EQ(callback_counter.get(), 1);
}

TEST(Brush_geometry_slot_test, generator_runs_once_under_concurrent_get)
{
    Latch       latch;
    Run_counter counter;
    Brush_geometry_slot slot{
        {},
        [&latch, &counter]() -> std::shared_ptr<erhe::geometry::Geometry>
        {
            counter.increment();
            latch.notify_entered();
            latch.wait();
            return make_triangle_geometry();
        }
    };

    constexpr int thread_count = 8;
    std::vector<std::shared_ptr<erhe::geometry::Geometry>> results;
    results.resize(thread_count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(
            [&slot, &results, i]() -> void
            {
                results[static_cast<std::size_t>(i)] = slot.get_geometry("concurrent brush");
            }
        );
    }

    // One thread is inside the generator; the others either wait on the slot's
    // condition variable or are about to.
    latch.wait_entered();
    latch.open();
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(counter.get(), 1);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
    ASSERT_NE(results[0], nullptr);
    for (int i = 1; i < thread_count; ++i) {
        EXPECT_EQ(results[static_cast<std::size_t>(i)], results[0]);
    }
}

TEST(Brush_geometry_slot_test, second_thread_waits_while_preparing_and_gets_the_same_geometry)
{
    Latch generator_latch;
    Brush_geometry_slot slot{
        {},
        [&generator_latch]() -> std::shared_ptr<erhe::geometry::Geometry>
        {
            generator_latch.notify_entered();
            generator_latch.wait();
            return make_triangle_geometry();
        }
    };

    std::shared_ptr<erhe::geometry::Geometry> first_result;
    std::thread preparing_thread{
        [&slot, &first_result]() -> void
        {
            first_result = slot.get_geometry("preparing brush");
        }
    };

    generator_latch.wait_entered();
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::preparing);
    // A tier 2 request finds nothing to do while the slot is preparing.
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::not_applicable);
    EXPECT_EQ(slot.get_geometry_if_ready(), nullptr);

    std::shared_ptr<erhe::geometry::Geometry> second_result;
    std::thread waiting_thread{
        [&slot, &second_result]() -> void
        {
            second_result = slot.get_geometry("preparing brush");
        }
    };

    generator_latch.open();
    preparing_thread.join();
    waiting_thread.join();

    ASSERT_NE(first_result, nullptr);
    EXPECT_EQ(second_result, first_result);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
}

TEST(Brush_geometry_slot_test, null_generator_result_is_failed)
{
    Brush_geometry_slot slot{
        {},
        []() -> std::shared_ptr<erhe::geometry::Geometry> { return {}; }
    };
    EXPECT_EQ(slot.get_geometry("null brush"), nullptr);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::failed);
    // Every later caller gets the same answer, and the generator is not rerun.
    EXPECT_EQ(slot.get_geometry("null brush"), nullptr);
    EXPECT_EQ(slot.get_geometry_if_ready(), nullptr);
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::not_applicable);
}

TEST(Brush_geometry_slot_test, zero_facet_geometry_is_failed)
{
    Run_counter counter;
    Brush_geometry_slot slot{
        {},
        [&counter]() -> std::shared_ptr<erhe::geometry::Geometry>
        {
            counter.increment();
            return std::make_shared<erhe::geometry::Geometry>("empty");
        }
    };
    EXPECT_EQ(slot.get_geometry("empty brush"), nullptr);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::failed);
    EXPECT_EQ(slot.get_geometry("empty brush"), nullptr);
    EXPECT_EQ(counter.get(), 1);
}

TEST(Brush_geometry_slot_test, failing_generator_is_failed_for_every_waiting_caller)
{
    Latch latch;
    Brush_geometry_slot slot{
        {},
        [&latch]() -> std::shared_ptr<erhe::geometry::Geometry>
        {
            latch.notify_entered();
            latch.wait();
            return {};
        }
    };

    constexpr int thread_count = 4;
    std::vector<std::shared_ptr<erhe::geometry::Geometry>> results;
    results.resize(thread_count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(
            [&slot, &results, i]() -> void
            {
                results[static_cast<std::size_t>(i)] = slot.get_geometry("failing brush");
            }
        );
    }
    latch.wait_entered();
    latch.open();
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(slot.get_state(), Brush_geometry_state::failed);
    for (int i = 0; i < thread_count; ++i) {
        EXPECT_EQ(results[static_cast<std::size_t>(i)], nullptr);
    }
}

TEST(Brush_geometry_slot_test, request_queues_once_and_is_a_no_op_in_other_states)
{
    Brush_geometry_slot slot{
        {},
        []() -> std::shared_ptr<erhe::geometry::Geometry> { return make_triangle_geometry(); }
    };
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::newly_queued);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::queued);
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::already_queued);
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::already_queued);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::queued);

    // Tier 1 prepares a queued brush on the calling thread.
    ASSERT_NE(slot.get_geometry("queued brush"), nullptr);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::not_applicable);
}

TEST(Brush_geometry_slot_test, request_on_a_ready_slot_is_not_applicable)
{
    Brush_geometry_slot slot{make_triangle_geometry(), {}};
    EXPECT_EQ(slot.request(), Brush_geometry_request_outcome::not_applicable);
    EXPECT_EQ(slot.get_state(), Brush_geometry_state::ready);
}

TEST(Brush_geometry_slot_test, state_names)
{
    EXPECT_EQ(editor::to_string(Brush_geometry_state::unprepared), "unprepared");
    EXPECT_EQ(editor::to_string(Brush_geometry_state::queued),     "queued");
    EXPECT_EQ(editor::to_string(Brush_geometry_state::preparing),  "preparing");
    EXPECT_EQ(editor::to_string(Brush_geometry_state::ready),      "ready");
    EXPECT_EQ(editor::to_string(Brush_geometry_state::failed),     "failed");
}
