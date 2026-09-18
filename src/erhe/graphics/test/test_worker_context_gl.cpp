// GL-specific worker-context tests: worker-side texture create + upload,
// worker-side blit-encoder writes, and context-index introspection. See
// doc/gl_worker_thread_contexts.md and doc/graphics_test_coverage.md.
//
// This file is added to the target only on the OpenGL backend (CMake); it
// reaches GL internals through the public Device::get_impl() and the
// erhe_graphics/gl headers. The uploads run on worker share contexts, so
// these are the only exercisers of the texture-storage, texture-upload,
// mipmap and fill publication points.

#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/scoped_container_access.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_gl/wrapper_functions.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int texture_width  = 8;
constexpr int texture_height = 8;
constexpr std::size_t bytes_per_row  = static_cast<std::size_t>(texture_width) * 4u;
constexpr std::size_t texture_bytes  = bytes_per_row * static_cast<std::size_t>(texture_height);

void fill_pixel_pattern(std::vector<std::uint8_t>& bytes)
{
    for (std::size_t i = 0, end = bytes.size(); i < end; ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 7u) + 3u);
    }
}

} // anonymous namespace

class Worker_context_gl_test : public Gpu_test
{
protected:
    void require_worker_contexts()
    {
        if (!device().supports_worker_contexts()) {
            GTEST_SKIP() << "Device has no worker-context support in this configuration";
        }
    }

    // Staging buffer created with initial pixel bytes - worker-legal
    // (host-visible, non-persistent, init_data at storage time; workers may
    // not map).
    auto make_staging_buffer(const std::vector<std::uint8_t>& bytes, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Buffer>
    {
        const erhe::graphics::Buffer_create_info create_info{
            .capacity_byte_count               = bytes.size(),
            .usage                             = erhe::graphics::Buffer_usage::transfer_src,
            .required_memory_property_bit_mask =
                erhe::graphics::Memory_property_flag_bit_mask::host_read |
                erhe::graphics::Memory_property_flag_bit_mask::host_write,
            .init_data   = bytes.data(),
            .debug_label = erhe::utility::Debug_label{debug_label}
        };
        return std::make_shared<erhe::graphics::Buffer>(device(), create_info);
    }

    auto make_worker_texture(const int level_count, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Texture>
    {
        const erhe::graphics::Texture_create_info create_info{
            .device      = device(),
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled          |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src     |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .width       = texture_width,
            .height      = texture_height,
            .level_count = level_count,
            .debug_label = erhe::utility::Debug_label{debug_label}
        };
        return std::make_shared<erhe::graphics::Texture>(device(), create_info);
    }
};

// The thread-local context index is the permission model: -1 off-scope on a
// worker thread, 0 on main, 1..pool inside a worker scope, and stable
// across a nested scope.
TEST_F(Worker_context_gl_test, context_index_tracks_scopes)
{
    require_worker_contexts();

    ASSERT_EQ(get_gl_context_index(), 0) << "main thread must be context 0";

    int index_before        = -2;
    int index_outer         = -2;
    int index_inner         = -2;
    int index_after_inner   = -2;
    int index_after_release = -2;
    std::thread worker{
        [&]() {
            index_before = get_gl_context_index();
            {
                erhe::graphics::Scoped_worker_context outer{device()};
                index_outer = get_gl_context_index();
                {
                    erhe::graphics::Scoped_worker_context inner{device()};
                    index_inner = get_gl_context_index();
                }
                index_after_inner = get_gl_context_index();
            }
            index_after_release = get_gl_context_index();
        }
    };
    worker.join();

    ASSERT_EQ(index_before, -1);
    ASSERT_GE(index_outer, 1);
    ASSERT_LE(index_outer, gl_worker_context_pool_size);
    ASSERT_EQ(index_inner, index_outer) << "nested scope must keep the same context";
    ASSERT_EQ(index_after_inner, index_outer) << "inner release must not drop the outer context";
    ASSERT_EQ(index_after_release, -1);
}

// T4: a worker creates a texture AND uploads its pixels (staging buffer
// with init_data, blit-encoder copy_from_buffer), all under one scope; the
// main thread reads the texture back and checks every texel. Exercises the
// texture-storage and texture-upload publication points and the
// main-consumer wait in the readback path.
TEST_F(Worker_context_gl_test, worker_texture_create_and_upload)
{
    require_worker_contexts();

    std::vector<std::uint8_t> expected(texture_bytes);
    fill_pixel_pattern(expected);

    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::shared_ptr<erhe::graphics::Buffer> staging = make_staging_buffer(expected, "T4 staging");
            texture = make_worker_texture(1, "T4 worker texture");
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T4 worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.copy_from_buffer(
                staging.get(),
                0,                                                   // source_offset
                bytes_per_row,                                       // source_bytes_per_row
                texture_bytes,                                       // source_bytes_per_image
                glm::ivec3{texture_width, texture_height, 1},        // source_size
                texture.get(),
                0,                                                   // destination_slice
                0,                                                   // destination_level
                glm::ivec3{0, 0, 0}                                  // destination_origin
            );
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(texture);

    const std::vector<std::uint8_t> actual = read_texture_rgba8(*texture);
    ASSERT_EQ(actual, expected);
}

// T5a: worker-side generate_mipmaps is a publication producer; the main
// thread reads a downsampled level back. With a constant-color level 0
// every level averages to the same color, so the check is exact.
TEST_F(Worker_context_gl_test, worker_generate_mipmaps)
{
    require_worker_contexts();

    std::vector<std::uint8_t> solid(texture_bytes);
    for (std::size_t i = 0; i < texture_bytes; i += 4) {
        solid[i + 0] = 10;
        solid[i + 1] = 20;
        solid[i + 2] = 30;
        solid[i + 3] = 255;
    }

    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::shared_ptr<erhe::graphics::Buffer> staging = make_staging_buffer(solid, "T5 staging");
            texture = make_worker_texture(
                erhe::graphics::get_texture_level_count(texture_width, texture_height),
                "T5 mipmapped texture"
            );
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T5 worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.copy_from_buffer(
                staging.get(), 0, bytes_per_row, texture_bytes,
                glm::ivec3{texture_width, texture_height, 1},
                texture.get(), 0, 0, glm::ivec3{0, 0, 0}
            );
            blit.generate_mipmaps(texture.get());
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(texture);
    ASSERT_GT(texture->get_level_count(), 1);

    const std::vector<std::byte> level1 = read_texture_level_bytes(*texture, 1, 4);
    ASSERT_EQ(level1.size(), (texture_bytes / 4u));
    for (std::size_t i = 0; i < level1.size(); i += 4) {
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 0]), 10u) << "texel " << (i / 4);
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 1]), 20u);
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 2]), 30u);
        EXPECT_EQ(static_cast<std::uint8_t>(level1[i + 3]), 255u);
    }
}

// T5b: worker-side fill_buffer is a publication producer; main reads the
// fill back through the blit copy path.
TEST_F(Worker_context_gl_test, worker_fill_buffer)
{
    require_worker_contexts();

    constexpr std::size_t byte_count = 512;
    constexpr std::uint8_t fill_value = 0xA5;

    std::shared_ptr<erhe::graphics::Buffer> buffer{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::vector<std::uint8_t> zeroes(byte_count, 0u);
            buffer = make_staging_buffer(zeroes, "T5b filled buffer");
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T5b worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.fill_buffer(buffer.get(), 0, byte_count, fill_value);
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(buffer);

    const std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "T5b readback");
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(buffer.get(), 0, readback.get(), 0, byte_count);
        }
    );
    const std::vector<std::byte> actual = read_buffer(*readback, byte_count);
    for (std::size_t i = 0; i < byte_count; ++i) {
        ASSERT_EQ(static_cast<std::uint8_t>(actual[i]), fill_value) << "byte " << i;
    }
}

// T13: worker GL errors are observable - the per-context debug callback
// must route a deliberately provoked worker-side GL error into the
// environment's message list. This is what makes the other worker tests'
// silence meaningful. The messages are consumed here so TearDown does not
// fail the case.
TEST_F(Worker_context_gl_test, worker_gl_errors_are_observable)
{
    require_worker_contexts();
    if (!device().get_info().use_debug_output) {
        GTEST_SKIP() << "GL debug output is not enabled in this configuration";
    }

    Gpu_test_environment::get().clear_messages();
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            // Deliberate GL_INVALID_VALUE: buffer name that was never
            // created by glCreateBuffers.
            gl::named_buffer_sub_data(0xDEADBEEFu, 0, 0, nullptr);
        }
    };
    worker.join();

    const std::vector<Gpu_test_environment::Message> messages = Gpu_test_environment::get().take_messages();
    ASSERT_FALSE(messages.empty()) << "worker GL error was silently discarded - the per-context debug callback is not installed or not routed";
}

// T7 + T8: one Vertex_input_state adopted on two contexts concurrently -
// both must succeed - and the accessor is idempotent per context: re-entry
// yields the same name. GL names are NEVER compared across contexts (name
// spaces are per-context; equal names are expected, not a bug).
TEST_F(Worker_context_gl_test, vertex_input_state_on_two_contexts)
{
    require_worker_contexts();

    erhe::graphics::Vertex_input_state vertex_input_state{device()};

    unsigned int worker_name_first  = 0;
    unsigned int worker_name_second = 0;
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const erhe::graphics::Scoped_vertex_input_state first{device(), vertex_input_state};
            worker_name_first = first.gl_name();
            // Concurrent with the main-thread adoption below is exercised
            // by the pool-contention test; here the property under test is
            // per-context idempotence.
            const erhe::graphics::Scoped_vertex_input_state second{device(), vertex_input_state};
            worker_name_second = second.gl_name();
        }
    };

    const erhe::graphics::Scoped_vertex_input_state main_first{device(), vertex_input_state};
    const erhe::graphics::Scoped_vertex_input_state main_second{device(), vertex_input_state};
    worker.join();

    ASSERT_NE(main_first.gl_name(), 0u);
    ASSERT_EQ(main_first.gl_name(), main_second.gl_name()) << "re-adoption on the main context must not create a second VAO";
    ASSERT_NE(worker_name_first, 0u);
    ASSERT_EQ(worker_name_first, worker_name_second) << "re-adoption on the worker context must not create a second VAO";
}

// T9: destroying container-object owners on the main thread while worker
// contexts hold instances queues those instances on the owning contexts'
// deferred-delete queues, and re-acquiring the contexts drains them.
TEST_F(Worker_context_gl_test, destruction_drains_deferred_deletes)
{
    require_worker_contexts();

    erhe::graphics::Device_impl& device_impl = device().get_impl();

    // Adopt a VAO and an FBO on one worker context.
    int worker_context_index = -1;
    {
        auto vertex_input_state = std::make_unique<erhe::graphics::Vertex_input_state>(device());

        const std::shared_ptr<erhe::graphics::Texture> attachment = make_worker_texture(1, "T9 attachment");
        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = attachment.get();
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = texture_width;
        descriptor.render_target_height = texture_height;
        descriptor.debug_label = erhe::utility::Debug_label{"T9 render pass"};
        auto render_pass = std::make_unique<erhe::graphics::Render_pass>(device(), descriptor);

        std::thread worker{
            [&]() {
                erhe::graphics::Scoped_worker_context worker_context{device()};
                worker_context_index = get_gl_context_index();
                const erhe::graphics::Scoped_vertex_input_state scoped_state{device(), *vertex_input_state};
                const erhe::graphics::Scoped_framebuffer        scoped_framebuffer{device(), *render_pass};
                ASSERT_NE(scoped_state.gl_name(), 0u);
                ASSERT_NE(scoped_framebuffer.gl_name(), 0u);
            }
        };
        worker.join();
        ASSERT_GE(worker_context_index, 1);
        ASSERT_EQ(device_impl.get_pending_container_delete_count(worker_context_index), 0u);

        // Destroy both owners on the main thread: the worker context's VAO
        // and FBO cannot be deleted from here, so they must be queued.
        vertex_input_state.reset();
        render_pass.reset();
    }
    ASSERT_EQ(device_impl.get_pending_container_delete_count(worker_context_index), 2u)
        << "main-thread destruction must queue the worker context's VAO and FBO for deferred deletion";

    // Re-acquiring pool contexts drains each acquired context's queue. The
    // free-slot list is LIFO, so the first acquire normally lands on the
    // same context; loop over the whole pool to be robust against ordering.
    for (int attempt = 0; attempt < gl_worker_context_pool_size; ++attempt) {
        if (device_impl.get_pending_container_delete_count(worker_context_index) == 0u) {
            break;
        }
        std::thread worker{
            [&]() {
                erhe::graphics::Scoped_worker_context worker_context{device()};
            }
        };
        worker.join();
    }
    ASSERT_EQ(device_impl.get_pending_container_delete_count(worker_context_index), 0u)
        << "re-acquiring the context must drain its deferred-delete queue";
}

// T10: a worker blits between two render passes it holds framebuffer
// accessors for (adoption happens inside blit_framebuffer); the source
// pixels were produced on the same worker (create + upload), so no
// main-side fence is involved, and the destination's publication is waited
// on by the main-thread readback.
TEST_F(Worker_context_gl_test, worker_blit_between_accessor_held_framebuffers)
{
    require_worker_contexts();

    std::vector<std::uint8_t> expected(texture_bytes);
    fill_pixel_pattern(expected);

    std::shared_ptr<erhe::graphics::Texture> source_texture{};
    std::shared_ptr<erhe::graphics::Texture> destination_texture{};
    std::unique_ptr<erhe::graphics::Render_pass> source_pass{};
    std::unique_ptr<erhe::graphics::Render_pass> destination_pass{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            const std::shared_ptr<erhe::graphics::Buffer> staging = make_staging_buffer(expected, "T10 staging");
            source_texture      = make_worker_texture(1, "T10 source");
            destination_texture = make_worker_texture(1, "T10 destination");
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T10 worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.copy_from_buffer(
                staging.get(), 0, bytes_per_row, texture_bytes,
                glm::ivec3{texture_width, texture_height, 1},
                source_texture.get(), 0, 0, glm::ivec3{0, 0, 0}
            );

            erhe::graphics::Render_pass_descriptor source_descriptor{};
            source_descriptor.color_attachments[0].texture       = source_texture.get();
            source_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            source_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
            source_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            source_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
            source_descriptor.render_target_width  = texture_width;
            source_descriptor.render_target_height = texture_height;
            source_descriptor.debug_label = erhe::utility::Debug_label{"T10 source pass"};
            source_pass = std::make_unique<erhe::graphics::Render_pass>(device(), source_descriptor);

            erhe::graphics::Render_pass_descriptor destination_descriptor{};
            destination_descriptor.color_attachments[0].texture       = destination_texture.get();
            destination_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            destination_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
            destination_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            destination_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
            destination_descriptor.render_target_width  = texture_width;
            destination_descriptor.render_target_height = texture_height;
            destination_descriptor.debug_label = erhe::utility::Debug_label{"T10 destination pass"};
            destination_pass = std::make_unique<erhe::graphics::Render_pass>(device(), destination_descriptor);

            blit.blit_framebuffer(
                *source_pass,
                glm::ivec2{0, 0},
                glm::ivec2{texture_width, texture_height},
                *destination_pass,
                glm::ivec2{0, 0}
            );
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(destination_texture);

    const std::vector<std::uint8_t> actual = read_texture_rgba8(*destination_texture);
    ASSERT_EQ(actual, expected);

    // The passes still hold worker-context FBO instances; destroying them
    // here queues deferred deletes, drained by later acquires / teardown.
    source_pass.reset();
    destination_pass.reset();
}

// T11: worker-side deletion of a shared buffer the MAIN context has bound.
// GL auto-unbinds a deleted object only in the deleting context; in the
// main context the orphan stays bound until the scrub drain issues a REAL
// unbind at the wait_frame() drain point. Verified with a raw GL binding
// query, never the cache.
TEST_F(Worker_context_gl_test, scrub_queue_real_unbind_on_main_drain)
{
    require_worker_contexts();

    std::vector<std::uint8_t> bytes(256, 0x11);
    std::shared_ptr<erhe::graphics::Buffer> buffer = make_staging_buffer(bytes, "T11 scrubbed buffer");
    const unsigned int deleted_name = buffer->get_impl().gl_name();
    ASSERT_NE(deleted_name, 0u);

    erhe::graphics::Gl_binding_state& main_binding_state = device().get_impl().get_binding_state();
    main_binding_state.bind_buffer(gl::Buffer_target::array_buffer, deleted_name);
    {
        GLint bound = 0;
        gl::get_integer_v(gl::Get_p_name::array_buffer_binding, &bound);
        ASSERT_EQ(static_cast<unsigned int>(bound), deleted_name);
    }

    // The worker releases the last reference: the deleting (worker) context
    // scrubs its own cache; the main context gets a scrub-queue entry.
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            buffer.reset();
        }
    };
    worker.join();
    ASSERT_FALSE(buffer);

    // Before the drain the orphan is still bound in the main context - this
    // is the silent-wrong-draw state the scrub exists to end.
    {
        GLint bound = 0;
        gl::get_integer_v(gl::Get_p_name::array_buffer_binding, &bound);
        ASSERT_EQ(static_cast<unsigned int>(bound), deleted_name) << "expected the deleted buffer to remain bound in the main context until the drain";
    }

    // Drive the main context's actual drain point (wait_frame inside
    // submit_and_wait) rather than calling the drain directly - "the drain
    // point actually runs" is part of what this test verifies.
    submit_and_wait([](erhe::graphics::Command_buffer&) {});

    {
        GLint bound = 0;
        gl::get_integer_v(gl::Get_p_name::array_buffer_binding, &bound);
        ASSERT_EQ(bound, 0) << "the main drain must issue a real glBindBuffer(target, 0), not just edit the cache";
    }
}

// T12: name-recycling epoch. A name rebound on the main context AFTER the
// worker-side delete was enqueued must survive the drain - when GL recycles
// the deleted name for the new buffer, only the epoch check prevents the
// drain from unbinding a live object. The assertion holds whether or not
// the name was recycled; the recycled case is the one the epoch exists for.
TEST_F(Worker_context_gl_test, scrub_queue_epoch_spares_rebound_name)
{
    require_worker_contexts();

    std::vector<std::uint8_t> bytes(256, 0x22);
    std::shared_ptr<erhe::graphics::Buffer> first = make_staging_buffer(bytes, "T12 first buffer");
    const unsigned int first_name = first->get_impl().gl_name();

    erhe::graphics::Gl_binding_state& main_binding_state = device().get_impl().get_binding_state();
    main_binding_state.bind_buffer(gl::Buffer_target::array_buffer, first_name);

    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            first.reset();
        }
    };
    worker.join();

    // Between the enqueue and the drain: create and bind a new buffer. GL
    // may hand back the recycled name.
    std::shared_ptr<erhe::graphics::Buffer> second = make_staging_buffer(bytes, "T12 second buffer");
    const unsigned int second_name = second->get_impl().gl_name();
    main_binding_state.bind_buffer(gl::Buffer_target::array_buffer, second_name);
    const bool name_recycled = (second_name == first_name);

    submit_and_wait([](erhe::graphics::Command_buffer&) {});

    {
        GLint bound = 0;
        gl::get_integer_v(gl::Get_p_name::array_buffer_binding, &bound);
        ASSERT_EQ(static_cast<unsigned int>(bound), second_name)
            << "a buffer bound after the delete was enqueued must survive the drain"
            << (name_recycled ? " (name WAS recycled - the epoch check is what protected it)" : " (name was not recycled this run)");
    }

    // Leave no dangling binding behind for later tests.
    main_binding_state.bind_buffer(gl::Buffer_target::array_buffer, 0);
    second.reset();
    submit_and_wait([](erhe::graphics::Command_buffer&) {});
}

// T6: the REVERSE handoff direction - the main thread writes staging
// pixels (map / unmap, main-only) and fences the handoff
// (publish_for_handoff); a worker waits on that fence (the blit encoder's
// source wait) and uploads the pixels into a texture it created; the main
// thread reads the texture back.
TEST_F(Worker_context_gl_test, main_written_staging_consumed_by_worker)
{
    require_worker_contexts();

    std::vector<std::uint8_t> expected(texture_bytes);
    fill_pixel_pattern(expected);

    // Main-created mappable staging buffer, written on main.
    const std::shared_ptr<erhe::graphics::Buffer> staging =
        make_host_buffer(texture_bytes, erhe::graphics::Buffer_usage::transfer_src, "T6 main staging");
    {
        const std::span<std::byte> mapped = staging->map_bytes(0, texture_bytes);
        std::memcpy(mapped.data(), expected.data(), texture_bytes);
        staging->unmap();
    }
    // The handoff fence: without it the worker's read of the staging bytes
    // has no GL-side ordering against the main context's write.
    staging->get_impl().publish_for_handoff();

    std::shared_ptr<erhe::graphics::Texture> texture{};
    std::thread worker{
        [&]() {
            erhe::graphics::Scoped_worker_context worker_context{device()};
            texture = make_worker_texture(1, "T6 worker texture");
            erhe::graphics::Command_buffer worker_command_buffer{device(), erhe::utility::Debug_label{"T6 worker cb"}};
            worker_command_buffer.begin();
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(worker_command_buffer);
            blit.copy_from_buffer(
                staging.get(), 0, bytes_per_row, texture_bytes,
                glm::ivec3{texture_width, texture_height, 1},
                texture.get(), 0, 0, glm::ivec3{0, 0, 0}
            );
            worker_command_buffer.end();
        }
    };
    worker.join();
    ASSERT_TRUE(texture);

    const std::vector<std::uint8_t> actual = read_texture_rgba8(*texture);
    ASSERT_EQ(actual, expected);
}

// T14: the guard actually guards - GPU work from a thread with NO worker
// scope must die on ERHE_VERIFY (HAS_CONTEXT) instead of faulting in the
// driver. Without this, the accessor / scope discipline is a convention
// rather than a mechanism.
TEST_F(Worker_context_gl_test, guard_aborts_offscope_worker_creation)
{
    require_worker_contexts();

    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            std::thread offscope_thread{
                [this]() {
                    const std::vector<std::uint8_t> bytes(16, 0u);
                    make_staging_buffer(bytes, "T14 off-scope buffer");
                }
            };
            offscope_thread.join();
        },
        ""
    );
}

} // namespace erhe::graphics::test
