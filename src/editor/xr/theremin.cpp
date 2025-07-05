#include "theremin.hpp"

#include "app_context.hpp"

#include "graphics/gradients.hpp"
#include "xr/hand_tracker.hpp"
#include "xr/headset_view.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>

namespace editor {

namespace {

enum class Note : unsigned int {
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

auto from_midi(const int midi_note) -> Note_and_octave
{
    const int note   = midi_note % 12;
    const int octave = (midi_note / 12) - 1;

    switch (note) {
        //using enum Note;
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

auto get_note_color(const int midi_note) -> uint32_t
{
    const int note = midi_note % 12;

    switch (note) {
        case  0: return 0xff4b19e6; // C  Red
        case  1: return 0xff000080; // C# Maroon
        case  2: return 0xff3182f5; // D  Orange
        case  3: return 0xff004080; // D# Apricot
        case  4: return 0xff19e1ff; // E  Yellow
        case  5: return 0xff4bd442; // F  Green
        case  6: return 0xff008000; // F# Mint
        case  7: return 0xffd86343; // G  Blue
        case  8: return 0xff750000; // G# Navy
        case  9: return 0xffe632f0; // A  Magenta
        case 10: return 0xff800080; // A# Purple
        case 11: return 0xffa9a9a9; // B  Gray
        default:
            return 0xffffffff; //
    }
}

auto c_str(const Note note) -> const char*
{
    switch (note) {
        //using enum Note;
        case Note::c:       return "C";
        case Note::c_sharp: return "C#";
        case Note::d:       return "D";
        case Note::d_sharp: return "D#";
        case Note::e:       return "E";
        case Note::f:       return "F";
        case Note::f_sharp: return "F#";
        case Note::g:       return "G";
        case Note::g_sharp: return "G#";
        case Note::a:       return "A";
        case Note::a_sharp: return "A#";
        case Note::b:       return "B";
        default: return "?";
    }
}

auto midi_note_to_frequency(const int p) -> float
{
    return std::pow(
        2.0f,
        static_cast<float>(p - 69) / 12.0f
    ) * 440.0f;
}

auto frequency_to_midi_note(const float f) -> int
{
    const float p = 69.0f + 12.0f * std::log2(static_cast<float>(f) / 440.0f);
    const float rp = std::round(p);
    return static_cast<int>(rp);
}

//// void miniaudio_data_callback(
////     ma_device*  pDevice,
////     void*       pOutput,
////     const void* pInput,
////     ma_uint32   frameCount
//// ) {
////     auto* theremin = reinterpret_cast<Theremin*>(pDevice->pUserData);
////     if (theremin != nullptr) {
////         theremin->audio_data_callback(pDevice, pOutput, pInput, frameCount);
////     }
//// }

} // anonymous namespace

Theremin::Theremin(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Hand_tracker&                hand_tracker,
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Theremin", "theremin"}
    , m_context{app_context}
    //// , m_audio_config             {}
    //// , m_audio_device             {}
{
    //// m_audio_config = ma_device_config_init(ma_device_type_playback);
    //// m_audio_config.playback.format   = ma_format_f32;
    //// m_audio_config.playback.channels = 1;
    //// m_audio_config.sampleRate        = 48000;
    //// m_audio_config.dataCallback      = miniaudio_data_callback;
    //// m_audio_config.pUserData         = this;
    ////
    //// if (ma_device_init(nullptr, &m_audio_config, &m_audio_device) == MA_SUCCESS) {
    ////     m_audio_ok = true;
    //// }
    ////
    //// if (m_enable_audio && m_audio_ok) {
    ////     ma_device_start(&m_audio_device);
    //// }

    hand_tracker.set_color(Hand_name::Left, Finger_name::thumb,  ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    hand_tracker.set_color(Hand_name::Left, Finger_name::index,  ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    hand_tracker.set_color(Hand_name::Left, Finger_name::middle, ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    hand_tracker.set_color(Hand_name::Left, Finger_name::ring,   ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    hand_tracker.set_color(Hand_name::Left, Finger_name::little, ImVec4{0.3f, 0.3f, 0.3f, 1.0f});

    //// Register renderable
    //// get<Tools>()->register_background_tool(this);
}

void Theremin::set_antenna_distance(const float distance)
{
    m_antenna_distance = distance;
    m_frequency = m_antenna_distance_scale / distance;
    if (m_snap_to_note) {
        const int midi_note = frequency_to_midi_note(m_frequency);
        m_frequency = midi_note_to_frequency(midi_note);
    }
}

//// void Theremin::audio_data_callback(
////     ma_device*  pDevice,
////     void*       pOutput,
////     const void* pInput,
////     ma_uint32   frameCount
//// )
//// {
////     static_cast<void>(pDevice);
////     static_cast<void>(pInput);
////     auto* audio_out = reinterpret_cast<float*>(pOutput);
////     const float waveform_length_in_samples = static_cast<float>(m_audio_config.sampleRate) / m_frequency;
////     generate(audio_out, frameCount, waveform_length_in_samples, m_phase);
////     m_phase += static_cast<float>(frameCount) / waveform_length_in_samples;
//// }

auto Theremin::normalized_finger_distance() const -> float
{
    if (!m_left_finger_distance.has_value()) {
        return 1.0f; // max distance
    }

    const float t  = m_left_finger_distance.value(); // m to cm
    const float t0 = m_finger_distance_min; // cm
    const float t1 = m_finger_distance_max; // cm
    if (t < t0) {
        return 0.0f;
    }
    if (t > t1) {
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
    const float volume = m_volume * (1.0f - normalized_finger_distance());
    for (uint32_t sample = 0; sample < sample_count; ++sample) {
        const float rel  = phase + static_cast<float>(sample) / waveform_length_in_samples;
        const float t    = glm::two_pi<float>() * rel;
        const float sine = std::sin(t);
        *output++ = volume * sine;
    }
}

void Theremin::render(const Render_context& context)
{
    static_cast<void>(context);

    if (!m_enable_audio) {
        return;
    }

    erhe::renderer::Primitive_renderer line_renderer = m_context.debug_renderer->get(2, true, false);

    const auto& root_node = m_context.headset_view->get_root_node();
    if (!root_node) {
        return;
    }

    const auto  transform  = root_node->world_from_node();
    const auto& left_hand  = m_context.hand_tracker->get_hand(Hand_name::Left);
    const auto& right_hand = m_context.hand_tracker->get_hand(Hand_name::Right);

    constexpr glm::vec4 green     {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 half_green{0.0f, 0.5f, 0.0f, 0.5f};

    const glm::vec3 antenna_p0{0.0f, 0.0f, 0.0f};
    const glm::vec3 antenna_p1{0.0f, 2.0f, 0.0f};
    line_renderer.set_line_color(green);
    line_renderer.set_thickness(10.0f);
    line_renderer.add_lines( { { antenna_p0, antenna_p1 } });

    m_left_finger_distance = left_hand.distance(
        XR_HAND_JOINT_THUMB_TIP_EXT,
        XR_HAND_JOINT_INDEX_TIP_EXT
    );

    // From m to cm
    if (m_left_finger_distance.has_value()) {
        m_left_finger_distance = m_left_finger_distance.value() * 100.0f;
    }

    const auto finger_distance_color = gradient::velocity_blue.get(
        1.0f - normalized_finger_distance()
    );
    const auto volume_color = ImVec4{
        finger_distance_color.x,
        finger_distance_color.y,
        finger_distance_color.z,
        1.0f
    };
    m_context.hand_tracker->set_color(Hand_name::Left, Finger_name::thumb, volume_color);
    m_context.hand_tracker->set_color(Hand_name::Left, Finger_name::index, volume_color);

    m_right_finger_distance = right_hand.distance(
        XR_HAND_JOINT_THUMB_TIP_EXT,
        XR_HAND_JOINT_INDEX_TIP_EXT
    );

    // From m to cm
    if (m_right_finger_distance.has_value()) {
        m_right_finger_distance = m_right_finger_distance.value() * 100.0f;
        if (m_right_finger_distance < 1.3f) {
            if (!m_right_hold_start_time.has_value()) {
                m_right_hold_start_time = std::chrono::steady_clock::now();
            } else if (!m_right_click) {
                const auto duration = std::chrono::steady_clock::now() - m_right_hold_start_time.value();
                if (duration > std::chrono::milliseconds(50)) {
                    m_right_click = true;

                    m_snap_to_note = !m_snap_to_note;
                }
            }
        } else if (m_right_click) {
            m_right_click = false;
            m_right_hold_start_time.reset();
        }
    } else {
        m_right_click = false;
        m_right_hold_start_time.reset();
    }

    if (right_hand.is_active()) {
        const auto right_closest_finger_opt = right_hand.get_closest_point_to_line(
            transform,
            antenna_p0,
            antenna_p1
        );
        if (right_closest_finger_opt.has_value()) {
            const auto  closest = right_closest_finger_opt.value();
            const auto  finger  = closest.finger;
            const auto  P       = closest.finger_point;
            const auto  Q       = closest.point;
            const float d       = glm::distance(P, Q);
            line_renderer.set_line_color(half_green);
            line_renderer.set_thickness(2.0f);
            line_renderer.add_lines( { { P, Q } } );
            set_antenna_distance(d);
            for (
                std::size_t i = Finger_name::thumb;
                i <= Finger_name::little;
                ++i
            ) {
                m_context.hand_tracker->set_color(
                    Hand_name::Right,
                    i,
                    (i == finger)
                        ?
                            ImGui::ColorConvertU32ToFloat4(
                                get_note_color(
                                    frequency_to_midi_note(m_frequency)
                                )
                            )
                        : ImVec4{0.35f, 0.35f, 0.35f, 1.0f}
                );
            }
        }
    }
}

void Theremin::imgui()
{
    if (m_audio_ok) {
        const bool enable_changed = ImGui::Checkbox("Enable", &m_enable_audio);
        if (m_left_finger_distance.has_value()) {
            ImGui::Text(
                "Left Finger Tip Distance: %.1f cm",
                m_left_finger_distance.value()
            );
        } else {
            ImGui::TextColored(
                ImVec4{1.0f, 0.3f, 0.2f, 1.0f},
                "Left Finger Tip Distance: Not tracked"
            );
        }
        ImGui::SliderFloat("Min Finger Tip Distance", &m_finger_distance_min, 0.5f, 30.0f, "%.1f cm");
        ImGui::SliderFloat("Max Finger Tip Distance", &m_finger_distance_max, 0.5f, 30.0f, "%.1f cm");
        if (enable_changed) {
            if (m_enable_audio && m_audio_ok) {
                //// ma_device_start(&m_audio_device);
            } else {
                //// ma_device_stop(&m_audio_device);
            }
        }

        ImGui::InputFloat(
            "Frequency Antenna Hand Distance",
            &m_antenna_distance,
            0.0f,
            0.0f,
            "%.3f m",
            ImGuiInputTextFlags_ReadOnly
        );

        ImGui::InputFloat(
            "Frequency Antenna Distance Scale",
            &m_antenna_distance_scale
        );

        ImGui::DragFloat(
            "Frequency",
            &m_frequency,
            0.0f,
            5.0f,
            2000.0f,
            "%.1f Hz"
        );

        const int  midi_note = frequency_to_midi_note(m_frequency);
        const auto note      = from_midi(midi_note);

        ImGui::Checkbox("Snap to Note", &m_snap_to_note);

        ImGui::Text("Note: %s-%d", c_str(note.note), note.octave);
        ImGui::SliderFloat("Volume", &m_volume, 0.0f, 1.0f);

        {
            m_wavetable.resize(500);
            const auto waveform_length_in_samples = static_cast<float   >(m_wavetable.size());
            const auto sample_count               = static_cast<uint32_t>(m_wavetable.size());
            generate(m_wavetable.data(), sample_count, waveform_length_in_samples, 0.0f);
        }
        if (!m_wavetable.empty()) {
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

} // namespace editor
