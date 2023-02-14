#pragma once

#include "tools/tool.hpp"

#include "tools/brushes/create/create.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/scene/transform.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class Brush;
class Brush_data;

class Create_box
    : public Brush_create
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
