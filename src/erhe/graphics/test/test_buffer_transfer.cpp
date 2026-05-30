#include "gpu_test_fixture.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"

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

} // namespace erhe::graphics::test
