#include "erhe/application/windows/performance_window.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/timer.hpp"

#include <glm/glm.hpp>
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui_internal.h>
#endif

#include <string>
#include <vector>

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
    ImVec2             m_frame_size     {512, 128.0f};
#endif
    std::vector<float> m_values;
};

class Gpu_timer_plot
    : public Plot
{
public:
    explicit Gpu_timer_plot(
        erhe::graphics::Gpu_timer* timer,
        std::size_t                width = 256
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
        std::size_t           width = 256
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

class Performance_window_impl
    : public Imgui_window
{
public:
    Performance_window_impl()
        : Imgui_window{Performance_window::c_title}
    {
        g_imgui_windows->register_imgui_window(this, "performance");
    }

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

Performance_window* g_performance_window{nullptr};

Performance_window::Performance_window()
    : erhe::components::Component{c_type_name}
{
}

Performance_window::~Performance_window() noexcept
{
    ERHE_VERIFY(g_performance_window == nullptr);
}

void Performance_window::deinitialize_component()
{
    ERHE_VERIFY(g_performance_window == this);
    m_impl.reset();
    g_performance_window = nullptr;
}

void Performance_window::declare_required_components()
{
    require<Imgui_windows>();
}

void Performance_window::initialize_component()
{
    ERHE_VERIFY(g_performance_window == nullptr);
    m_impl = std::make_unique<Performance_window_impl>();
    g_performance_window = this;
}

#pragma region Plot
void Plot::clear()
{
    m_offset = 0;
    m_value_count = 0;
    std::fill(
        m_values.begin(),
        m_values.end(),
        0.0f
    );
}

auto Plot::last_value() const -> float
{
    if (m_value_count == 0) {
        return 0.0f;
    }
    const size_t last_index = (m_offset + m_values.size() - 1) % m_values.size();
    return m_values[last_index];
}

Gpu_timer_plot::Gpu_timer_plot(
    erhe::graphics::Gpu_timer* timer,
    const std::size_t          width
)
    : m_gpu_timer{timer}
{
    m_values.resize(width);
}

void Gpu_timer_plot::sample()
{
    const auto time_elapsed = static_cast<double>(m_gpu_timer->last_result());
    const auto sample_value = time_elapsed / 1000000.0;

    m_values[m_offset % m_values.size()] = static_cast<float>(sample_value);
    m_value_count = std::min(m_value_count + 1, m_values.size());
    m_offset++;
}

auto Gpu_timer_plot::label() const -> const char*
{
    return m_gpu_timer->label();
}

auto Gpu_timer_plot::gpu_timer() const -> erhe::graphics::Gpu_timer*
{
    return m_gpu_timer;
}

Cpu_timer_plot::Cpu_timer_plot(
    erhe::toolkit::Timer* timer,
    const std::size_t     width
)
    : m_timer{timer}
{
    m_values.resize(width);
}

void Cpu_timer_plot::sample()
{
    if (!m_timer->duration().has_value()) {
        return;
    }
    const auto sample_value = std::chrono::duration_cast<std::chrono::milliseconds>(m_timer->duration().value()).count();

    m_values[m_offset % m_values.size()] = static_cast<float>(sample_value);
    m_value_count = std::min(m_value_count + 1, m_values.size());
    m_offset++;
}

auto Cpu_timer_plot::label() const -> const char*
{
    return m_timer->label();
}

auto Cpu_timer_plot::timer() const -> erhe::toolkit::Timer*
{
    return m_timer;
}

Frame_time_plot::Frame_time_plot(std::size_t width)
{
    m_values.resize(width);
    m_max_great       = 16.6666f;
    m_max_ok          = 16.6666f * 2.0f;
    m_scale_max       = 16.6666f * 2.0f;
    m_scale_max_limit = 16.6666f * 2.0f;
}

void Frame_time_plot::sample()
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_last_frame_time_point.has_value()) {
        m_last_frame_time_point = now;
        return;
    }
    const std::chrono::duration<float, std::milli> duration = now - m_last_frame_time_point.value();
    m_last_frame_time_point = now;

    m_values[m_offset % m_values.size()] = static_cast<float>(duration.count());
    m_value_count = std::min(m_value_count + 1, m_values.size());
    m_offset++;
}

auto Frame_time_plot::timer() const -> erhe::toolkit::Timer*
{
    return nullptr;
}

auto Frame_time_plot::label() const -> const char*
{
    return "Frame time";
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
namespace {

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2{lhs.x + rhs.x, lhs.y + rhs.y};
}

static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2{lhs.x - rhs.x, lhs.y - rhs.y};
}

template<typename T>
static inline T clamp(T value, T min_value, T max_value)
{
    return (value < min_value)
        ? min_value
        : (value > max_value)
            ? max_value
            : value;
}

}
#endif

Plot::~Plot() noexcept
{
}

void Plot::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    //ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    //const ImGuiStyle& style = g.Style;
    const char* label_cstr = label();
    const ImGuiID id = ImGui::GetID(label_cstr);

    const auto label_size = ImGui::CalcTextSize(label_cstr, nullptr, true);
    // if (frame_size.x == 0.0f) {
    //     frame_size.x = CalcItemWidth();
    // }
    // if (frame_size.y == 0.0f) {
    //     frame_size.y = label_size.y + (style.FramePadding.y * 2);
    // }
    const auto& style      = ImGui::GetStyle();
    const auto  cursor_pos = window->DC.CursorPos;
    const auto& io = ImGui::GetIO();

    const ImRect frame_bb{cursor_pos, cursor_pos + m_frame_size};
    const ImRect inner_bb{frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding};
    const ImRect total_bb{ frame_bb.Min, frame_bb.Max + ImVec2{label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0}};
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, 0, &frame_bb)) {
        return;// -1;
    }

    // Determine scale from values if not specified
    //if (scale_min == FLT_MAX || scale_max == FLT_MAX)
    {
        //m_scale_min = std::numeric_limits<float>::max();
        m_scale_max = std::numeric_limits<float>::lowest();
        for (
            int i = 0, end = static_cast<int>(m_values.size());
            i < end;
            i++
        ) {
            const float v = m_values.at(i);
            if (v != v) { // Ignore NaN values
                continue;
            }
            //m_scale_min = std::min(m_scale_min, v);
            m_scale_max = std::max(m_scale_max, v);
        }
    }
    const auto displayed_max = m_scale_max;
    m_scale_max = std::max(m_scale_max, m_scale_max_limit);

    ImGui::RenderFrame(
        frame_bb.Min,
        frame_bb.Max,
        ImGui::GetColorU32(ImGuiCol_FrameBg),
        true,
        style.FrameRounding
    );

    //const std::size_t values_count_min = 2;
    //int idx_hovered = -1;
    //if (m_value_count >= values_count_min)
    {
        int item_count = (int)(m_values.size() - 1);
        if (item_count < 1) {
            return;
        }

        int res_w = std::min((int)(m_frame_size.x), item_count);

        // Tooltip on hover
        const bool hovered = ImGui::ItemHoverable(frame_bb, id);
        if (hovered && inner_bb.Contains(io.MousePos)) {
            const float box_width = inner_bb.Max.x - inner_bb.Min.x;

            if (box_width != 0.0f) {
                const float       t0        = clamp((io.MousePos.x - inner_bb.Min.x) / box_width, 0.0f, 0.9999f);
                const float       idx       = t0 * m_values.size();
                const std::size_t v_idx     = static_cast<std::size_t>(idx);
                IM_ASSERT(v_idx < m_values.size());

                const float v0 = m_values.at((v_idx + m_offset) % m_values.size());
                const float v1 = m_values.at((v_idx + 1 + m_offset) % m_values.size());
                const float t1 = t0 - static_cast<std::size_t>(t0);
                const float v  = (1.0f - t1) * v0 + t1 * v1;
                //const float i  = (v_idx + m_offset) % m_values.size() + t1;
                ImGui::SetTooltip("%.2g", v);
                //%d: %8.4g\n%d: %8.4g",
                //    (v_idx + m_offset) % m_values.size(),
                //    v0,
                //    (v_idx + 1 + m_offset) % m_values.size(),
                //    v1);
                //idx_hovered = v_idx;
            }
        }

        if (res_w == 0.0f) {
            return;
        }
        const float t_step   = 1.0f / (float)res_w;

        if (m_scale_max == m_scale_min) {
            return;
        }

        const float inv_scale = (m_scale_min == m_scale_max)
            ? 0.0f
            : (1.0f / (m_scale_max - m_scale_min));

        float  v0  = m_values.at(m_offset % m_values.size());
        float  t0  = 0.0f;
        ImVec2 tp0 = ImVec2{t0, 1.0f - ImSaturate((v0 - m_scale_min) * inv_scale)};  // Point in the normalized space of our target rectangle
        //float histogram_zero_line_t = (m_scale_min * m_scale_max < 0.0f)
        //    ? (1 + m_scale_min * inv_scale)
        //    : (m_scale_min < 0.0f ? 0.0f : 1.0f);   // Where does the zero line stands

        //const ImU32 col_base = 0xff00ff00;
        //const ImU32 col_hovered = GetColorU32((plot_type == ImGuiPlotType_Lines) ? ImGuiCol_PlotLinesHovered : ImGuiCol_PlotHistogramHovered);
        {
            const auto t = 1.0f - ImSaturate((m_max_great - m_scale_min) * inv_scale);
            const ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2{0.0f, t});
            const ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2{1.0f, t});
            window->DrawList->AddLine(pos0, pos1, 0x88008800u);
        }
        {
            const auto t = 1.0f - ImSaturate((m_max_ok - m_scale_min) * inv_scale);
            const ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2{0.0f, t});
            const ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2{1.0f, t});
            window->DrawList->AddLine(pos0, pos1, 0x88004488u);
        }

        for (int n = 0; n < res_w; n++) {
            const float       t1     = t0 + t_step;
            const int         v1_idx = (int)(t0 * item_count + 0.5f);
            const std::size_t idx    = (v1_idx + m_offset + 1) % m_values.size();
            const float       v1     = m_values.at(idx);
            const ImVec2      tp1 = ImVec2{t1, 1.0f - ImSaturate((v1 - m_scale_min) * inv_scale)};

            // NB: Draw calls are merged together by the DrawList system. Still, we should render our batch are lower level to save a bit of CPU.
            const ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, tp0);
            const ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, tp1);

            //const float    t     = static_cast<float>(idx) / static_cast<float>(m_values.size());
            //const auto     color = gradient::summer.get(t);
            //const uint32_t col   = ImGui::ColorConvertFloat4ToU32(ImVec4{color.x, color.y, color.z, 1.0});
            const uint32_t col = ((v0 <= m_max_great) && (v1 <= m_max_great))
                ? 0xff00ff00u
                : ((v0 <= m_max_ok) && (v1 <= m_max_ok))
                    ? 0xff0088ffu
                    : 0xff4400ffu;

            window->DrawList->AddLine(pos0, pos1, col);

            v0 = v1;
            t0 = t1;
            tp0 = tp1;
        }


        auto max_text = fmt::format("Max: {:.3f} ms", displayed_max);
        auto now_text = fmt::format("Now: {:.3f} ms", v0);
        ImGui::RenderTextClipped(
            ImVec2{frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y},
            frame_bb.Max,
            max_text.c_str(),
            nullptr,
            nullptr,
            ImVec2{0.0f, 0.0f}
        );
        const float font_size = ImGui::GetFontSize();
        ImGui::RenderTextClipped(
            ImVec2{frame_bb.Min.x, frame_bb.Max.y - style.FramePadding.y - font_size},
            frame_bb.Max,
            now_text.c_str(),
            nullptr,
            nullptr,
            ImVec2{0.0f, 0.0f}
        );
    }

    // Text overlay
    //if (overlay_text)
    //    RenderTextClipped(ImVec2(frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y), frame_bb.Max, overlay_text, NULL, NULL, ImVec2(0.5f, 0.0f));
    //
    //if (label_size.x > 0.0f)
    ImGui::RenderText(
        ImVec2{
            frame_bb.Max.x + style.ItemInnerSpacing.x,
            inner_bb.Min.y
        },
        label_cstr
    );

    // Return hovered index or -1 if none are hovered.
    // This is currently not exposed in the public API because we need a larger redesign of the whole thing, but in the short-term we are making it available in PlotEx().
    //return idx_hovered;
#endif
}
#pragma endregion Plot

void Performance_window_impl::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    const auto all_gpu_timers = erhe::graphics::Gpu_timer::all_gpu_timers();
    for (auto* timer : all_gpu_timers) {
        bool found{false};
        for (auto& plot : m_gpu_timer_plots) {
            if (plot.gpu_timer() == timer) {
                found = true;
                break;
            }
        }
        if (!found) {
            log_performance->trace("Added new GPU timer plot {}", timer->label());
            m_gpu_timer_plots.emplace_back(timer);
        }
    }
    m_gpu_timer_plots.erase(
        std::remove_if(
            m_gpu_timer_plots.begin(),
            m_gpu_timer_plots.end(),
            [&all_gpu_timers](auto& plot) -> bool
            {
                bool found{false};
                for (auto* timer : all_gpu_timers) {
                    if (plot.gpu_timer() == timer) {
                        found = true;
                        break;
                    }
                }
                const bool do_remove = !found;
                if (do_remove) {
                    log_performance->trace("Removed old GPU timer plot");
                }
                return do_remove;
            }
        ),
        m_gpu_timer_plots.end()
    );

    const auto all_cpu_timers = erhe::toolkit::Timer::all_timers();
    for (auto* timer : all_cpu_timers) {
        bool found{false};
        for (auto& plot : m_cpu_timer_plots) {
            if (plot.timer() == timer) {
                found = true;
                break;
            }
        }
        if (!found) {
            log_performance->trace("Added new CPU timer plot {}", timer->label());
            m_cpu_timer_plots.emplace_back(timer);
        }
    }
    m_cpu_timer_plots.erase(
        std::remove_if(
            m_cpu_timer_plots.begin(),
            m_cpu_timer_plots.end(),
            [&all_cpu_timers](auto& plot) -> bool
            {
                bool found{false};
                for (auto* timer : all_cpu_timers) {
                    if (plot.timer() == timer) {
                        found = true;
                        break;
                    }
                }
                const bool do_remove = !found;
                if (do_remove) {
                    log_performance->trace("Removed old CPU timer plot");
                }
                return do_remove;
            }
        ),
        m_cpu_timer_plots.end()
    );

    if (ImGui::Checkbox("Pause", &m_pause)) {
        if (!m_pause) {
            m_frame_time_plot.sample();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Button("Clear")) {
        m_frame_time_plot.clear();
        for (auto& plot : m_cpu_timer_plots) {
            plot.clear();
        }
        for (auto& plot : m_gpu_timer_plots) {
            plot.clear();
        }
    }

    if (!m_pause) {
        m_frame_time_plot.sample();
        for (auto& plot : m_cpu_timer_plots) {
            plot.sample();
        }
        for (auto& plot : m_gpu_timer_plots) {
            plot.sample();
        }
    }

    m_frame_time_plot.imgui();
    for (auto& plot : m_cpu_timer_plots) {
        plot.imgui();
    }
    for (auto& plot : m_gpu_timer_plots) {
        plot.imgui();
    }
#endif
}

} // namespace erhe::application
