#pragma once

#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_instance.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

struct XrCompositionLayerProjectionView;

namespace erhe::graphics { class Command_buffer; class Device; }
namespace erhe::window { class Context_window; }

namespace erhe::xr {

class Render_view;
class Xr_session;

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
    Headset(erhe::window::Context_window& context_window, const Xr_configuration& configuration);
    ~Headset() noexcept;

    // Second-phase construction. Must be called after the graphics Device has been
    // created (on Vulkan the session's XrGraphicsBindingVulkan2KHR needs the device
    // handles; on OpenGL the session still needs the main-thread GL context). Returns
    // true if the Xr_session was successfully created.
    [[nodiscard]] auto create_session(erhe::graphics::Device& graphics_device) -> bool;

    auto is_valid      () const -> bool;
    auto is_active     () const -> bool;
    auto poll_events   () const -> bool;
    auto update_actions() const -> bool;
    auto begin_frame_  () -> Frame_timing;
    auto render        (erhe::graphics::Command_buffer& command_buffer, std::function<bool(Render_view&, erhe::graphics::Command_buffer&)> render_view_callback) -> bool;
    auto end_frame     (bool rendered) -> bool;
    [[nodiscard]] auto get_actions_left        ()       ->       Xr_actions*;
    [[nodiscard]] auto get_actions_left        () const -> const Xr_actions*;
    [[nodiscard]] auto get_actions_right       ()       ->       Xr_actions*;
    [[nodiscard]] auto get_actions_right       () const -> const Xr_actions*;
    [[nodiscard]] auto get_hand_tracking_joint (XrHandEXT hand, XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_headset_pose        (glm::vec3& position, glm::quat& orientation) const -> bool;
    [[nodiscard]] auto get_xr_instance         () -> Xr_instance*;
    [[nodiscard]] auto get_xr_session          () -> Xr_session*;

private:
    erhe::window::Context_window& m_context_window;
    Xr_configuration              m_configuration;
    std::unique_ptr<Xr_instance>  m_xr_instance;
    std::unique_ptr<Xr_session >  m_xr_session;
};

} // namespace erhe::xr
