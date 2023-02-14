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

class Create_uv_sphere
    : public Brush_create
{
public:
    void render_preview(const Create_preview_settings& preview_settings) override;

    void imgui() override;

    [[nodiscard]] auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

private:
    int   m_slice_count{8};
    int   m_stack_count{8};
    float m_radius     {1.0f};
};


} // namespace editor
