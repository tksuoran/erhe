// Present calibration cache entry file (present_calibration_cache.hpp): key
// hash naming, bit-exact round trip, key mismatch; no device involved.

#include "erhe_graphics/present_calibration_cache.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#   include <process.h>
#else
#   include <unistd.h>
#endif

namespace {

using erhe::graphics::Present_calibration_cache_entry;
using erhe::graphics::Present_calibration_key;
using erhe::graphics::Present_calibration_phase;
using erhe::graphics::Present_calibration_sample;
using erhe::graphics::Present_path;
using erhe::graphics::make_present_calibration_cache_path;
using erhe::graphics::read_present_calibration_cache_file;
using erhe::graphics::write_present_calibration_cache_file;

auto process_id() -> long
{
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

auto make_key() -> Present_calibration_key
{
    Present_calibration_key key{};
    key.display_vendor            = 0x10ac;
    key.display_model             = 0x42f1;
    key.display_serial            = 0x12345678;
    key.mode_pixel_width          = 3840;
    key.mode_pixel_height         = 1440;
    key.mode_refresh_rate         = 120.0;
    key.mode_io_flags             = 3;
    key.backing_scale_factor      = 1.0;
    key.minimum_refresh_interval  = 1.0 / 120.0;
    key.maximum_refresh_interval  = 1.0 / 48.0;
    key.update_granularity        = 0.0;
    key.maximum_frames_per_second = 120;
    key.os_version                = "27.0.1";
    key.os_build                  = "26A434";
    key.gpu_name                  = "Test GPU";
    key.present_path              = Present_path::metal;
    return key;
}

auto make_entry() -> Present_calibration_cache_entry
{
    Present_calibration_cache_entry entry{};
    entry.key                         = make_key();
    entry.display_label               = "Test Display";
    entry.calibration.done            = true;
    entry.calibration.valid           = true;
    entry.calibration.status          = "test";
    entry.calibration.period          = 1.0 / 120.0;
    entry.calibration.request_offset  = 0.011458333330665482;
    entry.calibration.request_min     = 0.1 / 12.0;
    entry.calibration.request_max     = 0.014583333329937886;
    entry.calibration.deadline_offset = 0.017046749951783568;
    Present_calibration_sample sample{};
    sample.phase             = Present_calibration_phase::deadline;
    sample.target_time       = 12345.678901234567;
    sample.parameter         = 0.1 / 3.0;
    sample.completion_margin = 0.2 / 7.0;
    sample.presented_time    = 12345.687234567891;
    sample.slot_offset       = -1;
    entry.calibration.samples.push_back(sample);
    entry.measured_unix_seconds = 1792776185;
    entry.measured_utc          = "2026-10-23T17:23:05Z";
    entry.build                 = "test build";
    return entry;
}

class Present_calibration_cache_file : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_directory = std::filesystem::temp_directory_path() / ("erhe_present_calibration_cache_test_" + std::to_string(process_id()));
        std::filesystem::remove_all(m_directory);
    }
    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_directory, ec);
    }
    std::filesystem::path m_directory;
};

auto same_bits(const double a, const double b) -> bool
{
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

TEST_F(Present_calibration_cache_file, round_trip_is_bit_exact)
{
    const Present_calibration_cache_entry entry = make_entry();
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, entry.key);
    ASSERT_TRUE(write_present_calibration_cache_file(path, entry));

    std::string reason;
    const std::optional<Present_calibration_cache_entry> read = read_present_calibration_cache_file(path, entry.key, reason);
    ASSERT_TRUE(read.has_value()) << reason;
    EXPECT_EQ(read->key, entry.key);
    EXPECT_TRUE(read->calibration.valid);
    EXPECT_TRUE(same_bits(read->calibration.period,          entry.calibration.period));
    EXPECT_TRUE(same_bits(read->calibration.request_offset,  entry.calibration.request_offset));
    EXPECT_TRUE(same_bits(read->calibration.request_min,     entry.calibration.request_min));
    EXPECT_TRUE(same_bits(read->calibration.request_max,     entry.calibration.request_max));
    EXPECT_TRUE(same_bits(read->calibration.deadline_offset, entry.calibration.deadline_offset));
    ASSERT_EQ(read->calibration.samples.size(), 1u);
    const Present_calibration_sample& sample = read->calibration.samples.front();
    EXPECT_EQ(sample.phase, Present_calibration_phase::deadline);
    EXPECT_TRUE(same_bits(sample.target_time,       entry.calibration.samples.front().target_time));
    EXPECT_TRUE(same_bits(sample.completion_margin, entry.calibration.samples.front().completion_margin));
    EXPECT_EQ(sample.slot_offset, -1);
    EXPECT_EQ(read->measured_unix_seconds, entry.measured_unix_seconds);
    EXPECT_EQ(read->display_label, "Test Display");
}

TEST_F(Present_calibration_cache_file, every_key_field_names_the_file)
{
    const Present_calibration_key key = make_key();
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, key);
    EXPECT_EQ(path, make_present_calibration_cache_path(m_directory, make_key()));

    Present_calibration_key other = key;
    other.present_path = Present_path::metal4;
    EXPECT_NE(path, make_present_calibration_cache_path(m_directory, other));
    other = key;
    other.calibration_version = key.calibration_version + 1;
    EXPECT_NE(path, make_present_calibration_cache_path(m_directory, other));
    other = key;
    other.mode_refresh_rate = 60.0;
    EXPECT_NE(path, make_present_calibration_cache_path(m_directory, other));
}

TEST_F(Present_calibration_cache_file, differing_key_field_is_a_miss)
{
    const Present_calibration_cache_entry entry = make_entry();
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, entry.key);
    ASSERT_TRUE(write_present_calibration_cache_file(path, entry));

    // The file found by name, but one stored key field differs (an edited
    // or colliding entry).
    Present_calibration_key other = entry.key;
    other.display_serial = 1;
    std::string reason;
    EXPECT_FALSE(read_present_calibration_cache_file(path, other, reason).has_value());
    EXPECT_NE(reason.find("display_serial"), std::string::npos) << reason;
}

TEST_F(Present_calibration_cache_file, absent_or_malformed_is_a_miss)
{
    const Present_calibration_key key = make_key();
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, key);
    std::string reason;
    EXPECT_FALSE(read_present_calibration_cache_file(path, key, reason).has_value());
    EXPECT_EQ(reason, "no entry");

    std::filesystem::create_directories(m_directory);
    {
        std::ofstream file{path};
        file << "{ not json";
    }
    EXPECT_FALSE(read_present_calibration_cache_file(path, key, reason).has_value());
    EXPECT_EQ(reason, "entry is not valid JSON");
}

TEST_F(Present_calibration_cache_file, wrong_field_type_is_a_miss)
{
    const Present_calibration_cache_entry entry = make_entry();
    const std::filesystem::path path = make_present_calibration_cache_path(m_directory, entry.key);
    ASSERT_TRUE(write_present_calibration_cache_file(path, entry));
    std::string text;
    {
        std::ifstream file{path};
        text.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    }
    const std::string field = "\"measured_unix_seconds\": 1792776185";
    const std::size_t at = text.find(field);
    ASSERT_NE(at, std::string::npos);
    text.replace(at, field.size(), "\"measured_unix_seconds\": \"soon\"");
    {
        std::ofstream file{path, std::ios::trunc};
        file << text;
    }
    std::string reason;
    EXPECT_FALSE(read_present_calibration_cache_file(path, entry.key, reason).has_value());
    EXPECT_NE(reason.find("entry is malformed"), std::string::npos) << reason;
}

// Present_calibration_store with keys given directly (no window).

using erhe::graphics::Present_calibration;
using erhe::graphics::Present_calibration_cache_reads;
using erhe::graphics::Present_calibration_key_query;
using erhe::graphics::Present_calibration_source;
using erhe::graphics::Present_calibration_store;

auto make_query(const Present_calibration_key& key) -> Present_calibration_key_query
{
    return Present_calibration_key_query{key, "Test Display"};
}

auto make_measured() -> Present_calibration
{
    Present_calibration calibration = make_entry().calibration;
    calibration.prewarm_seconds_per_iteration = 3.0e-7;
    return calibration;
}

TEST_F(Present_calibration_cache_file, store_measures_writes_and_adopts)
{
    const Present_calibration_key key = make_key();
    {
        Present_calibration_store store{Present_calibration_cache_reads::enabled, m_directory};
        store.set_key_source(nullptr, key.gpu_name, key.present_path);
        Present_calibration in_out{};
        EXPECT_FALSE(store.begin(make_query(key), in_out)) << "an empty cache is a miss";
        store.finish_measured(make_measured());
        EXPECT_EQ(store.get_state().source, Present_calibration_source::measured);
        EXPECT_TRUE(std::filesystem::exists(make_present_calibration_cache_path(m_directory, key)));
    }
    {
        Present_calibration_store store{Present_calibration_cache_reads::enabled, m_directory};
        store.set_key_source(nullptr, key.gpu_name, key.present_path);
        Present_calibration in_out{};
        in_out.prewarm_seconds_per_iteration = 4.0e-7;
        ASSERT_TRUE(store.begin(make_query(key), in_out));
        EXPECT_EQ(store.get_state().source, Present_calibration_source::cached);
        EXPECT_TRUE(same_bits(in_out.deadline_offset, make_measured().deadline_offset));
        EXPECT_TRUE(same_bits(in_out.prewarm_seconds_per_iteration, 4.0e-7)) << "the pre-warm rate is the live one";
        ASSERT_TRUE(store.get_request_offset().has_value());
        EXPECT_TRUE(same_bits(store.get_request_offset().value(), make_measured().request_offset));

        // remove_entry deletes the file; the session keeps its calibration.
        EXPECT_EQ(store.remove_entry("test"), make_present_calibration_cache_path(m_directory, key).filename().string());
        EXPECT_FALSE(std::filesystem::exists(make_present_calibration_cache_path(m_directory, key)));
        EXPECT_TRUE(store.get_calibration().valid);
        EXPECT_TRUE(store.remove_entry("test").empty()) << "a second delete finds no entry";
    }
}

TEST_F(Present_calibration_cache_file, store_display_round_trip_keeps_session_result)
{
    // Reads disabled (the gate scripts' setting): the session's own
    // measurement still comes back when the window returns to its display.
    const Present_calibration_key key = make_key();
    Present_calibration_key other = key;
    other.display_serial = 7;
    Present_calibration_store store{Present_calibration_cache_reads::disabled, m_directory};
    store.set_key_source(nullptr, key.gpu_name, key.present_path);
    Present_calibration in_out{};
    EXPECT_FALSE(store.begin(make_query(key), in_out));
    store.finish_measured(make_measured());

    EXPECT_FALSE(store.refresh_for_display(make_query(key))) << "an unchanged key changes nothing";
    EXPECT_TRUE(store.refresh_for_display(make_query(other)));
    EXPECT_FALSE(store.get_calibration().valid) << "no calibration for the other display: cleared";
    EXPECT_EQ(store.get_state().source, Present_calibration_source::none);
    EXPECT_FALSE(store.get_request_offset().has_value());

    EXPECT_TRUE(store.refresh_for_display(make_query(key)));
    EXPECT_TRUE(store.get_calibration().valid) << "the session's measurement comes back";
    EXPECT_EQ(store.get_state().source, Present_calibration_source::measured);
    EXPECT_TRUE(same_bits(store.get_calibration().deadline_offset, make_measured().deadline_offset));
}

TEST_F(Present_calibration_cache_file, store_display_change_adopts_file_entry)
{
    const Present_calibration_key key = make_key();
    Present_calibration_key other = key;
    other.display_serial = 7;
    Present_calibration_cache_entry entry = make_entry();
    entry.key = other;
    entry.calibration.deadline_offset = 0.0125;
    ASSERT_TRUE(write_present_calibration_cache_file(make_present_calibration_cache_path(m_directory, other), entry));

    Present_calibration_store store{Present_calibration_cache_reads::enabled, m_directory};
    store.set_key_source(nullptr, key.gpu_name, key.present_path);
    Present_calibration in_out{};
    EXPECT_FALSE(store.begin(make_query(key), in_out));
    store.finish_measured(make_measured());
    EXPECT_TRUE(store.refresh_for_display(make_query(other)));
    EXPECT_EQ(store.get_state().source, Present_calibration_source::cached);
    EXPECT_TRUE(same_bits(store.get_calibration().deadline_offset, 0.0125));
}

} // namespace
