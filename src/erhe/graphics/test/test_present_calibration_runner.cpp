// run_present_calibration (present_calibration_runner.hpp): the grid fit,
// request window search and deadline split against a modelled compositor
// with the ProMotion properties measured on Metal and MoltenVK (request
// window 1.0 ... 1.75 T before the target, deadline about 2.1 T), including
// a coarse deadline pass the compositor displays a refresh late as a whole.
// The probes run in real time on a 4 ms grid (about a second per test).

#include "erhe_graphics/present_calibration.hpp"
#include "erhe_graphics/present_calibration_runner.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {

using erhe::graphics::IPresent_calibration_presenter;
using erhe::graphics::Present_calibration;
using erhe::graphics::Present_calibration_probe_feedback;

constexpr double s_period = 0.004;

auto now() -> double
{
    return erhe::frame_pacing::Frame_time_recorder::now();
}

// Displays an image at the first vsync at or after (request time + request
// latch), and no earlier than deadline after its GPU end, and after the
// previously displayed image. Plain presents (request time 0) display at the
// first vsync deadline after their GPU end.
class Model_compositor : public IPresent_calibration_presenter
{
public:
    double request_latch{1.0 * s_period};
    double deadline     {2.1 * s_period};
    double phase        {0.0};
    // Probes (requested presents) shifted a refresh late as a whole: the
    // coarse deadline pass is the 34th to 54th requested present (33 request
    // window probes come first).
    int    shifted_coarse_passes{0};

    auto present(const double request_time, const double commit_time) -> std::ptrdiff_t override
    {
        // Swapchain backpressure: an image is free once the one presented
        // queue depth presents earlier has been displayed.
        if (m_entries.size() >= get_queue_depth()) {
            erhe::graphics::present_calibration_wait_until(m_entries[m_entries.size() - get_queue_depth()].presented_time);
        }
        if (commit_time > 0.0) {
            erhe::graphics::present_calibration_wait_until(commit_time);
        }
        // The GPU end is the commit time the runner asked for: the test
        // process's sleeps overshoot by up to a period on a 4 ms grid, which
        // the model must not read as compositor behaviour.
        const double gpu_end = (commit_time > 0.0) ? commit_time : now();
        double displayed = vsync_at_or_after(gpu_end + deadline);
        if (request_time > 0.0) {
            displayed = std::max(displayed, vsync_at_or_after(request_time + request_latch));
            const int probe = m_requested++;
            if ((probe >= 33) && (probe < (33 + (21 * shifted_coarse_passes)))) {
                displayed += s_period;
            }
        }
        if (displayed <= m_last_displayed) {
            displayed = m_last_displayed + s_period;
        }
        m_last_displayed = displayed;
        m_entries.push_back(Present_calibration_probe_feedback{.presented_time = displayed, .gpu_end = gpu_end, .presented = true});
        return static_cast<std::ptrdiff_t>(m_entries.size() - 1);
    }

    auto wait(const std::size_t index, double) -> Present_calibration_probe_feedback override
    {
        return m_entries[index];
    }

    void service_window() override {}

    auto get_queue_depth() const -> std::size_t override { return 3; }

private:
    auto vsync_at_or_after(const double time) const -> double
    {
        return phase + (std::ceil(((time - phase) / s_period) - 1.0e-6) * s_period);
    }

    std::vector<Present_calibration_probe_feedback> m_entries;
    double m_last_displayed    {0.0};
    int    m_requested         {0};
};

TEST(Present_calibration_runner, finds_request_window_and_deadline)
{
    Model_compositor    compositor{};
    Present_calibration result{};
    const char* const failure = erhe::graphics::run_present_calibration(compositor, s_period, result);
    ASSERT_EQ(failure, nullptr) << result.status;
    EXPECT_TRUE(result.valid);
    EXPECT_NEAR(result.period,         s_period,         1.0e-6);
    EXPECT_NEAR(result.request_min,    1.0  * s_period,  1.0e-6);
    EXPECT_NEAR(result.request_max,    1.75 * s_period,  1.0e-6);
    EXPECT_NEAR(result.request_offset, 1.375 * s_period, 1.0e-6);
    // The split lies within a T/8 step of the modelled deadline, plus the guard.
    EXPECT_NEAR(result.deadline_offset - 0.001, compositor.deadline, 0.125 * s_period);
}

TEST(Present_calibration_runner, measures_a_late_coarse_pass_again)
{
    Model_compositor compositor{};
    compositor.shifted_coarse_passes = 1;
    Present_calibration result{};
    const char* const failure = erhe::graphics::run_present_calibration(compositor, s_period, result);
    ASSERT_EQ(failure, nullptr) << result.status;
    EXPECT_NEAR(result.request_offset, 1.375 * s_period, 1.0e-6);
    EXPECT_NEAR(result.deadline_offset - 0.001, compositor.deadline, 0.125 * s_period);
    EXPECT_NE(result.status.find("1 coarse deadline passes late as a whole"), std::string::npos) << result.status;
}

TEST(Present_calibration_runner, fails_when_every_coarse_pass_is_late)
{
    Model_compositor compositor{};
    compositor.shifted_coarse_passes = 3;
    Present_calibration result{};
    const char* const failure = erhe::graphics::run_present_calibration(compositor, s_period, result);
    EXPECT_NE(failure, nullptr);
    EXPECT_FALSE(result.valid);
}

} // namespace
