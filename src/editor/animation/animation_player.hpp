#pragma once

#include "animation/animation_keying.hpp"

#include <memory>

namespace erhe::scene { class Animation; }

namespace editor {

class App_context;

// Owns animation playback state for the editor: the active animation, the
// play position and the transport state (playing / looping / speed).
//
// update() is called once per frame from Editor::tick(), independent of any
// ImGui window visibility, so playback continues even when the Animation
// window is hidden. Applying the animation writes the sampled TRS values to
// the target nodes, updates the scene node transforms and sends
// Animation_update_message so tools (e.g. Transform_tool) can refresh.
class Animation_player
{
public:
    explicit Animation_player(App_context& context);

    // Called once per frame from Editor::tick() with the wall-clock frame
    // duration in seconds.
    void update(float dt_s);

    void set_animation(const std::shared_ptr<erhe::scene::Animation>& animation);
    [[nodiscard]] auto get_animation() const -> const std::shared_ptr<erhe::scene::Animation>&;

    void play ();
    void pause();
    void stop (); // pause and seek to start

    // Seek to an absolute animation time (clamped to [start, end]) and apply.
    void seek(float time);

    // Timeline range (LightWave-style scene range): user-editable and
    // independent of the active animation; animation keys only ever extend it
    // (refresh_time_range()), never shrink it. Setting one end pushes the
    // other end if needed and keeps the play position in range.
    void set_start_time(float time);
    void set_end_time  (float time);

    void set_speed  (float speed);
    void set_looping(bool looping);

    [[nodiscard]] auto is_playing    () const -> bool;
    [[nodiscard]] auto is_looping    () const -> bool;
    [[nodiscard]] auto get_speed     () const -> float;
    [[nodiscard]] auto get_time      () const -> float;
    [[nodiscard]] auto get_start_time() const -> float;
    [[nodiscard]] auto get_end_time  () const -> float;

    // Called after keyframe data of the given animation has been modified
    // (edit / undo / redo): refreshes the cached time range, keeps the play
    // position in range and re-applies the current pose.
    void on_animation_edited(const std::shared_ptr<erhe::scene::Animation>& animation);

    // Autokey (LightWave-style): when not off, finished transform edits key
    // the edited nodes into the active animation at the current time (see
    // Transform_tool::record_transform_operation).
    void set_autokey_mode(Autokey_mode mode);
    [[nodiscard]] auto get_autokey_mode() const -> Autokey_mode;

private:
    void refresh_time_range();
    void apply();

    App_context& m_context;

    std::shared_ptr<erhe::scene::Animation> m_animation;

    Autokey_mode m_autokey_mode{Autokey_mode::off};

    bool  m_playing        {false};
    bool  m_looping        {true};
    float m_speed          {1.0f};
    float m_time           {0.0f};
    float m_start_time     {0.0f};
    float m_end_time       {10.0f};
    bool  m_apply_requested{false};
};

}
