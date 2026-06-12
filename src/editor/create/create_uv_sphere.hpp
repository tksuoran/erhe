#pragma once

#include "create/create_shape.hpp"

namespace editor {

class Uv_sphere_parameters
{
public:
    int   slice_count{8};
    int   stack_count{8};
    float radius     {1.0f};
};

class Create_uv_sphere : public Create_shape
{
public:
    ~Create_uv_sphere() noexcept override;

    void render_preview(const Create_preview_settings& preview_settings)               override;
    void imgui         ()                                                              override;
    auto create        (Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

    [[nodiscard]] static auto create_brush(Brush_data& brush_create_info, const Uv_sphere_parameters& parameters) -> std::shared_ptr<Brush>;

private:
    Uv_sphere_parameters m_parameters;
};

}
