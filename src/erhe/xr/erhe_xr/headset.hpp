#pragma once

#include "erhe_xr/xr.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

struct XrCompositionLayerProjectionView;

namespace erhe::window {
    class Context_window;
}

namespace erhe::xr {

class Render_view;
class Xr_actions;
class Xr_instance;
class Xr_session;
class Xr_configuration;

class Frame_timing
{
public:
    uint64_t predicted_display_time  {0};
    uint64_t predicted_display_pediod{0};
    bool     begin_ok                {false};
    bool     should_render           {false};
};

class Headset final
{
public:
    Headset(
        erhe::window::Context_window& context_window,
        const Xr_configuration&        configuration
    );
    ~Headset() noexcept;

    auto is_valid   () const -> bool;
    auto begin_frame() -> Frame_timing;
    auto render     (std::function<bool(Render_view&)> render_view_callback) -> bool;
    auto end_frame  (bool rendered) -> bool;
    [[nodiscard]] auto get_actions_left        ()       ->       Xr_actions&;
    [[nodiscard]] auto get_actions_left        () const -> const Xr_actions&;
    [[nodiscard]] auto get_actions_right       ()       ->       Xr_actions&;
    [[nodiscard]] auto get_actions_right       () const -> const Xr_actions&;
    [[nodiscard]] auto get_hand_tracking_joint (const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(const XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_view_in_world       () const -> glm::mat4;
    [[nodiscard]] auto get_xr_instance         () -> Xr_instance&;
    [[nodiscard]] auto get_xr_session          () -> Xr_session&;

private:
    std::unique_ptr<Xr_instance> m_xr_instance;
    std::unique_ptr<Xr_session > m_xr_session;
};

}
