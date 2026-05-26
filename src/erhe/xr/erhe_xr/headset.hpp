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

class Quad_layer;
class Render_view;
class Render_views_frame;
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
    Headset(erhe::window::Context_window& context_window, const Headset_config& configuration);
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
    // Multiview render entry. Forwards to Xr_session::render_frame_multiview().
    // Only valid when Xr_session::is_multiview_enabled() returns true; the
    // caller is expected to check that and otherwise fall back to render().
    auto render_multiview(erhe::graphics::Command_buffer& command_buffer, std::function<bool(const Render_views_frame&, erhe::graphics::Command_buffer&)> render_views_callback) -> bool;
    auto end_frame     (bool rendered) -> bool;
    // Create a quad composition layer backed by its own color swapchain.
    // Returns nullptr when no session exists or the swapchain cannot be
    // created (the caller is expected to fall back to scene-mesh rendering).
    [[nodiscard]] auto create_quad_layer(uint32_t width, uint32_t height, const std::string& debug_label) -> std::unique_ptr<Quad_layer>;
    [[nodiscard]] auto get_actions_left        ()       ->       Xr_actions*;
    [[nodiscard]] auto get_actions_left        () const -> const Xr_actions*;
    [[nodiscard]] auto get_actions_right       ()       ->       Xr_actions*;
    [[nodiscard]] auto get_actions_right       () const -> const Xr_actions*;
    [[nodiscard]] auto get_hand_tracking_joint (XrHandEXT hand, XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_headset_pose        (glm::vec3& position, glm::quat& orientation) const -> bool;
    [[nodiscard]] auto get_xr_instance         () -> Xr_instance*;
    [[nodiscard]] auto get_xr_session          () -> Xr_session*;
    [[nodiscard]] auto get_configuration       () const -> const Headset_config& { return m_configuration; }

private:
    erhe::window::Context_window& m_context_window;
    Headset_config                m_configuration;
    std::unique_ptr<Xr_instance>  m_xr_instance;
    std::unique_ptr<Xr_session >  m_xr_session;
};

} // namespace erhe::xr
