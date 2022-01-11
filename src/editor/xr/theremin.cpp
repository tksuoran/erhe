#include "theremin.hpp"
#include "editor_tools.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "renderers/line_renderer.hpp"

#include "xr/gradients.hpp"
#include "xr/hand_tracker.hpp"
#include "xr/headset_renderer.hpp"

#include "erhe/toolkit/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <imgui.h>

#include <string>

namespace editor
{

using namespace erhe::primitive;
using namespace erhe::geometry;

namespace {

void miniaudio_data_callback(
    ma_device*  pDevice,
    void*       pOutput,
    const void* pInput,
    ma_uint32   frameCount
)
{
    auto* theremin = reinterpret_cast<Theremin*>(pDevice->pUserData);
    if (theremin != nullptr)
    {
        theremin->audio_data_callback(pDevice, pOutput, pInput, frameCount);
    }
}

} // anonymous namespace

Theremin::Theremin()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
    , m_audio_config             {}
    , m_audio_device             {}
{
}

Theremin::~Theremin()
{
    ma_device_uninit(&m_audio_device);
}

auto Theremin::description() -> const char*
{
    return c_description.data();
}

void Theremin::connect()
{
    m_hand_tracker      = get<Hand_tracker     >();
    m_headset_renderer  = get<Headset_renderer >();
    m_line_renderer_set = get<Line_renderer_set>();
    require<Editor_tools>();
}

void Theremin::initialize_component()
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

    if (m_enable_audio && m_audio_ok)
    {
        ma_device_start(&m_audio_device);
    }

    //hide();
}

void Theremin::set_left_distance(float distance)
{
    m_left_distance = distance;
    //m_volume = m_left_distance_scale * distance;
}

void Theremin::set_right_distance(float distance)
{
    m_right_distance = distance;
    m_frequency = m_right_distance_scale / distance;
}

void Theremin::audio_data_callback(
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

auto Theremin::normalized_finger_distance() const -> float
{
    if (!m_left_finger_distance.has_value())
    {
        return 1.0f; // max distance
    }

    const float t  = m_left_finger_distance.value(); // m to cm
    const float t0 = m_finger_distance_min; // cm
    const float t1 = m_finger_distance_max; // cm
    if (t < t0)
    {
        return 0.0f;
    }
    if (t > t1)
    {
        return 1.0f;
    }
    const float result = (t - t0) / (t1 - t0);
    return result;
 }

void Theremin::generate(
    float*         output,
    const uint32_t sample_count,
    const float    waveform_length_in_samples,
    const float    phase
)
{
    const float volume  = m_volume * (1.0f - normalized_finger_distance());
    //const float normalize = 1.0f / (m_sine_weight + m_square_weight + m_triangle_weight);
    for (uint32_t sample = 0; sample < sample_count; ++sample)
    {
        const float rel  = phase + static_cast<float>(sample) / waveform_length_in_samples;
        const float t    = glm::two_pi<float>() * rel;
        const float sine = std::sin(t);
        //const float square         = sine < 0.0f ? -1.0f : 1.0f;
        //const float triangle       = 2.0f * (rel - std::floor(rel)) - 1.0f;
        //const float base_value     = (m_sine_weight * sine + m_square_weight * square + m_triangle_weight * triangle) * normalize;
        //const float sign           = (base_value < 0.0f) ? -1.0f : 1.0f;
        //const float abs_value      = std::abs(base_value);
        //const float pow_value      = sign * std::pow(abs_value, m_pow);
        *output++ = volume * sine;
    }
}

enum class Note : unsigned int
{
    c = 0,
    c_sharp, // d_flat
    d,
    d_sharp, // e_flat
    e,
    f,
    f_sharp, // g_flat
    g,
    g_sharp, // a_flat
    a,
    a_sharp, // b_flat
    b
};

class Note_and_octave
{
public:
    Note note;
    int  octave;
};

auto from_midi(int midi_note) -> Note_and_octave
{
    int note   = midi_note % 12;
    int octave = (midi_note / 12) - 1;

    switch (note)
    {
        case  0: return { Note::c,       octave };
        case  1: return { Note::c_sharp, octave };
        case  2: return { Note::d,       octave };
        case  3: return { Note::d_sharp, octave };
        case  4: return { Note::e,       octave };
        case  5: return { Note::f,       octave };
        case  6: return { Note::f_sharp, octave };
        case  7: return { Note::g,       octave };
        case  8: return { Note::g_sharp, octave };
        case  9: return { Note::a,       octave };
        case 10: return { Note::a_sharp, octave };
        case 11: return { Note::b,       octave };
        default: return { Note::a,       octave };
    }
}

auto get_note_color(int midi_note) -> uint32_t
{
    int note = midi_note % 12;

    switch (note)
    {
        case  0: return 0xff0000ff;
        case  1: return 0xff0088ff;
        case  2: return 0xff00ffff;
        case  3: return 0xff00ff88;
        case  4: return 0xff00ff00;
        case  5: return 0xff88ff00;
        case  6: return 0xffffff00;
        case  7: return 0xffff8800;
        case  8: return 0xffff0000;
        case  9: return 0xffff0088;
        case 10: return 0xffff00ff;
        case 11: return 0xff8800ff;
        default: return 0xff888888;
    }
}

auto c_str(Note note) -> const char*
{
    switch (note)
    {
        using enum Note;
        case c:       return "C";
        case c_sharp: return "C#";
        case d:       return "D";
        case d_sharp: return "D#";
        case e:       return "E";
        case f:       return "F";
        case f_sharp: return "F#";
        case g:       return "G";
        case g_sharp: return "G#";
        case a:       return "A";
        case a_sharp: return "A#";
        case b:       return "B";
        default: return "?";
    }
}

auto midi_note_to_frequency(int p) -> float
{
    const double f = std::pow(
        2.0,
        static_cast<double>(p - 69) / 12.0
    ) * 440.0;
    return static_cast<float>(f);
}

auto frequency_to_midi_note(float f) -> int
{
    const double p = 69 + 12 * std::log2(f / 440.0);
    const double rp = std::round(p);
    return static_cast<int>(rp);
}
    
void Theremin::tool_render(const Render_context& context)
{
    static_cast<void>(context);

    if (!m_headset_renderer)
    {
        return;
    }

    auto&       line_renderer = m_line_renderer_set->hidden;
    const auto  camera        = m_headset_renderer->root_camera();

    if (!camera)
    {
        return;
    }

    const auto  transform     = camera->world_from_node();
    const auto& left_hand     = m_hand_tracker->get_hand(Hand_name::Left);
    const auto& right_hand    = m_hand_tracker->get_hand(Hand_name::Right);

    //constexpr uint32_t red        = 0xff0000ffu;
    //constexpr uint32_t half_red   = 0x88000088u;
    constexpr uint32_t green      = 0xff00ff00u;
    constexpr uint32_t half_green = 0x88008800u;
    //constexpr uint32_t blue       = 0xffff0000u;
    constexpr float    thickness  = 70.0f;

    const glm::vec3 left_p0{-0.15f, 0.0f, 0.0f};
    const glm::vec3 left_p1{-0.15f, 2.0f, 0.0f};
    //line_renderer.set_line_color(red);
    //line_renderer.add_lines( { { left_p0, left_p1 } }, thickness );

    const glm::vec3 right_p0{0.15f, 0.0f, 0.0f};
    const glm::vec3 right_p1{0.15f, 2.0f, 0.0f};
    line_renderer.set_line_color(green);
    line_renderer.add_lines( { { right_p0, right_p1 } }, thickness );

    m_left_finger_distance = left_hand.distance(XR_HAND_JOINT_THUMB_TIP_EXT, XR_HAND_JOINT_INDEX_TIP_EXT);

    // From m to cm
    if (m_left_finger_distance.has_value())
    {
        m_left_finger_distance = m_left_finger_distance.value() * 100.0f;
    }
    //const float volume = m_volume * (1.0f - normalized_finger_distance());
    const auto finger_distance_color = gradient::cubehelix.get(1.0f - normalized_finger_distance());
    m_hand_tracker->set_left_hand_color(
        ImGui::ColorConvertFloat4ToU32(
            ImVec4{
                finger_distance_color.x,
                finger_distance_color.y,
                finger_distance_color.z,
                1.0f
            }
        )
    );

    if (left_hand.is_active())
    {
        const auto left_closest_point = left_hand.get_closest_point_to_line(
            transform,
            left_p0,
            left_p1
        );
        if (left_closest_point.has_value())
        {
            const auto  P = left_closest_point.value().P;
            const auto  Q = left_closest_point.value().Q;
            const float d = glm::distance(P, Q);
            //const float s = 0.085f / d;
            //line_renderer.set_line_color(gradient::viridis.get(s));
            //line_renderer.set_line_color(half_red);
            //line_renderer.add_lines( { { P, Q } }, thickness );
            set_left_distance(d);
        }
    }

    m_right_finger_distance = right_hand.distance(XR_HAND_JOINT_THUMB_TIP_EXT, XR_HAND_JOINT_INDEX_TIP_EXT);

    // From m to cm
    if (m_right_finger_distance.has_value())
    {
        m_right_finger_distance = m_right_finger_distance.value() * 100.0f;
    }

    if (right_hand.is_active())
    {
        const auto right_closest_point = right_hand.get_closest_point_to_line(
            transform,
            right_p0,
            right_p1
        );
        if (right_closest_point.has_value())
        {
            const auto  P = right_closest_point.value().P;
            const auto  Q = right_closest_point.value().Q;
            const float d = glm::distance(P, Q);
            //const float s = 0.085f / d;
            //line_renderer.set_line_color(gradient::temperature.get(s));
            line_renderer.set_line_color(half_green);
            line_renderer.add_lines( { { P, Q } }, thickness );
            set_right_distance(d);
        }
    }
}

void Theremin::imgui()
{
    if (m_audio_ok)
    {
        const bool enable_changed = ImGui::Checkbox("Enable", &m_enable_audio);
        if (m_left_finger_distance.has_value())
        {
            ImGui::Text(
                "Left Finger Tip Distance: %.1f cm",
                m_left_finger_distance.value()
            );
        }
        else
        {
            ImGui::TextColored(
                ImVec4{1.0f, 0.3f, 0.2f, 1.0f},
                "Left Finger Tip Distance: Not tracked"
            );
        }
        //if (m_right_finger_distance.has_value())
        //{
        //    ImGui::Text("Right Finger Distance: %.1f cm", m_right_finger_distance.value());
        //}
        //else
        //{
        //    ImGui::TextColored(
        //        ImVec4{1.0f, 0.3f, 0.2f, 1.0f},
        //        "Right Finger Distance: Not tracked"
        //    );
        //}
        ImGui::SliderFloat("Min Left Finger Tip Distance", &m_finger_distance_min, 0.5f, 30.0f, "%.1f cm"); // cm
        ImGui::SliderFloat("Max Left Finger Tip Distance", &m_finger_distance_max, 0.5f, 30.0f, "%.1f cm"); // cm
        if (enable_changed)
        {
            if (m_enable_audio && m_audio_ok)
            {
                ma_device_start(&m_audio_device);
            }
            else
            {
                ma_device_stop(&m_audio_device);
            }
        }

        //ImGui::InputFloat("Left Distance",        &m_left_distance, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
        ImGui::InputFloat("Right Hand Distance",       &m_right_distance, 0.0f, 0.0f, "%.3f m", ImGuiInputTextFlags_ReadOnly);
        //ImGui::InputFloat("Left Distance Scale",  &m_left_distance_scale);
        ImGui::InputFloat("Right Hanb Distance Scale", &m_right_distance_scale);
        bool any = false;
        any = ImGui::DragFloat("Frequency",       &m_frequency, 0.0f, 5.0f, 2000.0f, "%.1f Hz") || any;
        const int  midi_note = frequency_to_midi_note(m_frequency);
        const auto note      = from_midi(midi_note);
        ImGui::Checkbox("Snap to Note", &m_snap_to_note);
        if (m_snap_to_note)
        {
            m_frequency = midi_note_to_frequency(midi_note);
        }
        m_hand_tracker->set_right_hand_color(get_note_color(midi_note));
        ImGui::Text("Note: %s-%d", c_str(note.note), note.octave);
        any = ImGui::SliderFloat("Volume",          &m_volume,            0.0f,    1.0f) || any;
        //any = ImGui::SliderFloat("Sine Weight",     &m_sine_weight,      -5.0f,    5.0f) || any;
        //any = ImGui::SliderFloat("Square Weight",   &m_square_weight,    -5.0f,    5.0f) || any;
        //any = ImGui::SliderFloat("Triangle Weight", &m_triangle_weight,  -5.0f,    5.0f) || any;
        //any = ImGui::SliderFloat("Pow",             &m_pow,             -10.0f, 2000.0f, "%.4f", ImGuiSliderFlags_Logarithmic) || any;
        //if (any || m_wavetable.empty())
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
