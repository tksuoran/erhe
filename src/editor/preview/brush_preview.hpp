#pragma once

#include "preview/scene_preview.hpp"

namespace erhe::primitive { class Primitive; }

namespace editor {

class Brush;

class Brush_preview : public Scene_preview
{
public:
    Brush_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        App_context&                    app_context
    );
    ~Brush_preview() noexcept;

    void render_preview(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        unsigned int                                    texture_layer,
        const std::shared_ptr<Brush>&                   brush,
        int64_t                                         time
    );

    // Renders any prebuilt renderable primitive (bounding-sphere camera
    // fit, auto-rotation by `time`). The brush overload above reduces to
    // this; the geometry graph's per-node mesh previews call it directly.
    // Null material falls back to the preview scene's default material.
    void render_preview(
        const std::shared_ptr<erhe::graphics::Texture>&    texture,
        unsigned int                                       texture_layer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const std::shared_ptr<erhe::primitive::Material>&  material,
        int64_t                                            time
    );

private:
    void make_preview_scene();

    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Node>         m_key_light_node;
    std::shared_ptr<erhe::scene::Light>        m_key_light;
    std::shared_ptr<erhe::scene::Node>         m_fill_light_node;
    std::shared_ptr<erhe::scene::Light>        m_fill_light;
};

}
