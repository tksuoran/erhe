#pragma once

#include "tools/tool.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/xr/xr.hpp"

#include <glm/glm.hpp>
#include <imgui.h>

#include <optional>

namespace erhe::xr
{
    class Headset;
}

namespace erhe::application
{
    class Line_renderer;
}

namespace editor
{

enum class Hand_name : unsigned int
{
    Left  = 0,
    Right = 1
};

class Finger_name
{
public:
    static const std::size_t thumb  = 0;
    static const std::size_t index  = 1;
    static const std::size_t middle = 2;
    static const std::size_t ring   = 3;
    static const std::size_t little = 4;
    static const std::size_t palm   = 5;
    static const std::size_t wrist  = 6;
    static const std::size_t count  = 7;
};

class Finger_point
{
public:
    std::size_t finger;
    glm::vec3   finger_point;
    glm::vec3   point;
};

class Joint
{
public:
    glm::vec3 position;
    glm::mat4 orientation;
};

class Hand
{
public:
    explicit Hand(const XrHandEXT hand);

    // Public API
    void update(erhe::xr::Headset& headset);

    [[nodiscard]] auto get_closest_point_to_line(
        const glm::mat4 transform,
        const glm::vec3 p0,
        const glm::vec3 p1
    ) const -> std::optional<Finger_point>;

    [[nodiscard]] auto get_joint(
        XrHandJointEXT joint
    ) const -> std::optional<Joint>;

    auto distance (const XrHandJointEXT lhs, const XrHandJointEXT rhs) const -> std::optional<float>;
    auto is_active() const -> bool;
    auto is_valid (const XrHandJointEXT joint) const -> bool;
    void draw     (erhe::application::Line_renderer& line_renderer, const glm::mat4 transform);
    void set_color(const std::size_t finger, const ImVec4 color);

private:
    void draw_joint_line_strip(
        const glm::mat4                    transform,
        const std::vector<XrHandJointEXT>& joint_names,
        erhe::application::Line_renderer&  line_renderer
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
    static constexpr std::string_view c_type_name  {"Hand_tracker"};
    static constexpr std::string_view c_description{"Hand_tracker"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Hand_tracker();
    ~Hand_tracker();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Public API
    void update_hands(erhe::xr::Headset& headset);
    auto get_hand (const Hand_name hand_name) -> Hand&;
    void set_color(const Hand_name hand_name, const ImVec4 color);
    void set_color(
        const Hand_name   hand_name,
        const std::size_t finger_name,
        const ImVec4      color
    );

private:
    Hand m_left_hand;
    Hand m_right_hand;

    bool m_show_hands{true};
};

extern Hand_tracker* g_hand_tracker;

} // namespace editor
