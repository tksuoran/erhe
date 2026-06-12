#pragma once

#include "create/create_shape.hpp"

namespace editor {

class Create_capsule : public Create_shape
{
public:
    ~Create_capsule() noexcept override;

    void render_preview(const Create_preview_settings& preview_settings)               override;
    void imgui         ()                                                              override;
    auto create        (Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

private:
    int   m_slice_count           {32};
    int   m_hemisphere_stack_count{8};
    float m_length                {1.0f};
    float m_bottom_radius         {1.0f};
    float m_top_radius            {0.5f};
};

}
