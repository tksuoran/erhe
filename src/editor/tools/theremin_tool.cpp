#include "theremin_tool.hpp"
#include "editor_tools.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "erhe/toolkit/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <imgui.h>

#include <string>

namespace editor
{

using namespace erhe::primitive;
using namespace erhe::geometry;

Theremin_tool::Theremin_tool()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
{
}

Theremin_tool::~Theremin_tool()
{
    ma_device_uninit(&m_audio_device);
}

auto Theremin_tool::description() -> const char*
{
    return c_description.data();
}

void Theremin_tool::connect()
{
    require<Editor_tools>();
}

void miniaudio_data_callback(
    ma_device*  pDevice,
    void*       pOutput,
    const void* pInput,
    ma_uint32   frameCount
)
{
    auto* theremin_tool = reinterpret_cast<Theremin_tool*>(pDevice->pUserData);
    if (theremin_tool != nullptr)
    {
        theremin_tool->audio_data_callback(pDevice, pOutput, pInput, frameCount);
    }
}

void Theremin_tool::set_left_distance(float distance)
{
    m_left_distance = distance;
    m_volume = m_left_distance_scale * distance;
}

void Theremin_tool::set_right_distance(float distance)
{
    m_right_distance = distance;
    m_frequency = m_right_distance_scale / distance;
}

void Theremin_tool::audio_data_callback(
    ma_device*  pDevice,
    void*       pOutput,
    const void* pInput,
    ma_uint32   frameCount
)
{
    static_cast<void>(pDevice);
    static_cast<void>(pInput);
    auto* audio_out = reinterpret_cast<float*>(pOutput);
    const float waveform_length_in_samples = static_cast<float>(m_audio_config.sampleRate) / m_frequency;
    generate(audio_out, frameCount, waveform_length_in_samples, m_phase);
    m_phase += static_cast<float>(frameCount) / waveform_length_in_samples;
}

void Theremin_tool::generate(float* output, const uint32_t sample_count, const float waveform_length_in_samples, const float phase)
{
    const float normalize = 1.0f / (m_sine_weight + m_square_weight + m_triangle_weight);
    for (uint32_t sample = 0; sample < sample_count; ++sample)
    {
        const float rel            = phase + static_cast<float>(sample) / waveform_length_in_samples;
        const float t              = glm::two_pi<float>() * rel;
        const float sine           = std::sin(t);
        const float square         = sine < 0.0f ? -1.0f : 1.0f;
        const float triangle       = 2.0f * (rel - std::floor(rel)) - 1.0f;
        const float base_value     = (m_sine_weight * sine + m_square_weight * square + m_triangle_weight * triangle) * normalize;
        const float sign           = (base_value < 0.0f) ? -1.0f : 1.0f;
        const float abs_value      = std::abs(base_value);
        const float pow_value      = sign * std::pow(abs_value, m_pow);
        *output++ = m_volume * pow_value;
    }
}

void Theremin_tool::initialize_component()
{
    get<Editor_tools>()->register_background_tool(this);

    m_audio_config = ma_device_config_init(ma_device_type_playback);
    m_audio_config.playback.format   = ma_format_f32;
    m_audio_config.playback.channels = 1;
    m_audio_config.sampleRate        = 48000;
    m_audio_config.dataCallback      = miniaudio_data_callback;
    m_audio_config.pUserData         = this;

    if (ma_device_init(nullptr, &m_audio_config, &m_audio_device) == MA_SUCCESS)
    {
        m_audio_ok = true;
    }

    hide();
}

void Theremin_tool::imgui()
{
    if (m_audio_ok)
    {
        if (ImGui::Checkbox("Enable", &m_enable_audio))
        {
            if (m_enable_audio)
            {
                ma_device_start(&m_audio_device);
            }
            else
            {
                ma_device_stop(&m_audio_device);
            }
        }
        ImGui::InputFloat("Left Distance",        &m_left_distance);
        ImGui::InputFloat("Right Distance",       &m_right_distance);
        ImGui::InputFloat("Left Distance Scale",  &m_left_distance_scale);
        ImGui::InputFloat("Right Distance Scale", &m_right_distance_scale);
        bool any = false;
        any = ImGui::SliderFloat("Frequency",       &m_frequency,         5.0f, 2000.0f) || any;
        any = ImGui::SliderFloat("Volume",          &m_volume,            0.0f,    1.0f) || any;
        any = ImGui::SliderFloat("Sine Weight",     &m_sine_weight,      -5.0f,    5.0f) || any;
        any = ImGui::SliderFloat("Square Weight",   &m_square_weight,    -5.0f,    5.0f) || any;
        any = ImGui::SliderFloat("Triangle Weight", &m_triangle_weight,  -5.0f,    5.0f) || any;
        any = ImGui::SliderFloat("Pow",             &m_pow,             -10.0f, 2000.0f, "%.4f", ImGuiSliderFlags_Logarithmic) || any;
        if (any || m_wavetable.empty())
        {
            m_wavetable.resize(500);
            const auto waveform_length_in_samples = static_cast<float   >(m_wavetable.size());
            const auto sample_count               = static_cast<uint32_t>(m_wavetable.size());
            generate(m_wavetable.data(), sample_count, waveform_length_in_samples, 0.0f);
        }
        if (!m_wavetable.empty())
        {
            ImGui::PlotLines(
                "Waveform",
                m_wavetable.data(),
                static_cast<int>(m_wavetable.size()),
                0,
                nullptr,
                -1.0f,
                1.0f,
                ImVec2{0, 100.0f}
            );
        }
    }
}


}
