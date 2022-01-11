#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/xr/xr.hpp"

#include <glm/glm.hpp>
#include <imgui.h>

#include <optional>

namespace erhe::xr
{
    class Headset;
}

namespace editor
{

class Headset_renderer;
class Line_renderer;
class Line_renderer_set;

enum class Hand_name : unsigned int
{
    Left  = 0,
    Right = 1
};

class Hand
{
public:
    Hand(XrHandEXT hand);

    // Public API
    void update(erhe::xr::Headset& headset);

    auto get_closest_point_to_line(
        const glm::mat4 transform,
        const glm::vec3 p0,
        const glm::vec3 p1
    ) const -> std::optional<erhe::toolkit::Closest_points<float>>;

    auto distance (const XrHandJointEXT lhs, const XrHandJointEXT rhs) const -> std::optional<float>;
    auto is_active() const -> bool;
    auto is_valid (const XrHandJointEXT joint) const -> bool;
    void draw     (Line_renderer& line_renderer, const glm::mat4 transform);

private:
    void draw_joint_line_strip(
        const glm::mat4                    transform,
        const std::vector<XrHandJointEXT>& joint_names,
        Line_renderer&                     line_renderer
    ) const;

    XrHandEXT                                                          m_hand;
    std::array<erhe::xr::Hand_tracking_joint, XR_HAND_JOINT_COUNT_EXT> m_joints;
    bool                                                               m_is_active;
};

class Hand_tracker
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name       {"Hand_tracker"};
    static constexpr std::string_view c_description{"Hand_tracker"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Hand_tracker();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void update              (erhe::xr::Headset& headset);
    auto get_hand            (const Hand_name hand_name) -> Hand&;
    void set_left_hand_color (const uint32_t color);
    void set_right_hand_color(const uint32_t color);

private:
    // Component dependencies
    std::shared_ptr<Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Headset_renderer>  m_headset_renderer;

    Hand m_left_hand;
    Hand m_right_hand;

    bool   m_show_hands      {true};
    ImVec4 m_left_hand_color {1.0f, 0.5f, 0.0f, 1.0f};
    ImVec4 m_right_hand_color{0.0f, 1.0f, 0.5f, 1.0f};
};

} // namespace editor
