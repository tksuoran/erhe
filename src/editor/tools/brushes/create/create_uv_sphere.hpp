#pragma once

#include "tools/brushes/create/create_shape.hpp"

namespace editor {

class Create_uv_sphere
    : public Create_shape
{
public:
    void render_preview(const Create_preview_settings& preview_settings) override;

    void imgui() override;

    auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

private:
    int   m_slice_count{8};
    int   m_stack_count{8};
    float m_radius     {1.0f};
};

} // namespace editor
