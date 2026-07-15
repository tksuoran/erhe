#pragma once

#include "preview/scene_preview.hpp"

struct Preview_edge_lines_config;

namespace erhe::primitive { class Primitive; }

namespace editor {

class Brush;
class Composition_pass;

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
    // fit, oriented by `rotation_radians` around Y - the brush overload
    // above derives it from `time` for the spinning thumbnails, the
    // geometry graph passes each node's persistent hover-spun angle).
    // Null material falls back to the preview scene's default material.
    // headlight_shading gives an N.V-dimmed look: the key light is
    // co-located with the fitted camera (Lambert diffuse then falls off
    // with dot(N, V)), the fill light is off, and a neutral white diffuse
    // material is used when none is given.
    // edge_lines selects the edge-line overlay (solid-wireframe pass over
    // the fill): null or disabled draws fill only; the brush overload
    // passes editor_settings->brush_preview_edge_lines, the geometry graph
    // passes graph_node_preview_edge_lines. Inert when the device lacks
    // the SOLID_WIREFRAME variant (Device_info::use_solid_wireframe) or
    // the primitive was built without the expanded fill stream.
    void render_preview(
        const std::shared_ptr<erhe::graphics::Texture>&    texture,
        unsigned int                                       texture_layer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const std::shared_ptr<erhe::primitive::Material>&  material,
        float                                              rotation_radians,
        bool                                               headlight_shading = false,
        const Preview_edge_lines_config*                   edge_lines = nullptr
    );

private:
    void make_preview_scene();

    bool                                       m_solid_wireframe_supported;
    erhe::graphics::Base_render_pipeline       m_wireframe_pipeline;
    std::shared_ptr<Composition_pass>          m_wireframe_pass;
    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::primitive::Material> m_headlight_material;
    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Node>         m_key_light_node;
    std::shared_ptr<erhe::scene::Light>        m_key_light;
    std::shared_ptr<erhe::scene::Node>         m_fill_light_node;
    std::shared_ptr<erhe::scene::Light>        m_fill_light;
};

}
