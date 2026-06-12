#pragma once

#include "create/create_shape.hpp"

namespace editor {

class Cone_parameters
{
public:
    int   slice_count  {32};
    int   stack_count  {1};
    float height       {1.33f};
    float bottom_radius{1.0f};
    float top_radius   {0.5f};
    bool  use_top      {true};
    bool  use_bottom   {true};
};

class Create_cone : public Create_shape
{
public:
    ~Create_cone() noexcept override;

    void render_preview(const Create_preview_settings& preview_settings)               override;
    void imgui         ()                                                              override;
    auto create        (Brush_data& brush_create_info) const -> std::shared_ptr<Brush> override;

    [[nodiscard]] static auto create_brush(Brush_data& brush_create_info, const Cone_parameters& parameters) -> std::shared_ptr<Brush>;

private:
    Cone_parameters m_parameters;
};


}
