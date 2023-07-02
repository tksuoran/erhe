#pragma once

#include "tools/brushes/create/create_shape.hpp"

#include <glm/glm.hpp>

namespace editor
{

class Brush;
class Brush_data;

class Create_box
    : public Create_shape
{
public:
    void render_preview(const Create_preview_settings& preview_settings) override;

    void imgui() override;

    [[nodiscard]] auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

private:
    glm::vec3  m_size {1.0f, 1.0f, 1.0f};
    glm::ivec3 m_steps{3, 3, 3};
    float      m_power{1.0f};
};


} // namespace editor
