#pragma once

#include "erhe/xr/xr.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

struct XrCompositionLayerProjectionView;
//struct XrFrameState;

namespace erhe::toolkit {

class Context_window;

}

namespace erhe::xr {

class Render_view;
class Xr_instance;
class Xr_session;

class Frame_timing
{
public:
    uint64_t predicted_display_time  {0};
    uint64_t predicted_display_pediod{0};
    bool     should_render           {false};
};

class Headset final
{
public:
    explicit Headset(erhe::toolkit::Context_window* window);
    ~Headset        () noexcept;

    // TODO [[nodiscard]]
    auto begin_frame    () -> Frame_timing;
    auto render         (std::function<bool(Render_view&)> render_view_callback) -> bool;
    auto end_frame      () -> bool;
    auto trigger_value  () const -> float;
    auto squeeze_click  () const -> bool;
    auto controller_pose() const -> Pose;
    [[nodiscard]] auto get_hand_tracking_joint (const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(const XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_view_in_world       () const -> glm::mat4;

private:
    std::unique_ptr<Xr_instance> m_xr_instance;
    std::unique_ptr<Xr_session > m_xr_session;
    Pose                         m_controller_pose;
};

}
