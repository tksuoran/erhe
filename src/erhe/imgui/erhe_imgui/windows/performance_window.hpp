#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <imgui/imgui.h>

#include <chrono>
#include <optional>
#include <vector>

namespace erhe::graphics {
    class Gpu_timer;
}
namespace erhe::time {
    class Timer;
}

namespace erhe::imgui {

class Imgui_windows;

class Plot
{
public:
    virtual ~Plot() noexcept;

    void imgui();
    void clear();
    [[nodiscard]] auto last_value() const -> float;

    virtual void sample() = 0;
    [[nodiscard]] virtual auto label () const -> const char* = 0;

protected:
    std::size_t        m_offset         {0};
    std::size_t        m_value_count    {0};
    float              m_max_great      {2.5f};
    float              m_max_ok         {5.0f};
    float              m_scale_min      {0.0f};
    float              m_scale_max      {5.0f}; // 2 ms
    float              m_scale_max_limit{1.0f}; // 1 ms
    ImVec2             m_frame_size     {512, 128.0f};
    std::vector<float> m_values;
};

class Gpu_timer_plot : public Plot
{
public:
    explicit Gpu_timer_plot(erhe::graphics::Gpu_timer* timer, std::size_t width = 256);

    void sample() override;
    auto label() const -> const char* override;

    [[nodiscard]] auto gpu_timer() const -> erhe::graphics::Gpu_timer*;

private:
    erhe::graphics::Gpu_timer* m_gpu_timer{nullptr};
};

class Cpu_timer_plot : public Plot
{
public:
    explicit Cpu_timer_plot(erhe::time::Timer* timer, std::size_t width = 256);

    void sample() override;
    auto label() const -> const char* override;

    [[nodiscard]] auto timer() const -> erhe::time::Timer*;

private:
    erhe::time::Timer* m_timer{nullptr};
};

class Frame_time_plot : public Plot
{
public:
    explicit Frame_time_plot(std::size_t width = 256);

    void sample() override;
    auto label() const -> const char* override;

    [[nodiscard]] auto timer() const -> erhe::time::Timer*;

private:
    std::optional<std::chrono::steady_clock::time_point> m_last_frame_time_point;
};

class Performance_window : public Imgui_window
{
public:
    Performance_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows);

    // Implements Imgui_window
    void imgui() override;

    void register_plot(Plot* plot);
    void unregister_plot(Plot* plot);

private:
    Frame_time_plot             m_frame_time_plot;
    std::vector<Gpu_timer_plot> m_gpu_timer_plots;
    std::vector<Cpu_timer_plot> m_cpu_timer_plots;
    std::vector<Plot*>          m_generic_plots;
    bool                        m_pause{false};
};

} // namespace editor
