#include "theremin.hpp"
#include "editor_imgui_windows.hpp"
#include "editor_tools.hpp"
#include "log.hpp"
#include "rendering.hpp"

#include "graphics/gl_context_provider.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_root.hpp"
#include "tools/grid_tool.hpp"

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

namespace {

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

auto from_midi(const int midi_note) -> Note_and_octave
{
    const int note   = midi_note % 12;
    const int octave = (midi_note / 12) - 1;

    switch (note)
    {
        using enum Note;
        case  0: return { c,       octave };
        case  1: return { c_sharp, octave };
        case  2: return { d,       octave };
        case  3: return { d_sharp, octave };
        case  4: return { e,       octave };
        case  5: return { f,       octave };
        case  6: return { f_sharp, octave };
        case  7: return { g,       octave };
        case  8: return { g_sharp, octave };
        case  9: return { a,       octave };
        case 10: return { a_sharp, octave };
        case 11: return { b,       octave };
        default: return { a,       octave };
    }
}

auto get_note_color(const int midi_note) -> uint32_t
{
    const int note = midi_note % 12;

    switch (note)
    {
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

auto midi_note_to_frequency(const int p) -> float
{
    const double f = std::pow(
        2.0,
        static_cast<double>(p - 69) / 12.0
    ) * 440.0;
    return static_cast<float>(f);
}

auto frequency_to_midi_note(const float f) -> int
{
    const double p = 69 + 12 * std::log2(static_cast<double>(f) / 440.0);
    const double rp = std::round(p);
    return static_cast<int>(rp);
}

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
    , Rendertarget_imgui_window  {c_description}
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
    m_hand_tracker      = require<Hand_tracker     >();
    m_headset_renderer  = get    <Headset_renderer >();
    m_line_renderer_set = get    <Line_renderer_set>();
    require<Editor_imgui_windows>();
    require<Editor_tools>();
    require<Gl_context_provider>();
    require<Mesh_memory>();
    require<Programs>();
    require<Grid_tool>();
}

void Theremin::initialize_component()
{
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

    update_grid_color();

    m_hand_tracker->set_color(Hand_name::Left, Finger_name::thumb,  ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    m_hand_tracker->set_color(Hand_name::Left, Finger_name::index,  ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    m_hand_tracker->set_color(Hand_name::Left, Finger_name::middle, ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    m_hand_tracker->set_color(Hand_name::Left, Finger_name::ring,   ImVec4{0.3f, 0.3f, 0.3f, 1.0f});
    m_hand_tracker->set_color(Hand_name::Left, Finger_name::little, ImVec4{0.3f, 0.3f, 0.3f, 1.0f});

    //create_gui_quad();

    get<Editor_tools>()->register_background_tool(this);
}

void Theremin::create_gui_quad()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    auto rendertarget = get<Editor_imgui_windows>()->create_rendertarget(
        "Theremin",
        1000,
        1000,
        1000.0
    );
    const auto placement = erhe::toolkit::create_look_at(
        glm::vec3{0.5f, 1.0f, 1.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f}
    );
    rendertarget->mesh_node()->set_parent_from_node(placement);

    rendertarget->register_imgui_window(this);
}

void Theremin::set_antenna_distance(const float distance)
{
    m_antenna_distance = distance;
    m_frequency = m_antenna_distance_scale / distance;
    if (m_snap_to_note)
    {
        const int midi_note = frequency_to_midi_note(m_frequency);
        m_frequency = midi_note_to_frequency(midi_note);
    }
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
    const float volume = m_volume * (1.0f - normalized_finger_distance());
    for (uint32_t sample = 0; sample < sample_count; ++sample)
    {
        const float rel  = phase + static_cast<float>(sample) / waveform_length_in_samples;
        const float t    = glm::two_pi<float>() * rel;
        const float sine = std::sin(t);
        *output++ = volume * sine;
    }
}

void Theremin::update_grid_color() const
{
    const auto& grid = get<Grid_tool>();
    if (m_snap_to_note)
    {
        grid->set_major_color(glm::vec4{0.265f, 0.265f, 0.95f, 1.0f});
        grid->set_minor_color(glm::vec4{0.035f, 0.035f, 0.35f, 1.0f});
    }
    else
    {
        grid->set_major_color(glm::vec4{0.15f, 0.15f, 0.45f, 1.0f});
        grid->set_minor_color(glm::vec4{0.07f, 0.07f, 0.25f, 1.0f});
    }
}

void Theremin::tool_render(const Render_context& context)
{
    static_cast<void>(context);

    if (!m_headset_renderer || !m_enable_audio)
    {
        return;
    }

    auto&      line_renderer = m_line_renderer_set->hidden;
    const auto camera        = m_headset_renderer->root_camera();

    if (!camera)
    {
        return;
    }

    const auto  transform  = camera->world_from_node();
    const auto& left_hand  = m_hand_tracker->get_hand(Hand_name::Left);
    const auto& right_hand = m_hand_tracker->get_hand(Hand_name::Right);

    constexpr uint32_t green      = 0xff00ff00u;
    constexpr uint32_t half_green = 0x88008800u;

    const glm::vec3 antenna_p0{0.0f, 0.0f, 0.0f};
    const glm::vec3 antenna_p1{0.0f, 2.0f, 0.0f};
    line_renderer.set_line_color(green);
    line_renderer.add_lines( { { antenna_p0, antenna_p1 } }, 10.0f );

    m_left_finger_distance = left_hand.distance(
        XR_HAND_JOINT_THUMB_TIP_EXT,
        XR_HAND_JOINT_INDEX_TIP_EXT
    );

    // From m to cm
    if (m_left_finger_distance.has_value())
    {
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
    m_hand_tracker->set_color(Hand_name::Left, Finger_name::thumb, volume_color);
    m_hand_tracker->set_color(Hand_name::Left, Finger_name::index, volume_color);

    m_right_finger_distance = right_hand.distance(
        XR_HAND_JOINT_THUMB_TIP_EXT,
        XR_HAND_JOINT_INDEX_TIP_EXT
    );

    // From m to cm
    if (m_right_finger_distance.has_value())
    {
        m_right_finger_distance = m_right_finger_distance.value() * 100.0f;
        if (m_right_finger_distance < 1.3f)
        {
            if (!m_right_hold_start_time.has_value())
            {
                m_right_hold_start_time = std::chrono::steady_clock::now();
            }
            else if (!m_right_click)
            {
                const auto duration = std::chrono::steady_clock::now() - m_right_hold_start_time.value();
                if (duration > std::chrono::milliseconds(50))
                {
                    m_right_click = true;

                    m_snap_to_note = !m_snap_to_note;
                    update_grid_color();
                }
            }
        }
        else if (m_right_click)
        {
            m_right_click = false;
            m_right_hold_start_time.reset();
        }
    }
    else
    {
        m_right_click = false;
        m_right_hold_start_time.reset();
    }

    if (right_hand.is_active())
    {
        const auto right_closest_finger = right_hand.get_closest_point_to_line(
            transform,
            antenna_p0,
            antenna_p1
        );
        if (right_closest_finger.has_value())
        {
            const auto  finger         = right_closest_finger.value().finger;
            const auto& closest_points = right_closest_finger.value().closest_points;
            const auto  P = closest_points.P;
            const auto  Q = closest_points.Q;
            const float d = glm::distance(P, Q);
            line_renderer.set_line_color(half_green);
            line_renderer.add_lines( { { P, Q } }, 2.0f );
            set_antenna_distance(d);
            for (
                size_t i = Finger_name::thumb;
                i <= Finger_name::little;
                ++i
            )
            {
                m_hand_tracker->set_color(
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
        ImGui::SliderFloat("Min Finger Tip Distance", &m_finger_distance_min, 0.5f, 30.0f, "%.1f cm");
        ImGui::SliderFloat("Max Finger Tip Distance", &m_finger_distance_max, 0.5f, 30.0f, "%.1f cm");
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
