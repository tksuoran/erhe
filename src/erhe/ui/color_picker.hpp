#ifndef color_picker_hpp_erhe_ui
#define color_picker_hpp_erhe_ui

#include "erhe/ui/area.hpp"
#include "erhe/ui/ninepatch.hpp"
#include <memory>

namespace erhe::primitive
{
class Mesh;
class Geometry_mesh;
} // namespace erhe::primitive

namespace erhe::ui
{

class Color_picker
    : public Area
{
public:
    Color_picker(Gui_renderer&         renderer,
                 gsl::not_null<Style*> style) noexcept;

    virtual ~Color_picker() = default;

    void animate() noexcept;

    // Area methods

    void begin_place(Rectangle reference, glm::vec2 grow_direction) noexcept override;

    void draw_self(ui_context& context) noexcept override;

private:
    std::unique_ptr<erhe::primitive::Mesh>          m_disc;
    std::unique_ptr<erhe::primitive::Mesh>          m_triangle;
    std::unique_ptr<erhe::primitive::Geometry_mesh> m_hsv_disc_mesh;
    std::unique_ptr<erhe::primitive::Geometry_mesh> m_hsv_disc2_mesh;
    std::unique_ptr<erhe::primitive::Geometry_mesh> m_hsv_quad_mesh;
    std::unique_ptr<erhe::primitive::Geometry_mesh> m_color_mesh;
    std::unique_ptr<erhe::primitive::Geometry_mesh> m_handle_mesh;
    std::unique_ptr<erhe::primitive::Geometry_mesh> m_handle2_mesh;

    std::unique_ptr<Ninepatch> m_ninepatch;

    float     m_size                 { 0.0f};
    glm::mat4 m_background_frame     { 1.0f};
    glm::mat4 m_hsv_transform        { 1.0f};
    glm::mat4 m_disc_handle_transform{ 1.0f};
    glm::mat4 m_quad_handle_transform{12.0f};
    float     m_disc_handle_radius   { 0.0f};
    float     m_quad_edge_length     { 0.0f};
    float     m_h                    { 0.0f};
    float     m_s                    { 1.0f};
    float     m_v                    { 1.0f};
};

} // namespace erhe::ui

#endif
