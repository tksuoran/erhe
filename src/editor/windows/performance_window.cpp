#include "windows/performance_window.hpp"
#include "editor_imgui_windows.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "graphics/gradients.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/imgui_renderer.hpp"
#include "renderers/shadow_renderer.hpp"

#include <gsl/gsl>
#include <imgui.h>
#include <imgui_internal.h>

namespace editor
{

Performance_window::Performance_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Performance_window::~Performance_window() = default;

void Performance_window::connect()
{
    m_editor_rendering = get<Editor_rendering>();
    m_id_renderer      = get<Id_renderer     >();
    m_imgui_renderer   = get<Imgui_renderer  >();
    m_shadow_renderer  = get<Shadow_renderer >();
}

void Performance_window::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);
}

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

Gpu_timer_plot::Gpu_timer_plot(
    erhe::graphics::Gpu_timer* timer,
    const size_t               width
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
    const size_t          width
)
    : m_timer{timer}
{
    m_values.resize(width);
}

void Cpu_timer_plot::sample()
{
    if (!m_timer->duration().has_value())
    {
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

void Plot::imgui()
{
    //ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
    {
        return;
    }

    //const ImGuiStyle& style = g.Style;
    const char* label_cstr = label();
    const ImGuiID id = ImGui::GetID(label_cstr);

    const auto label_size = ImGui::CalcTextSize(label_cstr, nullptr, true);
    // if (frame_size.x == 0.0f)
    // {
    //     frame_size.x = CalcItemWidth();
    // }
    // if (frame_size.y == 0.0f)
    // {
    //     frame_size.y = label_size.y + (style.FramePadding.y * 2);
    // }
    const auto& style      = ImGui::GetStyle();
    const auto  cursor_pos = window->DC.CursorPos;
    const auto& io = ImGui::GetIO();

    const ImRect frame_bb{cursor_pos, cursor_pos + m_frame_size};
    const ImRect inner_bb{frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding};
    const ImRect total_bb{ frame_bb.Min, frame_bb.Max + ImVec2{label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0}};
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, 0, &frame_bb))
    {
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
        )
        {
            const float v = m_values.at(i);
            if (v != v) // Ignore NaN values
            {
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

    //const size_t values_count_min = 2;
    //int idx_hovered = -1;
    //if (m_value_count >= values_count_min)
    {
        int res_w = std::min((int)(m_frame_size.x), (int)(m_values.size() - 1));
        int item_count = (int)(m_values.size() - 1);

        // Tooltip on hover
        const bool hovered = ImGui::ItemHoverable(frame_bb, id);
        if (hovered && inner_bb.Contains(io.MousePos))
        {
            const float  t0    = clamp((io.MousePos.x - inner_bb.Min.x) / (inner_bb.Max.x - inner_bb.Min.x), 0.0f, 0.9999f);
            const float  idx   = t0 * m_values.size();
            const size_t v_idx = (size_t)(idx);
            IM_ASSERT(v_idx < m_values.size());

            const float v0 = m_values.at((v_idx + m_offset) % m_values.size());
            const float v1 = m_values.at((v_idx + 1 + m_offset) % m_values.size());
            const float t1 = t0 - static_cast<size_t>(t0);
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

        const float t_step   = 1.0f / (float)res_w;
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

        for (int n = 0; n < res_w; n++)
        {
            const float  t1     = t0 + t_step;
            const int    v1_idx = (int)(t0 * item_count + 0.5f);
            const size_t idx    = (v1_idx + m_offset + 1) % m_values.size();
            const float  v1     = m_values.at(idx);
            const ImVec2 tp1 = ImVec2{t1, 1.0f - ImSaturate((v1 - m_scale_min) * inv_scale)};

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

        auto overlay_text = fmt::format("Max: {:.3f} ms", displayed_max);
        ImGui::RenderTextClipped(
            ImVec2{frame_bb.Min.x, frame_bb.Min.y + style.FramePadding.y},
            frame_bb.Max,
            overlay_text.c_str(),
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
}

void Performance_window::imgui()
{

    const auto all_gpu_timers = erhe::graphics::Gpu_timer::all_gpu_timers();
    for (auto* timer : all_gpu_timers)
    {
        bool found{false};
        for (auto& plot : m_gpu_timer_plots)
        {
            if (plot.gpu_timer() == timer)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            log_performance.info("Added new GPU timer plot {}\n", timer->label());
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
                for (auto* timer : all_gpu_timers)
                {
                    if (plot.gpu_timer() == timer)
                    {
                        found = true;
                        break;
                    }
                }
                const bool do_remove = !found;
                if (do_remove)
                {
                    log_performance.info("Removed old GPU timer plot\n");
                }
                return do_remove;
            }
        ),
        m_gpu_timer_plots.end()
    );

    const auto all_cpu_timers = erhe::toolkit::Timer::all_timers();
    for (auto* timer : all_cpu_timers)
    {
        bool found{false};
        for (auto& plot : m_cpu_timer_plots)
        {
            if (plot.timer() == timer)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            log_performance.info("Added new CPU timer plot {}\n", timer->label());
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
                for (auto* timer : all_cpu_timers)
                {
                    if (plot.timer() == timer)
                    {
                        found = true;
                        break;
                    }
                }
                const bool do_remove = !found;
                if (do_remove)
                {
                    log_performance.info("Removed old CPU timer plot\n");
                }
                return do_remove;
            }
        ),
        m_cpu_timer_plots.end()
    );

    ImGui::Checkbox("Pause", &m_pause);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Button("Clear"))
    {
        for (auto& plot : m_gpu_timer_plots)
        {
            plot.clear();
        }
    }

    if (!m_pause)
    {
        for (auto& plot : m_cpu_timer_plots)
        {
            plot.sample();
        }
        for (auto& plot : m_gpu_timer_plots)
        {
            plot.sample();
        }
    }

    for (auto& plot : m_cpu_timer_plots)
    {
        plot.imgui();
    }
    for (auto& plot : m_gpu_timer_plots)
    {
        plot.imgui();
    }
}

}
