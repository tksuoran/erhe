#pragma once

#include "erhe/xr/xr.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

struct XrCompositionLayerProjectionView;
struct XrFrameState;

namespace erhe::toolkit {

class Context_window;

}

namespace erhe::xr {

struct Render_view;
class Xr_instance;
class Xr_session;

struct Frame_timing
{
    uint64_t predicted_display_time  {0};
    uint64_t predicted_display_pediod{0};
    bool     should_render           {false};
};

class Headset final
{
public:
    explicit Headset(erhe::toolkit::Context_window* window);
    ~Headset        ();

    auto begin_frame    () -> Frame_timing;
    auto render         (std::function<bool(Render_view&)> render_view_callback) -> bool;
    auto end_frame      () -> bool;
    auto trigger_value  () const -> float;
    auto controller_pose() const -> Pose;

private:
    std::unique_ptr<Xr_instance> m_xr_instance;
    std::unique_ptr<Xr_session > m_xr_session;
    Pose                         m_controller_pose;
};

}
