#include "gpu_test_results.hpp"
#include "gpu_test_environment.hpp"

#include "erhe_graphics/device.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

namespace erhe::graphics::test {

namespace {

[[nodiscard]] auto get_backend_name() -> const char*
{
#if defined(ERHE_GRAPHICS_API_VULKAN)
    return "vulkan";
#elif defined(ERHE_GRAPHICS_API_OPENGL)
    return "opengl";
#elif defined(ERHE_GRAPHICS_API_METAL)
    return "metal";
#else
    return "none";
#endif
}

[[nodiscard]] auto c_str(const Golden_kind kind) -> const char*
{
    switch (kind) {
        case Golden_kind::buffer: return "buffer";
        case Golden_kind::image:  return "image";
        default:                  return "?";
    }
}

[[nodiscard]] auto c_str(const Golden_outcome outcome) -> const char*
{
    switch (outcome) {
        case Golden_outcome::compared: return "compared";
        case Golden_outcome::updated:  return "updated";
        case Golden_outcome::missing:  return "missing";
        case Golden_outcome::error:    return "error";
        default:                       return "?";
    }
}

[[nodiscard]] auto to_json(const Golden_record& record) -> nlohmann::ordered_json
{
    nlohmann::ordered_json json;
    json["name"]        = record.name;
    json["kind"]        = c_str(record.kind);
    json["outcome"]     = c_str(record.outcome);
    json["golden_file"] = record.golden_file;
    json["output"]      = record.output_artifact;
    json["golden"]      = record.golden_artifact;
    if (!record.note.empty()) {
        json["note"] = record.note;
    }
    if (record.kind == Golden_kind::image) {
        json["format"]        = record.format;
        json["width"]         = record.width;
        json["height"]        = record.height;
        json["golden_width"]  = record.golden_width;
        json["golden_height"] = record.golden_height;
        json["flip"]          = record.flip_artifact;
        json["threshold"]     = record.threshold;
        json["flip_mean"]     = record.flip_mean;
        json["flip_max"]      = record.flip_max;
    } else {
        json["output_size"]  = record.output_size;
        json["golden_size"]  = record.golden_size;
        json["diff_count"]   = record.diff_count;
        json["diff_offsets"] = record.diff_offsets;
    }
    return json;
}

// Writes results.json when the run ends. It is the only writer of that file;
// the golden helpers only fill Gpu_test_results.
class Results_listener : public ::testing::EmptyTestEventListener
{
public:
    void OnTestProgramStart(const ::testing::UnitTest&) override
    {
        m_start = std::chrono::system_clock::now();
        const std::filesystem::path artifacts = get_results_directory() / "artifacts";
        std::error_code error_code;
        std::filesystem::remove_all(artifacts, error_code);
    }

    void OnEnvironmentsSetUpEnd(const ::testing::UnitTest&) override
    {
        Gpu_test_environment& environment = Gpu_test_environment::get();
        if (environment.is_available()) {
            m_device = environment.device().get_info().api_info;
        }
    }

    void OnTestStart(const ::testing::TestInfo&) override
    {
        Gpu_test_results::get().clear();
    }

    void OnTestEnd(const ::testing::TestInfo& test_info) override
    {
        const ::testing::TestResult* result = test_info.result();
        std::string status = "passed";
        if (result->Skipped()) {
            status = "skipped";
        } else if (result->Failed()) {
            status = "failed";
        }

        std::string message;
        for (int i = 0, end = result->total_part_count(); i < end; ++i) {
            const ::testing::TestPartResult& part = result->GetTestPartResult(i);
            if (part.message() == nullptr) {
                continue;
            }
            const std::string_view text{part.message()};
            if (text.empty()) {
                continue;
            }
            if (!message.empty()) {
                message.append("\n");
            }
            message.append(text);
        }

        nlohmann::ordered_json goldens = nlohmann::ordered_json::array();
        for (const Golden_record& record : Gpu_test_results::get().take_golden_records()) {
            if (!record.note.empty()) {
                if (!message.empty()) {
                    message.append("\n");
                }
                message.append(record.note);
            }
            goldens.push_back(to_json(record));
        }

        nlohmann::ordered_json entry;
        entry["name"]        = std::string{test_info.test_suite_name()} + "." + test_info.name();
        entry["status"]      = status;
        entry["duration_ms"] = static_cast<long long>(result->elapsed_time());
        entry["message"]     = message;
        entry["goldens"]     = goldens;
        m_tests.push_back(entry);
    }

    void OnTestProgramEnd(const ::testing::UnitTest& unit_test) override
    {
        int passed  = 0;
        int failed  = 0;
        int skipped = 0;
        for (const nlohmann::ordered_json& entry : m_tests) {
            const std::string status = entry["status"].get<std::string>();
            if (status == "passed") {
                ++passed;
            } else if (status == "failed") {
                ++failed;
            } else {
                ++skipped;
            }
        }

        nlohmann::ordered_json json;
        json["device"]      = m_device;
        json["backend"]     = get_backend_name();
        json["timestamp"]   = std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(m_start));
        json["duration_ms"] = static_cast<long long>(unit_test.elapsed_time());
        json["summary"]     = nlohmann::ordered_json{
            { "total",   static_cast<int>(m_tests.size()) },
            { "passed",  passed  },
            { "failed",  failed  },
            { "skipped", skipped }
        };
        json["tests"] = m_tests;

        const std::filesystem::path directory = get_results_directory();
        std::error_code error_code;
        std::filesystem::create_directories(directory, error_code);
        const std::filesystem::path path = directory / "results.json";
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        if (!stream) {
            std::cerr << "[ results  ] could not write " << path.string() << "\n";
            return;
        }
        stream << json.dump(2) << "\n";
        std::cout << "[ results  ] " << path.string() << "\n";
    }

private:
    std::chrono::system_clock::time_point m_start;
    std::string                           m_device;
    // Copy-initialized, not brace-initialized: brace initialization picks
    // nlohmann's initializer_list constructor and would make m_tests an array
    // holding one empty array.
    nlohmann::ordered_json                m_tests = nlohmann::ordered_json::array();
};

} // anonymous namespace

auto get_results_directory() -> std::filesystem::path
{
    const char* const override_directory = std::getenv("ERHE_GPU_TEST_RESULTS_DIR");
    if ((override_directory != nullptr) && (override_directory[0] != '\0')) {
        return std::filesystem::absolute(std::filesystem::path{override_directory});
    }
    return std::filesystem::current_path() / "gpu_test_results";
}

auto get_current_test_name() -> std::string
{
    const ::testing::TestInfo* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info == nullptr) {
        return "unknown";
    }
    return std::string{test_info->test_suite_name()} + "." + test_info->name();
}

auto Gpu_test_results::get() -> Gpu_test_results&
{
    static Gpu_test_results instance;
    return instance;
}

void Gpu_test_results::add_golden_record(const Golden_record& record)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_records.push_back(record);
}

void Gpu_test_results::clear()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_records.clear();
}

auto Gpu_test_results::take_golden_records() -> std::vector<Golden_record>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    std::vector<Golden_record> out;
    out.swap(m_records);
    return out;
}

void install_results_listener()
{
    // GoogleTest takes ownership of the listener.
    ::testing::UnitTest::GetInstance()->listeners().Append(new Results_listener{});
}

} // namespace erhe::graphics::test
