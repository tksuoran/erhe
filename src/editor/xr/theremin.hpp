#pragma once

#include "renderable.hpp"
#include "erhe_imgui/imgui_window.hpp"

// #include <miniaudio.h>

#include <chrono>
#include <optional>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class Hand_tracker;

class Theremin
    : public erhe::imgui::Imgui_window
    , public Renderable
{
public:
    Theremin(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Hand_tracker&                hand_tracker,
        App_context&                 app_context
    );

    // Implements Renderable
    void render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    //// void audio_data_callback(
    ////     ma_device*  pDevice,
    ////     void*       pOutput,
    ////     const void* pInput,
    ////     ma_uint32   frameCount
    //// );

    void generate(
        float*   output,
        uint32_t sample_count,
        float    waveform_length_in_samples,
        float    phase
    );

    void set_antenna_distance(float distance);

private:
    auto normalized_finger_distance() const -> float;

    App_context& m_context;

    bool                 m_enable_audio          {false};  // master on/off switch
    float                m_antenna_distance      {0.0f};   // closest point of right hand to the frequency antenna
    float                m_antenna_distance_scale{100.0f}; // adjusts right hand antenna distance scaling
    std::optional<float> m_left_finger_distance;           // left hand distance between thumb and index finger tips
    std::optional<float> m_right_finger_distance;          // right hand distance between thumb and index finger tips
    float                m_finger_distance_min  {1.0f};    // minimum distance for thumb and index finger tips
    float                m_finger_distance_max  {7.0f};    // maximum distance for thumb and index finger tips
    float                m_volume               {1.0f};
    float                m_frequency            {440.0f};
    bool                 m_snap_to_note         {true}; // snap frequency to nearest note
    float                m_phase                {0.0f};
    bool                 m_audio_ok             {false};
    //// ma_device_config   m_audio_config;
    //// ma_device          m_audio_device;
    std::vector<float>   m_wavetable; // for visualization

    std::optional<
        std::chrono::steady_clock::time_point
    >                    m_right_hold_start_time;
    bool                 m_right_click{false};

};

}
