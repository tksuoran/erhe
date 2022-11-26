#pragma once

#include "tools/tool.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>
//// #include <miniaudio.h>

#include <chrono>
#include <memory>
#include <optional>

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::primitive
{
    class Material;
}

namespace erhe::application
{
    class Line_renderer_set;
    class Text_renderer;
}
namespace editor
{

class Hand_tracker;
class Headset_view;
class Pointer_context;
class Scene_root;

class Theremin
    : public erhe::components::Component
    , public erhe::application::Imgui_window
    , public Tool
{
public:
    static constexpr std::string_view c_type_name  {"Theremin_tool"};
    static constexpr std::string_view c_description{"Theremin tool"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Theremin ();
    ~Theremin() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    //// void audio_data_callback(
    ////     ma_device*  pDevice,
    ////     void*       pOutput,
    ////     const void* pInput,
    ////     ma_uint32   frameCount
    //// );

    void generate(
        float*         output,
        const uint32_t sample_count,
        const float    waveform_length_in_samples,
        const float    phase
    );

    void set_antenna_distance(const float distance);

private:
    auto normalized_finger_distance() const -> float;

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Hand_tracker                        > m_hand_tracker;
    std::shared_ptr<Headset_view                        > m_headset_view;

    bool                 m_enable_audio          {false};   // master on/off switch
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

} // namespace editor
