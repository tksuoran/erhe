#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics
{
    class Gpu_timer;
};

namespace erhe::toolkit
{
    class Timer;
};

namespace erhe::application
{

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
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImVec2             m_frame_size     {256.0f, 64.0f};
#endif
    std::vector<float> m_values;
};

class Gpu_timer_plot
    : public Plot
{
public:
    explicit Gpu_timer_plot(
        erhe::graphics::Gpu_timer* timer,
        const std::size_t          width = 256
    );

    void sample() override;

    [[nodiscard]] auto gpu_timer() const -> erhe::graphics::Gpu_timer*;
    [[nodiscard]] auto label    () const -> const char* override;

private:
    erhe::graphics::Gpu_timer* m_gpu_timer{nullptr};
};

class Cpu_timer_plot
    : public Plot
{
public:
    explicit Cpu_timer_plot(
        erhe::toolkit::Timer* timer,
        const std::size_t     width = 256
    );

    void sample() override;

    [[nodiscard]] auto timer() const -> erhe::toolkit::Timer*;
    [[nodiscard]] auto label() const -> const char* override;

private:
    erhe::toolkit::Timer* m_timer{nullptr};
};

class Frame_time_plot
    : public Plot
{
public:
    explicit Frame_time_plot(std::size_t width = 256);

    void sample() override;

    [[nodiscard]] auto timer() const -> erhe::toolkit::Timer*;
    [[nodiscard]] auto label() const -> const char* override;

private:
    std::optional<std::chrono::steady_clock::time_point> m_last_frame_time_point;
};

class Performance_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Performance_window"};
    static constexpr std::string_view c_title{"Performance"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Performance_window ();
    ~Performance_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

private:
    Frame_time_plot             m_frame_time_plot;
    std::vector<Gpu_timer_plot> m_gpu_timer_plots;
    std::vector<Cpu_timer_plot> m_cpu_timer_plots;
    bool m_pause{false};

    //int  m_taps   = 1;
    //int  m_expand = 0;
    //int  m_reduce = 0;
    //bool m_linear = true;
};

} // namespace editor
