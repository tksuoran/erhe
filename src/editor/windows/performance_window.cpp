#include "windows/performance_window.hpp"
#include "editor_imgui_windows.hpp"
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


auto factorial(int input) -> int
{
    int result = 1;
    for (int i = 1; i <= input; i++)
    {
        result = result * i;
    }
    return result;
}

// Computes the n-th coefficient from Pascal's triangle binomial coefficients.
auto binom(
    const int row_index,
    const int column_index = -1
) -> int
{
    return
        factorial(row_index) /
        (
            factorial(row_index - column_index) *
            factorial(column_index)
        );
}

class Kernel
{
public:
    std::vector<float> weights;
    std::vector<float> offsets;
};

// Compute discrete weights and factors
auto kernel_binom(
    const int taps,
    const int expand_by = 0,
    const int reduce_by = 0
) -> Kernel
{
    const auto row          = taps - 1 + (expand_by << 1);
    const auto coeffs_count = row + 1;
    const auto radius       = taps >> 1;

    // sanity check, avoid duped coefficients at center
    if ((coeffs_count & 1) == 0)
    {
        return {}; // ValueError("Duped coefficients at center")
    }

    // compute total weight
    // https://en.wikipedia.org/wiki/Power_of_two
    // TODO: seems to be not optimal ...
    int sum = 0;
    for (int x = 0; x < reduce_by; ++x)
    {
        sum += 2 * binom(row, x);
    }
    const auto total = float(1 << row) - sum;

    // compute final weights
    Kernel result;
    for (
        int x = reduce_by + radius;
        x > reduce_by - 1;
        --x
    )
    {
        result.weights.push_back(binom(row, x) / total);
    }
    for (
        int offset = 0;
        offset <= radius;
        ++offset
    )
    {
        result.offsets.push_back(static_cast<float>(offset));
    }
    return result;
}

// Compute linearly interpolated weights and factors
auto kernel_binom_linear(const Kernel& discrete_data) -> Kernel
{
    const auto& wd = discrete_data.weights;
    const auto& od = discrete_data.offsets;

    const int w_count = static_cast<int>(wd.size());

    // sanity checks
    const auto pairs = w_count - 1;
    if ((w_count & 1) == 0)
    {
        return {};
        //raise ValueError("Duped coefficients at center")
    }

    if ((pairs % 2 != 0))
    {
        return {};
        //raise ValueError("Can't perform bilinear reduction on non-paired texels")
    }

    Kernel result;
    result.weights.push_back(wd[0]);
    for (int x = 1; x < w_count - 1; x += 2)
    {
        result.weights.push_back(wd[x] + wd[x + 1]);
    }

    result.offsets.push_back(0);
    for (
        int x = 1;
        x < w_count - 1;
        x += 2
    )
    {
        int i = (x - 1) / 2;
        const float value =
            (
                od[x    ] * wd[x] +
                od[x + 1] * wd[x + 1]
            ) / result.weights[i + 1];
        result.offsets.push_back(value);
    }

    return result;
}


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

Plot::Plot(const std::string_view label, const size_t width)
    : m_label{label}
{
    m_values.resize(width);
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

void Plot::add(const float sample)
{
    m_values[m_offset % m_values.size()] = sample;
    m_value_count = std::min(m_value_count + 1, m_values.size());
    m_offset++;
}

namespace {

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs)
{
    return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
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
    const ImGuiID id = ImGui::GetID(m_label.c_str());

    const auto label_size = ImGui::CalcTextSize(m_label.c_str(), nullptr, true);
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
    //m_scale_max = std::max(m_scale_max, 0.2f);

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

        auto overlay_text = fmt::format("Max: {} ms", m_scale_max);
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
    ImGui::RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, inner_bb.Min.y), m_label.c_str());

    // Return hovered index or -1 if none are hovered.
    // This is currently not exposed in the public API because we need a larger redesign of the whole thing, but in the short-term we are making it available in PlotEx().
    //return idx_hovered;
}

void Performance_window::imgui()
{
    ImGui::DragInt("Taps",   &m_taps,   1.0f, 1, 32);
    ImGui::DragInt("Expand", &m_expand, 1.0f, 0, 32);
    ImGui::DragInt("Reduce", &m_reduce, 1.0f, 0, 32);
    ImGui::Checkbox("Linear", &m_linear);

    const auto discrete = kernel_binom(m_taps, m_expand, m_reduce);
    if (ImGui::TreeNodeEx("Discrete", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t i = 0; i < discrete.weights.size(); ++i)
        {
            ImGui::Text(
                "W: %.3f O: %.3f",
                discrete.weights.at(i),
                discrete.offsets.at(i)
            );
        }
        ImGui::TreePop();
    }
    if (m_linear)
    {
        const auto linear = kernel_binom_linear(discrete);
        if (ImGui::TreeNodeEx("Linear", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < linear.weights.size(); ++i)
            {
                ImGui::Text(
                    "W: %.3f O: %.3f",
                    linear.weights.at(i),
                    linear.offsets.at(i)
                );
            }
            ImGui::TreePop();
        }
    }

    ImGui::Checkbox("Pause", &m_pause);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Button("Clear"))
    {
        m_id_renderer_plot    .clear();
        m_imgui_renderer_plot .clear();
        m_shadow_renderer_plot.clear();
        m_content_plot        .clear();
        m_selection_plot      .clear();
        m_brush_plot          .clear();
        m_gui_plot            .clear();
        m_tools_plot          .clear();
    }
    //ImGui::SameLine();

    if (!m_pause)
    {
        m_id_renderer_plot    .add(static_cast<float>(m_id_renderer     ->gpu_time()));
        m_imgui_renderer_plot .add(static_cast<float>(m_imgui_renderer  ->gpu_time()));
        m_shadow_renderer_plot.add(static_cast<float>(m_shadow_renderer ->gpu_time()));
        m_content_plot        .add(static_cast<float>(m_editor_rendering->gpu_time_content  ()));
        m_selection_plot      .add(static_cast<float>(m_editor_rendering->gpu_time_selection()));
        m_brush_plot          .add(static_cast<float>(m_editor_rendering->gpu_time_brush    ()));
        m_gui_plot            .add(static_cast<float>(m_editor_rendering->gpu_time_gui      ()));
        m_tools_plot          .add(static_cast<float>(m_editor_rendering->gpu_time_tools    ()));
        //m_line_renderer_plot  .add(static_cast<float>(m_editor_rendering->lin
        //m_text_renderer_plot  .add(static_cast<float>(m_editor_rendering->
    }

    m_id_renderer_plot    .imgui();
    m_imgui_renderer_plot .imgui();
    m_shadow_renderer_plot.imgui();
    m_content_plot        .imgui();
    m_selection_plot      .imgui();
    m_brush_plot          .imgui();
    m_gui_plot            .imgui();
    m_tools_plot          .imgui();
}

}
