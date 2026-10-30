// The GPU disocclusion passes of the potentially visible set bake
// (doc/plans/pvs.md "Bake" 5 and "Verification";
// erhe_scene_renderer/pvs_disocclusion_pipelines.hpp) against their CPU
// reference (pvs_disocclusion.hpp): every synthetic layered buffer of
// test_pvs_disocclusion_cases.hpp is uploaded with its column bounds, the
// pass chain runs, and the closed flags, column masks, tile masks,
// disoccluded map and column words read back equal the reference's bit for
// bit.

#include "gpu_test_fixture.hpp"
#include "scene_renderer_test_logging.hpp"
#include "test_pvs_disocclusion_cases.hpp"

#include "erhe_scene_renderer/pvs_disocclusion.hpp"
#include "erhe_scene_renderer/pvs_disocclusion_pipelines.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace erhe::scene_renderer::test {

using erhe::graphics::test::Gpu_test;

namespace {

[[nodiscard]] auto shader_path() -> std::filesystem::path
{
    return std::filesystem::path{ERHE_SCENE_RENDERER_TEST_REPO_ROOT} / "res" / "shaders";
}

[[nodiscard]] auto to_words(const std::vector<std::byte>& bytes) -> std::vector<uint32_t>
{
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), words.size() * sizeof(uint32_t));
    return words;
}

// The words that differ (and a size mismatch), the first few listed in
// `report` as index: gpu != reference.
[[nodiscard]] auto count_differences(const std::vector<uint32_t>& gpu, const std::vector<uint32_t>& reference, std::string& report) -> std::size_t
{
    std::size_t differences = (gpu.size() > reference.size()) ? (gpu.size() - reference.size()) : (reference.size() - gpu.size());
    for (std::size_t i = 0, end = std::min(gpu.size(), reference.size()); i < end; ++i) {
        if (gpu[i] != reference[i]) {
            if (differences < 8) {
                report += fmt::format(" [{}]: {:08x} != {:08x}", i, gpu[i], reference[i]);
            }
            ++differences;
        }
    }
    return differences;
}

} // anonymous namespace

class Pvs_disocclusion_gpu_test : public Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();
        initialize_scene_renderer_test_logging();
    }

    void run_and_compare(const Pvs_test_case& test_case)
    {
        using namespace erhe::graphics;
        using Usage = Buffer_usage_flag_bit_mask;

        Pvs_disocclusion_pipelines pipelines{device(), shader_path()};
        ASSERT_TRUE(pipelines.is_supported()) << "a pvs_*.comp shader did not build";
        Pvs_disocclusion_buffers buffers{
            device(), test_case.parameters.get_resolution(), test_case.parameters.get_tile_size(), test_case.parameters.get_layer_count()
        };

        Pvs_disocclusion_result reference{};
        run_pvs_disocclusion_reference(test_case.parameters, std::span<const uint32_t>{test_case.layered_words}, std::span<const glm::vec4>{test_case.column_bounds}, reference);

        // The layered buffer in the Pvs_layered_interface layout: a 32 byte
        // header (the passes read none of it), then the words.
        constexpr std::size_t header_bytes  = 32;
        const std::size_t     words_bytes   = test_case.layered_words.size() * sizeof(uint32_t);
        const std::size_t     layered_bytes = header_bytes + words_bytes;
        std::unique_ptr<Buffer> layered = std::make_unique<Buffer>(
            device(),
            Buffer_create_info{
                .capacity_byte_count               = layered_bytes,
                .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst,
                .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
                .debug_label                       = erhe::utility::Debug_label{"PVS test layered buffer"}
            }
        );
        const std::size_t column_bytes = test_case.parameters.get_column_word_count() * sizeof(uint32_t);
        const std::size_t bounds_bytes = test_case.column_bounds.size() * sizeof(glm::vec4);
        const auto make_device_buffer = [&](const std::size_t byte_count, const char* label) -> std::unique_ptr<Buffer> {
            return std::make_unique<Buffer>(
                device(),
                Buffer_create_info{
                    .capacity_byte_count               = byte_count,
                    .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst | Buffer_usage::transfer_src,
                    .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
                    .debug_label                       = erhe::utility::Debug_label{label}
                }
            );
        };
        std::unique_ptr<Buffer> column_words  = make_device_buffer(column_bytes, "PVS test column words");
        std::unique_ptr<Buffer> column_bounds = make_device_buffer(bounds_bytes, "PVS test column bounds");
        std::shared_ptr<Buffer> closed_readback       = make_readback_buffer(buffers.get_closure_bytes(),      "PVS test closed readback");
        std::shared_ptr<Buffer> column_masks_readback = make_readback_buffer(buffers.get_column_masks_bytes(), "PVS test column masks readback");
        std::shared_ptr<Buffer> tile_masks_readback   = make_readback_buffer(buffers.get_tile_masks_bytes(),   "PVS test tile masks readback");
        std::shared_ptr<Buffer> map_readback          = make_readback_buffer(buffers.get_map_bytes(),          "PVS test map readback");
        std::shared_ptr<Buffer> runs_readback  = make_readback_buffer(buffers.get_runs_bytes(),  "PVS test runs readback");
        std::shared_ptr<Buffer> column_words_readback = make_readback_buffer(column_bytes,                     "PVS test column words readback");

        std::vector<std::byte> layered_image(layered_bytes, std::byte{0});
        std::memcpy(layered_image.data() + header_bytes, test_case.layered_words.data(), words_bytes);

        submit_and_wait(
            [&](Command_buffer& command_buffer) {
                command_buffer.upload_to_buffer(*layered, 0, layered_image.data(), layered_image.size());
                command_buffer.upload_to_buffer(*column_bounds, 0, test_case.column_bounds.data(), bounds_bytes);
                {
                    Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                    blit.fill_buffer(column_words.get(), 0, column_bytes, 0x00u);
                }
                command_buffer.buffer_barrier(*layered,       Usage::transfer_dst, Usage::storage_read | Usage::compute_shader);
                command_buffer.buffer_barrier(*column_bounds, Usage::transfer_dst, Usage::storage_read | Usage::compute_shader);
                command_buffer.buffer_barrier(*column_words,  Usage::transfer_dst, Usage::storage_read | Usage::storage_write | Usage::compute_shader);
                pipelines.record_face(command_buffer, buffers, *layered, layered_bytes, test_case.parameters, *column_bounds, bounds_bytes, *column_words, nullptr);
                constexpr uint64_t written = Usage::storage_write | Usage::compute_shader;
                command_buffer.buffer_barrier(buffers.get_closure(),      written, Usage::transfer_src);
                command_buffer.buffer_barrier(buffers.get_column_masks(), written, Usage::transfer_src);
                command_buffer.buffer_barrier(buffers.get_tile_masks(),   written, Usage::transfer_src);
                command_buffer.buffer_barrier(buffers.get_map(),          written, Usage::transfer_src);
                command_buffer.buffer_barrier(buffers.get_runs(),  written, Usage::transfer_src);
                command_buffer.buffer_barrier(*column_words,              written, Usage::transfer_src);
                {
                    Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
                    blit.copy_from_buffer(&buffers.get_closure(),      0, closed_readback.get(),       0, buffers.get_closure_bytes());
                    blit.copy_from_buffer(&buffers.get_column_masks(), 0, column_masks_readback.get(), 0, buffers.get_column_masks_bytes());
                    blit.copy_from_buffer(&buffers.get_tile_masks(),   0, tile_masks_readback.get(),   0, buffers.get_tile_masks_bytes());
                    blit.copy_from_buffer(&buffers.get_map(),          0, map_readback.get(),          0, buffers.get_map_bytes());
                    blit.copy_from_buffer(&buffers.get_runs(),  0, runs_readback.get(),  0, buffers.get_runs_bytes());
                    blit.copy_from_buffer(column_words.get(),          0, column_words_readback.get(), 0, column_bytes);
                }
                command_buffer.buffer_barrier(*closed_readback,       Usage::transfer_dst, Usage::host_read);
                command_buffer.buffer_barrier(*column_masks_readback, Usage::transfer_dst, Usage::host_read);
                command_buffer.buffer_barrier(*tile_masks_readback,   Usage::transfer_dst, Usage::host_read);
                command_buffer.buffer_barrier(*map_readback,          Usage::transfer_dst, Usage::host_read);
                command_buffer.buffer_barrier(*runs_readback,  Usage::transfer_dst, Usage::host_read);
                command_buffer.buffer_barrier(*column_words_readback, Usage::transfer_dst, Usage::host_read);
            }
        );

        const std::vector<uint32_t> closed       = to_words(read_buffer(*closed_readback,       buffers.get_closure_bytes()));
        const std::vector<uint32_t> column_masks = to_words(read_buffer(*column_masks_readback, buffers.get_column_masks_bytes()));
        const std::vector<uint32_t> tile_masks   = to_words(read_buffer(*tile_masks_readback,   buffers.get_tile_masks_bytes()));
        const std::vector<uint32_t> map          = to_words(read_buffer(*map_readback,          buffers.get_map_bytes()));
        const std::vector<uint32_t> runs         = to_words(read_buffer(*runs_readback,  buffers.get_runs_bytes()));
        const std::vector<uint32_t> columns      = to_words(read_buffer(*column_words_readback, column_bytes));
        std::string closed_report;
        std::string column_masks_report;
        std::string tile_masks_report;
        std::string map_report;
        std::string runs_report;
        std::string columns_report;
        EXPECT_EQ(count_differences(closed,       reference.closed,       closed_report),       0u) << test_case.name << ": closed tiles" << closed_report;
        EXPECT_EQ(count_differences(column_masks, reference.column_masks, column_masks_report), 0u) << test_case.name << ": column masks" << column_masks_report;
        EXPECT_EQ(count_differences(tile_masks,   reference.tile_masks,   tile_masks_report),   0u) << test_case.name << ": tile masks" << tile_masks_report;
        EXPECT_EQ(count_differences(map,          reference.map,          map_report),          0u) << test_case.name << ": disoccluded map" << map_report;
        EXPECT_EQ(count_differences(runs,         reference.runs,  runs_report),  0u) << test_case.name << ": runs" << runs_report;
        EXPECT_EQ(count_differences(columns,      reference.column_words, columns_report),      0u) << test_case.name << ": column words" << columns_report;
    }
};

TEST_F(Pvs_disocclusion_gpu_test, figure_2_matches_reference)
{
    run_and_compare(make_figure_2_case());
}

TEST_F(Pvs_disocclusion_gpu_test, empty_matches_reference)
{
    run_and_compare(make_empty_case());
}

TEST_F(Pvs_disocclusion_gpu_test, closed_layer_0_matches_reference)
{
    run_and_compare(make_closed_layer_0_case());
}

TEST_F(Pvs_disocclusion_gpu_test, intersection_matches_reference)
{
    run_and_compare(make_intersection_case());
}

TEST_F(Pvs_disocclusion_gpu_test, degenerate_matches_reference)
{
    run_and_compare(make_degenerate_case());
}

TEST_F(Pvs_disocclusion_gpu_test, grazing_band_matches_reference)
{
    run_and_compare(make_grazing_band_case());
}

TEST_F(Pvs_disocclusion_gpu_test, gating_matches_reference)
{
    run_and_compare(make_gating_closed_case());
    run_and_compare(make_gating_open_case());
}

TEST_F(Pvs_disocclusion_gpu_test, rule_3_matches_reference)
{
    run_and_compare(make_rule_3_stair_case());
    run_and_compare(make_rule_3_box_edge_case());
    run_and_compare(make_rule_3_crease_case());
    run_and_compare(make_rule_3_one_row_case());
    run_and_compare(make_rule_3_inner_edge_case());
    run_and_compare(make_rule_3_small_step_case());
    run_and_compare(make_rule_3_ledge_case());
    run_and_compare(make_rule_1_step_case());
    run_and_compare(make_epsilon_case(Pvs_epsilon_step::none));
    run_and_compare(make_epsilon_case(Pvs_epsilon_step::one_centimeter));
}

TEST_F(Pvs_disocclusion_gpu_test, bounds_vote_matches_reference)
{
    run_and_compare(make_bounds_vote_case());
}

TEST_F(Pvs_disocclusion_gpu_test, production_layer_count_matches_reference)
{
    run_and_compare(make_word_boundary_case());
    for (const uint32_t seed : {4u, 5u}) {
        run_and_compare(make_random_case(seed, 64));
    }
}

TEST_F(Pvs_disocclusion_gpu_test, random_matches_reference)
{
    for (const uint32_t seed : {1u, 2u, 3u}) {
        run_and_compare(make_random_case(seed));
    }
}

} // namespace erhe::scene_renderer::test
