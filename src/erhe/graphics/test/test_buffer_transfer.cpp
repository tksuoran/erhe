#include "gpu_test_fixture.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics::test {

// Command_buffer::upload_to_buffer records a GPU-side write; read it back and
// assert. Exercises the buffer upload path + map readback.
TEST_F(Gpu_test, buffer_upload_download)
{
    constexpr uint32_t    n     = 256;
    constexpr std::size_t bytes = static_cast<std::size_t>(n) * sizeof(uint32_t);

    std::vector<uint32_t> src(n);
    for (uint32_t i = 0; i < n; ++i) {
        src[i] = (i * 2654435761u) ^ 0x9e3779b9u; // arbitrary but deterministic
    }

    const std::shared_ptr<erhe::graphics::Buffer> buffer =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_dst, "upload_download");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_buffer(*buffer, 0, src.data(), bytes);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*buffer, bytes);
    std::vector<uint32_t> got(n);
    std::memcpy(got.data(), raw.data(), bytes);

    int mismatches = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (got[i] != src[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << n << " uploaded words differed after readback";
}

// Blit_command_encoder::copy_from_buffer (buffer -> buffer). Host-write the
// source, copy it to the destination on the GPU, read the destination back.
TEST_F(Gpu_test, buffer_to_buffer_copy)
{
    constexpr uint32_t    n     = 256;
    constexpr std::size_t bytes = static_cast<std::size_t>(n) * sizeof(uint32_t);

    std::vector<uint32_t> src(n);
    for (uint32_t i = 0; i < n; ++i) {
        src[i] = 0xDEAD0000u + i;
    }

    const std::shared_ptr<erhe::graphics::Buffer> source =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_src");
    const std::shared_ptr<erhe::graphics::Buffer> destination =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_dst, "copy_dst");

    // Host write into the source (visible to the device on the next submit,
    // and the memory is host_coherent).
    {
        const std::span<std::byte> mapped = source->map_bytes(0, bytes);
        std::memcpy(mapped.data(), src.data(), bytes);
        source->unmap();
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(source.get(), 0, destination.get(), 0, bytes);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*destination, bytes);
    std::vector<uint32_t> got(n);
    std::memcpy(got.data(), raw.data(), bytes);

    int mismatches = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (got[i] != src[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << n << " words differed after buffer-to-buffer copy";
}

// Blit_command_encoder::fill_buffer fills a byte value across the range.
TEST_F(Gpu_test, fill_buffer_constant)
{
    constexpr std::size_t bytes = 1024;
    constexpr uint8_t     value = 0xABu;

    const std::shared_ptr<erhe::graphics::Buffer> buffer =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_dst, "fill");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.fill_buffer(buffer.get(), 0, bytes, value);
        }
    );

    const std::vector<std::byte> raw = read_buffer(*buffer, bytes);
    int mismatches = 0;
    for (const std::byte byte_value : raw) {
        if (std::to_integer<uint8_t>(byte_value) != value) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << bytes << " bytes were not 0x" << std::hex << static_cast<int>(value);
}

// agfx CopyBufferToBuffer: Blit_command_encoder::copy_from_buffer (buffer ->
// buffer) with zero offsets over the whole source, then with non-zero source
// and destination offsets into a second buffer prefilled through fill_buffer
// (in its own submission, so the fill and the copy are ordered). The whole
// copy must equal the source; the offset copy must hold the source range at the
// destination offset and the fill everywhere else. Offsets and size are
// multiples of 4 (the strictest backend rule, Metal on macOS). Both
// destinations are also asserted against buffer goldens.
TEST_F(Gpu_test, copy_buffer_to_buffer)
{
    constexpr std::size_t size               = 1024;
    constexpr std::size_t source_offset      = 200;
    constexpr std::size_t destination_offset = 520;
    constexpr std::size_t copy_size          = 300;
    constexpr uint8_t     fill_value         = 0xCDu;

    std::vector<uint8_t> pattern(size);
    for (std::size_t i = 0; i < size; ++i) {
        pattern[i] = static_cast<uint8_t>(((i * 37u) + 11u) & 0xffu);
    }

    const std::shared_ptr<erhe::graphics::Buffer> source =
        make_host_buffer(size, erhe::graphics::Buffer_usage::transfer_src, "copy_buffer_to_buffer source");
    const std::shared_ptr<erhe::graphics::Buffer> whole =
        make_host_buffer(size, erhe::graphics::Buffer_usage::transfer_dst, "copy_buffer_to_buffer whole");
    const std::shared_ptr<erhe::graphics::Buffer> offset =
        make_host_buffer(size, erhe::graphics::Buffer_usage::transfer_dst, "copy_buffer_to_buffer offset");
    {
        const std::span<std::byte> mapped = source->map_bytes(0, size);
        std::memcpy(mapped.data(), pattern.data(), size);
        source->unmap();
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.fill_buffer(offset.get(), 0, size, fill_value);
        }
    );
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(source.get(), 0, whole.get(), 0, size);
            blit.copy_from_buffer(source.get(), source_offset, offset.get(), destination_offset, copy_size);
        }
    );

    std::vector<uint8_t> expected_offset(size, fill_value);
    std::memcpy(expected_offset.data() + destination_offset, pattern.data() + source_offset, copy_size);

    const std::vector<std::byte> whole_bytes  = read_buffer(*whole, size);
    const std::vector<std::byte> offset_bytes = read_buffer(*offset, size);
    std::size_t whole_differing  = 0;
    std::size_t offset_differing = 0;
    std::size_t first_offset     = size;
    for (std::size_t i = 0; i < size; ++i) {
        if (std::to_integer<uint8_t>(whole_bytes[i]) != pattern[i]) {
            ++whole_differing;
        }
        if (std::to_integer<uint8_t>(offset_bytes[i]) != expected_offset[i]) {
            if (offset_differing == 0) {
                first_offset = i;
            }
            ++offset_differing;
        }
    }
    EXPECT_EQ(whole_differing, 0u) << whole_differing << " of " << size << " bytes differ after the zero-offset copy";
    EXPECT_EQ(offset_differing, 0u)
        << offset_differing << " of " << size << " bytes differ after the offset copy, first at offset " << first_offset;

    expect_buffer_matches_golden("copy_buffer_to_buffer_whole", whole_bytes);
    expect_buffer_matches_golden("copy_buffer_to_buffer_offset", offset_bytes);
}

// Device::allocate_upload_staging is a hard cap (doc/erhe/ring_buffer_memory.md
// "Upload staging cap"): a request that does not fit the room left gets an
// empty range instead of a new buffer, smaller requests then wait behind it,
// a request larger than the cap waits while anything is staged, and a staged
// range copies like any ring range.
TEST_F(Gpu_test, upload_staging_cap)
{
    constexpr std::size_t cap          = 1024 * 1024;
    constexpr std::size_t large_bytes  = 600 * 1024;
    constexpr std::size_t small_bytes  = 64 * 1024;
    constexpr uint32_t    n            = static_cast<uint32_t>(large_bytes / sizeof(uint32_t));

    std::vector<uint32_t> src(n);
    for (uint32_t i = 0; i < n; ++i) {
        src[i] = 0x5EED0000u ^ (i * 2654435761u);
    }
    const std::shared_ptr<erhe::graphics::Buffer> destination =
        make_host_buffer(large_bytes, erhe::graphics::Buffer_usage::transfer_dst, "upload_staging_dst");

    bool second_large_refused = false;
    bool small_refused        = false;
    bool oversize_refused     = false;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Ring_buffer_range range = device().allocate_upload_staging(erhe::graphics::Buffer_target::transfer_src, large_bytes, cap);
            ASSERT_NE(range.get_buffer(), nullptr);
            ASSERT_GE(range.get_span().size(), large_bytes);
            std::memcpy(range.get_span().data(), src.data(), large_bytes);
            range.bytes_written(large_bytes);
            range.close();
            {
                erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(range.get_buffer()->get_buffer(), range.get_byte_start_offset_in_buffer(), destination.get(), 0, large_bytes);
            }
            range.release();

            erhe::graphics::Ring_buffer_range second_large = device().allocate_upload_staging(erhe::graphics::Buffer_target::transfer_src, large_bytes, cap);
            second_large_refused = (second_large.get_buffer() == nullptr);
            erhe::graphics::Ring_buffer_range small = device().allocate_upload_staging(erhe::graphics::Buffer_target::transfer_src, small_bytes, cap);
            small_refused = (small.get_buffer() == nullptr);
            erhe::graphics::Ring_buffer_range oversize = device().allocate_upload_staging(erhe::graphics::Buffer_target::transfer_src, 2 * cap, cap);
            oversize_refused = (oversize.get_buffer() == nullptr);
        }
    );

    EXPECT_TRUE(second_large_refused) << "a request beyond the room left under the cap must wait, not spill";
    EXPECT_TRUE(small_refused)        << "a smaller request must wait behind the refused larger one";
    EXPECT_TRUE(oversize_refused)     << "a request larger than the cap must wait while anything is staged";

    const erhe::graphics::Upload_staging_usage usage = device().get_upload_staging_usage();
    EXPECT_EQ(usage.capacity_byte_count, cap);
    EXPECT_GE(usage.high_water_bytes, large_bytes);
    EXPECT_LE(usage.high_water_bytes, cap);
    EXPECT_EQ(usage.refused_count, 3u);

    const std::vector<std::byte> raw = read_buffer(*destination, large_bytes);
    EXPECT_EQ(std::memcmp(raw.data(), src.data(), large_bytes), 0) << "staged bytes differed after the copy";
}

} // namespace erhe::graphics::test
