#ifndef ninepatch_hpp_erhe_ui
#define ninepatch_hpp_erhe_ui

#include "erhe/graphics_experimental/render_group.hpp"
#include "erhe/primitive/mesh.hpp"
#include <glm/glm.hpp>
#include <gsl/pointers>

namespace erhe::ui
{

class Gui_renderer;
class Ninepatch_style;

class Ninepatch
{
public:
    Ninepatch(Gui_renderer&                         renderer,
              gsl::not_null<const Ninepatch_style*> style);

    void place(float x0, float y0, float width, float height);

    void render(Gui_renderer& renderer);

    auto style() const
    -> gsl::not_null<const Ninepatch_style*>
    {
        return m_style;
    }

    auto mesh() const
    -> const erhe::primitive::Mesh&
    {
        return m_mesh;
    }

    auto size() const
    -> glm::vec2
    {
        return m_size;
    }

    void update_render(Gui_renderer& gui_renderer) const noexcept;

private:
    gsl::not_null<const Ninepatch_style*>      m_style;
    erhe::primitive::Mesh                           m_mesh;
    glm::vec2                                  m_size{0.0f};
    erhe::graphics::Render_group::draw_index_t m_draw;
};

} // namespace erhe::ui

#endif
