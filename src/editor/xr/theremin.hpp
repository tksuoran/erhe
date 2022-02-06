#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/optional.hpp"

#include <glm/glm.hpp>
#include <miniaudio.h>

#include <chrono>
#include <memory>

namespace erhe::scene {
    class Mesh;
}

namespace erhe::primitive {
    class Material;
}

namespace editor
{

class Hand_tracker;
class Headset_renderer;
class Line_renderer_set;
class Pointer_context;
class Scene_root;
class Text_renderer;

class Theremin
    : public erhe::components::Component
    , public Tool
    , public Rendertarget_imgui_window
{
public:
    static constexpr std::string_view c_name       {"Theremin_tool"};
    static constexpr std::string_view c_description{"Theremin tool"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Theremin();
    ~Theremin() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    void audio_data_callback(
        ma_device*  pDevice,
        void*       pOutput,
        const void* pInput,
        ma_uint32   frameCount
    );

    void generate(
        float*         output,
        const uint32_t sample_count,
        const float    waveform_length_in_samples,
        const float    phase
    );

    void set_antenna_distance(const float distance);

private:
    void create_gui_quad();
    void update_grid_color() const;
    auto normalized_finger_distance() const -> float;

    // Component dependencies
    std::shared_ptr<Hand_tracker     > m_hand_tracker;
    std::shared_ptr<Headset_renderer > m_headset_renderer;
    std::shared_ptr<Line_renderer_set> m_line_renderer_set;

    bool                    m_enable_audio          {false};   // master on/off switch
    float                   m_antenna_distance      {0.0f};   // closest point of right hand to the frequency antenna
    float                   m_antenna_distance_scale{100.0f}; // adjusts right hand antenna distance scaling
    nonstd::optional<float> m_left_finger_distance;           // left hand distance between thumb and index finger tips
    nonstd::optional<float> m_right_finger_distance;          // right hand distance between thumb and index finger tips
    float                   m_finger_distance_min  {1.0f};    // minimum distance for thumb and index finger tips
    float                   m_finger_distance_max  {7.0f};    // maximum distance for thumb and index finger tips
    float                   m_volume               {1.0f};
    float                   m_frequency            {440.0f};
    bool                    m_snap_to_note         {true}; // snap frequency to nearest note
    float                   m_phase                {0.0f};
    bool                    m_audio_ok             {false};
    ma_device_config        m_audio_config;
    ma_device               m_audio_device;
    std::vector<float>      m_wavetable; // for visualization

    nonstd::optional<
        std::chrono::steady_clock::time_point
    >                    m_right_hold_start_time;
    bool                 m_right_click{false};

};

} // namespace editor
