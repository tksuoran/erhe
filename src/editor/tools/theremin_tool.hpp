#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <glm/glm.hpp>
#include <miniaudio.h>

#include <memory>
#include <optional>

namespace erhe::scene {
    class Mesh;
}

namespace erhe::primitive {
    class Material;
}

namespace editor
{

class Line_renderer;
class Pointer_context;
class Scene_root;
class Text_renderer;

class Theremin_tool
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
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

    Theremin_tool ();
    ~Theremin_tool() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;

    // Implements Imgui_window
    void imgui() override;

    void audio_data_callback(
        ma_device*  pDevice,
        void*       pOutput,
        const void* pInput,
        ma_uint32   frameCount
    );

    void generate(float* output, const uint32_t sample_count, const float waveform_length_in_samples, const float phase);

    void set_left_distance (float distance);
    void set_right_distance(float distance);

private:
    bool             m_enable_audio        {false};
    float            m_left_distance       {0.0f};
    float            m_right_distance      {0.0f};
    float            m_left_distance_scale {2.0f};
    float            m_right_distance_scale{100.0f};
    float            m_volume              {1.0f};
    float            m_frequency           {440.0f};
    float            m_phase               {0.0f};
    float            m_pow                 {1.0f};
    float            m_sine_weight         {1.0f};
    float            m_square_weight       {0.0f};
    float            m_triangle_weight     {0.0f};

    bool             m_audio_ok           {false};
    ma_device_config m_audio_config;
    ma_device        m_audio_device;

    std::vector<float> m_wavetable;
};

} // namespace editor
