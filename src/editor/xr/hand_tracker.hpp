#pragma once

#include "tools/tool.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/optional.hpp"
#include "erhe/xr/xr.hpp"

#include <glm/glm.hpp>
#include <imgui.h>

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

class Finger_name
{
public:
    static const size_t thumb  = 0;
    static const size_t index  = 1;
    static const size_t middle = 2;
    static const size_t ring   = 3;
    static const size_t little = 4;
    static const size_t palm   = 5;
    static const size_t wrist  = 6;
    static const size_t count  = 7;
};

class Closest_finger
{
public:
    size_t                               finger;
    erhe::toolkit::Closest_points<float> closest_points;
};

class Hand
{
public:
    explicit Hand(const XrHandEXT hand);

    // Public API
    void update(erhe::xr::Headset& headset);

    auto get_closest_point_to_line(
        const glm::mat4 transform,
        const glm::vec3 p0,
        const glm::vec3 p1
    ) const -> nonstd::optional<Closest_finger>;

    auto distance (const XrHandJointEXT lhs, const XrHandJointEXT rhs) const -> nonstd::optional<float>;
    auto is_active() const -> bool;
    auto is_valid (const XrHandJointEXT joint) const -> bool;
    void draw     (Line_renderer& line_renderer, const glm::mat4 transform);
    void set_color(const size_t finger, const ImVec4 color);

private:
    void draw_joint_line_strip(
        const glm::mat4                    transform,
        const std::vector<XrHandJointEXT>& joint_names,
        Line_renderer&                     line_renderer
    ) const;

    XrHandEXT                                                          m_hand;
    std::array<erhe::xr::Hand_tracking_joint, XR_HAND_JOINT_COUNT_EXT> m_joints;
    bool                                                               m_is_active;
    std::array<ImVec4, Finger_name::count>                             m_color;
};

class Hand_tracker
    : public erhe::components::Component
    , public Tool
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

    // Public API
    void update   (erhe::xr::Headset& headset);
    auto get_hand (const Hand_name hand_name) -> Hand&;
    void set_color(const Hand_name hand_name, const ImVec4 color);
    void set_color(
        const Hand_name hand_name,
        const size_t    finger_name,
        const ImVec4    color
    );

private:
    // Component dependencies
    std::shared_ptr<Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Headset_renderer>  m_headset_renderer;

    Hand m_left_hand;
    Hand m_right_hand;

    bool m_show_hands{true};
};

} // namespace editor
