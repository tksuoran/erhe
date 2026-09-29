#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace erhe::graphics::test {

enum class Golden_kind : unsigned int {
    buffer,
    image
};

enum class Golden_outcome : unsigned int {
    compared, // compared against an existing golden (pass or fail per the test status)
    updated,  // ERHE_GPU_TEST_UPDATE_GOLDENS=1: the golden was (re)written
    missing,  // no golden file exists (the test fails)
    error     // the golden or an artifact could not be read / written (the test fails)
};

// One golden assertion of a test, filled by Gpu_test::expect_*_matches_golden
// and written to results.json by the results listener. Artifact paths are
// relative to the results directory, with forward slashes.
class Golden_record
{
public:
    Golden_kind    kind   {Golden_kind::buffer};
    Golden_outcome outcome{Golden_outcome::compared};
    std::string    name;
    std::string    golden_file;      // path of the golden in the source tree
    std::string    output_artifact;
    std::string    golden_artifact;
    std::string    flip_artifact;    // image goldens only
    std::string    note;

    // Image goldens
    std::string    format;           // "rgba8" or "rgba32f"
    int            width        {0};
    int            height       {0};
    int            golden_width {0};
    int            golden_height{0};
    float          threshold    {0.0f};
    float          flip_mean    {-1.0f};
    float          flip_max     {-1.0f};

    // Buffer goldens
    std::size_t              output_size {0};
    std::size_t              golden_size {0};
    std::size_t              diff_count  {0};
    std::vector<std::size_t> diff_offsets; // first differing byte offsets, at most c_max_recorded_diff_offsets
};

inline constexpr std::size_t c_max_recorded_diff_offsets = 4096;

// Where the run's results go: ERHE_GPU_TEST_RESULTS_DIR when set, otherwise
// <working directory>/gpu_test_results. results.json is written at the root,
// per-test artifacts under artifacts/<suite>.<test>/<golden name>/.
[[nodiscard]] auto get_results_directory() -> std::filesystem::path;

// Name of the currently running test as "<suite>.<test>", or "unknown" outside
// a test.
[[nodiscard]] auto get_current_test_name() -> std::string;

// Collects the golden records of the running test. The golden helpers add to
// it; the results listener drains it when a test ends. The fixture clears it
// when a test starts, so targets without the listener do not accumulate.
class Gpu_test_results
{
public:
    [[nodiscard]] static auto get() -> Gpu_test_results&;

    void               add_golden_record (const Golden_record& record);
    void               clear             ();
    [[nodiscard]] auto take_golden_records() -> std::vector<Golden_record>;

private:
    std::mutex                 m_mutex;
    std::vector<Golden_record> m_records;
};

// Appends the GoogleTest event listener that writes <results>/results.json
// after the run: device, backend, timestamp, summary counts and one entry per
// test with its golden records. Called by the GPU test main() after
// InitGoogleTest. The listener also empties <results>/artifacts when the run
// starts so the directory only holds the current run's artifacts.
void install_results_listener();

} // namespace erhe::graphics::test
