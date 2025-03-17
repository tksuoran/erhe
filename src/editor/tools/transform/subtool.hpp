#pragma once

#include "tools/tool.hpp"

#include <glm/glm.hpp>

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
    explicit Subtool(Editor_context& editor_context);
    ~Subtool() noexcept override;

    [[nodiscard]] virtual auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool = 0;
    [[nodiscard]] virtual auto update(Scene_view* scene_view) -> bool = 0;

    virtual void imgui(Property_editor& property_editor);

    void end();

    [[nodiscard]] auto is_active    () const -> bool;
    [[nodiscard]] auto get_axis_mask() const -> unsigned int;

protected:
    [[nodiscard]] auto get_shared              () const -> Transform_tool_shared&;
    [[nodiscard]] auto get_basis               () const -> const glm::mat4&;
    [[nodiscard]] auto get_basis               (bool world) const -> const glm::mat4&;
    [[nodiscard]] auto project_pointer_to_plane(Scene_view* scene_view, const glm::vec3 n, const glm::vec3 p) -> std::optional<glm::vec3>;
    [[nodiscard]] auto offset_plane_origo      (const glm::vec3 p) const -> glm::vec3;
    [[nodiscard]] auto project_to_offset_plane (const glm::vec3 p, const glm::vec3 q) const -> glm::vec3;
    [[nodiscard]] auto get_axis_direction      () const -> glm::vec3;
    [[nodiscard]] auto get_plane_normal        (const bool world) const -> glm::vec3;
    [[nodiscard]] auto get_plane_side          (const bool world) const -> glm::vec3;

    bool         m_active   {false};
    unsigned int m_axis_mask{0u};
};

} // namespace editor
