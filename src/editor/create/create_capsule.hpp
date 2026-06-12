#pragma once

#include "create/create_shape.hpp"

namespace editor {

class Capsule_parameters
{
public:
    int   slice_count           {32};
    int   hemisphere_stack_count{8};
    float length                {1.0f};
    float bottom_radius         {1.0f};
    float top_radius            {0.5f};
};

class Create_capsule : public Create_shape
{
public:
    ~Create_capsule() noexcept override;

    void render_preview(const Create_preview_settings& preview_settings)               override;
    void imgui         ()                                                              override;
    auto create        (Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

    [[nodiscard]] static auto create_brush(Brush_data& brush_create_info, const Capsule_parameters& parameters) -> std::shared_ptr<Brush>;

private:
    Capsule_parameters m_parameters;
};

}
