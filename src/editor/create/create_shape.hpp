#pragma once

#include <memory>

namespace erhe::renderer { class Primitive_renderer; }

namespace editor {

class Create_preview_settings;
class Brush;
class Brush_data;

class Create_shape
{
public:
    virtual ~Create_shape() noexcept;

    virtual void render_preview(const Create_preview_settings& preview_settings) = 0;
    virtual void imgui() = 0;
    [[nodiscard]] virtual auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> = 0;

    static auto get_line_renderer(const Create_preview_settings& preview_settings) -> erhe::renderer::Primitive_renderer;
};

}
