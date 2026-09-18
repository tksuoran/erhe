// Multithreaded worker-context tests: buffers prepared on worker threads
// under Scoped_worker_context and consumed on the main thread. See
// doc/gl_worker_thread_contexts.md and doc/graphics_test_coverage.md.
//
// Backend-neutral by design: on Vulkan / Metal the scope is a no-op and
// these tests validate plain multithreaded buffer creation; on OpenGL they
// exercise the share-context pool, the thread guards and the cross-context
// publication fences. Worker-side GL errors fail the owning test through
// the per-context debug callback -> Gpu_test_environment message list.

#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_worker_context.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Deterministic per-buffer byte pattern, distinct across (thread, index).
void fill_pattern(std::vector<std::uint8_t>& bytes, const unsigned int thread_index, const unsigned int buffer_index)
{
    for (std::size_t i = 0, end = bytes.size(); i < end; ++i) {
        bytes[i] = static_cast<std::uint8_t>((thread_index * 89u) + (buffer_index * 37u) + (i * 13u));
    }
}

// Host-visible, NON-persistent buffer created with initial contents. The
// only worker-legal combination on every backend: on OpenGL a worker may
// not allocate a persistently mapped buffer (mapping is main-context-only),
// and on Vulkan init_data requires host-visible memory.
auto make_worker_buffer(erhe::graphics::Device& device, const std::vector<std::uint8_t>& bytes, const char* debug_label)
    -> std::shared_ptr<erhe::graphics::Buffer>
{
    const erhe::graphics::Buffer_create_info create_info{
        .capacity_byte_count               = bytes.size(),
        .usage                             = erhe::graphics::Buffer_usage::transfer_src | erhe::graphics::Buffer_usage::storage,
        .required_memory_property_bit_mask =
            erhe::graphics::Memory_property_flag_bit_mask::host_read |
            erhe::graphics::Memory_property_flag_bit_mask::host_write,
        .init_data   = bytes.data(),
        .debug_label = erhe::utility::Debug_label{debug_label}
    };
    return std::make_shared<erhe::graphics::Buffer>(device, create_info);
}

} // anonymous namespace

class Worker_context_test : public Gpu_test
{
protected:
    // The pool may legitimately not exist (GL device with a window that
    // cannot produce share contexts). Never fail for environmental reasons.
    void require_worker_contexts()
    {
        if (!device().supports_worker_contexts()) {
            GTEST_SKIP() << "Device has no worker-context support in this configuration";
        }
    }

    // Copy a (possibly worker-created) buffer into a fresh readback buffer
    // on the main thread and return the bytes. The blit copy is a
    // publication consumer, so this is exactly the worker-prepare /
    // main-consume handoff under test.
    auto read_via_blit_copy(erhe::graphics::Buffer& buffer) -> std::vector<std::uint8_t>
    {
        const std::size_t byte_count = buffer.get_capacity_byte_count();
        const std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "worker readback");
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(&buffer, 0, readback.get(), 0, byte_count);
            }
        );
        const std::vector<std::byte> raw = read_buffer(*readback, byte_count);
        std::vector<std::uint8_t> out(byte_count);
        std::memcpy(out.data(), raw.data(), byte_count);
        return out;
    }
};

// T1: one worker thread creates a pre-filled buffer under a worker scope;
// the main thread copies it out and checks every byte.
TEST_F(Worker_context_test, worker_prepared_buffer_round_trip)
{
    require_worker_contexts();

    constexpr std::size_t byte_count = 4096;
    std::vector<std::uint8_t> expected(byte_count);
    fill_pattern(expected, 1, 1);

    std::shared_ptr<erhe::graphics::Buffer> buffer{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            buffer = make_worker_buffer(device(), expected, "T1 worker buffer");
        }
    };
    worker.join();
    ASSERT_TRUE(buffer);

    const std::vector<std::uint8_t> actual = read_via_blit_copy(*buffer);
    ASSERT_EQ(actual, expected);
}

// T2: more worker threads than pool contexts (the GL pool is 4), several
// buffers each, so the blocking pool acquire and context reuse across
// threads are exercised by construction. Every byte of every buffer must
// come back intact on the main thread.
TEST_F(Worker_context_test, worker_pool_contention_stress)
{
    require_worker_contexts();

    constexpr unsigned int thread_count            = 8;
    constexpr unsigned int buffers_per_thread      = 4;
    constexpr std::size_t  byte_count              = 1024;

    std::vector<std::shared_ptr<erhe::graphics::Buffer>> buffers(thread_count * buffers_per_thread);
    std::vector<std::thread> workers{};
    workers.reserve(thread_count);
    for (unsigned int thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back(
            [this, &buffers, thread_index]() {
                for (unsigned int buffer_index = 0; buffer_index < buffers_per_thread; ++buffer_index) {
                    // Scope per buffer: each iteration re-acquires a pool
                    // context, maximizing acquire / release churn.
                    erhe::graphics::Scoped_worker_context worker_context{device()};
                    std::vector<std::uint8_t> bytes(byte_count);
                    fill_pattern(bytes, thread_index, buffer_index);
                    buffers[(thread_index * buffers_per_thread) + buffer_index] =
                        make_worker_buffer(device(), bytes, "T2 worker buffer");
                }
            }
        );
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    for (unsigned int thread_index = 0; thread_index < thread_count; ++thread_index) {
        for (unsigned int buffer_index = 0; buffer_index < buffers_per_thread; ++buffer_index) {
            const std::shared_ptr<erhe::graphics::Buffer>& buffer = buffers[(thread_index * buffers_per_thread) + buffer_index];
            ASSERT_TRUE(buffer);
            std::vector<std::uint8_t> expected(byte_count);
            fill_pattern(expected, thread_index, buffer_index);
            const std::vector<std::uint8_t> actual = read_via_blit_copy(*buffer);
            ASSERT_EQ(actual, expected) << "thread " << thread_index << " buffer " << buffer_index;
        }
    }
}

// T3a: nested scopes on one worker refcount - the inner scope keeps the
// outer context, and work done under it is valid.
TEST_F(Worker_context_test, nested_worker_scope_refcounts)
{
    require_worker_contexts();

    constexpr std::size_t byte_count = 512;
    std::vector<std::uint8_t> expected(byte_count);
    fill_pattern(expected, 3, 7);

    std::shared_ptr<erhe::graphics::Buffer> outer_buffer{};
    std::shared_ptr<erhe::graphics::Buffer> inner_buffer{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context outer{device()};
            outer_buffer = make_worker_buffer(device(), expected, "T3 outer buffer");
            {
                erhe::graphics::Scoped_worker_context inner{device()};
                inner_buffer = make_worker_buffer(device(), expected, "T3 inner buffer");
            }
            // The outer scope's context must still be current after the
            // inner scope released: this creation would fault / assert
            // otherwise.
            outer_buffer = make_worker_buffer(device(), expected, "T3 outer buffer after inner");
        }
    };
    worker.join();
    ASSERT_TRUE(outer_buffer);
    ASSERT_TRUE(inner_buffer);
    ASSERT_EQ(read_via_blit_copy(*outer_buffer), expected);
    ASSERT_EQ(read_via_blit_copy(*inner_buffer), expected);
}

// T3b: on the main thread the scope is a no-op and creation keeps working.
TEST_F(Worker_context_test, main_thread_scope_is_noop)
{
    require_worker_contexts();

    constexpr std::size_t byte_count = 256;
    std::vector<std::uint8_t> expected(byte_count);
    fill_pattern(expected, 5, 5);

    erhe::graphics::Scoped_worker_context scope{device()};
    const std::shared_ptr<erhe::graphics::Buffer> buffer = make_worker_buffer(device(), expected, "T3 main-thread buffer");
    ASSERT_TRUE(buffer);
    ASSERT_EQ(read_via_blit_copy(*buffer), expected);
}

} // namespace erhe::graphics::test
