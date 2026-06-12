#pragma once

#include "create/create_shape.hpp"

#include <glm/glm.hpp>

namespace editor {

class Brush;
class Brush_data;

class Box_parameters
{
public:
    glm::vec3  size {1.0f, 1.0f, 1.0f};
    glm::ivec3 steps{3, 3, 3};
    float      power{1.0f};
};

class Create_box : public Create_shape
{
public:
    ~Create_box() noexcept override;

    void render_preview(const Create_preview_settings& preview_settings)               override;
    void imgui         ()                                                              override;
    auto create        (Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

    [[nodiscard]] static auto create_brush(Brush_data& brush_create_info, const Box_parameters& parameters) -> std::shared_ptr<Brush>;

private:
    Box_parameters m_parameters;
};

}
