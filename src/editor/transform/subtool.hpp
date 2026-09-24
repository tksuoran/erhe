#pragma once

#include "tools/tool.hpp"

#include <imgui/imgui.h>

#include <glm/glm.hpp>

#include <functional>
#include <optional>

namespace editor {

enum class Handle : unsigned int;

class Hover_entry;
class Property_editor;
class Scene_view;
class Tools;
class Transform_tool_shared;

class Subtool : public Tool
{
public:
    explicit Subtool(App_context& app_context);
    Subtool(App_context& app_context, Tools& tools, uint64_t flags);
    ~Subtool() noexcept override;

    // Starts a drag: calls begin() and makes the subtool active exactly when
    // begin() succeeded. A drag that did not start is never ended (end() is
    // not called for it), so a failed begin() leaves the subtool inactive -
    // render() and update() only ever see state a successful begin() set up.
    [[nodiscard]] auto begin_drag(unsigned int axis_mask, Scene_view* scene_view) -> bool;

    [[nodiscard]] virtual auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool = 0;
    [[nodiscard]] virtual auto update(Scene_view* scene_view) -> bool = 0;

    virtual void imgui(Property_editor& property_editor);

    void end();

    [[nodiscard]] auto is_active    () const -> bool;
    [[nodiscard]] auto get_axis_mask() const -> unsigned int;

    void set_transform_shared(Transform_tool_shared& shared, std::function<void()> record_operation);

protected:
    [[nodiscard]] auto get_shared              () const -> Transform_tool_shared&;
    [[nodiscard]] auto get_basis               () const -> glm::mat3;
    [[nodiscard]] auto get_basis               (bool world) const -> glm::mat3;
    [[nodiscard]] auto project_pointer_to_plane(Scene_view* scene_view, glm::vec3 n, glm::vec3 p) -> std::optional<glm::vec3>;
    [[nodiscard]] auto offset_plane_origo      (glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto project_to_offset_plane (glm::vec3 p, glm::vec3 q) const -> glm::vec3;
    [[nodiscard]] auto get_axis_direction      () const -> glm::vec3;
    [[nodiscard]] auto get_plane_normal        (bool world) const -> glm::vec3;
    [[nodiscard]] auto get_plane_side          (bool world) const -> glm::vec3;

    bool         m_active   {false};
    unsigned int m_axis_mask{0u};

private:
    Transform_tool_shared* m_shared           {nullptr};
    std::function<void()>  m_record_operation;
};

auto get_label_color(std::size_t i, bool text, bool matches_gizmo) -> uint32_t;

static constexpr int axis_x            = 0;
static constexpr int axis_y            = 1;
static constexpr int axis_z            = 2;
static constexpr int axis_w            = 3;
static constexpr int axis_xyzw_mask    = 0x3;
static constexpr int axis_euler_repeat = 4;
static constexpr int axis_x2           = axis_x | axis_euler_repeat;
static constexpr int axis_y2           = axis_y | axis_euler_repeat;
static constexpr int axis_z2           = axis_z | axis_euler_repeat;
static constexpr int axis_w2           = axis_w | axis_euler_repeat;

auto get_drag_color(std::size_t i, bool locked) -> ImVec4;
}
