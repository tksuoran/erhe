#pragma once

#include "preview/scene_preview.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/gtc/quaternion.hpp>

#include <span>

struct Preview_edge_lines_config;

namespace erhe::primitive { class Material; class Primitive; }

namespace editor {

class Brush;
class Composition_pass;

// The preview scene's lights for one render (Brush_preview::render_preview()).
enum class Preview_lighting : unsigned int {
    key_and_fill = 0,
    headlight         // the key light at the camera, no fill
};

class Brush_preview : public Scene_preview
{
public:
    Brush_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        App_context&                    app_context,
        App_message_bus&                app_message_bus
    );
    ~Brush_preview() noexcept;

    // Both overloads record into command_buffer: the frame command buffer
    // when called from a Thumbnails::update() callback (render phase), the
    // upload command buffer (App_context::current_command_buffer) when
    // called from the prepare phase.
    void render_preview(
        erhe::graphics::Command_buffer&                 command_buffer,
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        unsigned int                                    texture_layer,
        const std::shared_ptr<Brush>&                   brush,
        int64_t                                         time
    );

    // Renders any prebuilt renderable primitive (bounding-sphere camera
    // fit, model oriented by `orientation` - the brush overload above
    // derives a Y spin from `time` for the spinning thumbnails, the
    // geometry graph passes each node's persistent arcball orientation).
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
        erhe::graphics::Command_buffer&                    command_buffer,
        const std::shared_ptr<erhe::graphics::Texture>&    texture,
        unsigned int                                       texture_layer,
        const std::shared_ptr<erhe::primitive::Primitive>& primitive,
        const std::shared_ptr<erhe::primitive::Material>&  material,
        const glm::quat&                                   orientation,
        bool                                               headlight_shading = false,
        const Preview_edge_lines_config*                   edge_lines = nullptr
    );

    // Lists the material in the preview scene's content library (R5.2b explicit registration); a listed one stays.
    void add_material(const std::shared_ptr<erhe::primitive::Material>& material);

    // The parts of a kit piece, part i with materials[i] (null: the preview material), framed by their joint
    // bounding sphere (doc/editor/level_tools.md "Shape thumbnails").
    void render_parts_preview(
        erhe::graphics::Command_buffer&                              command_buffer,
        const std::shared_ptr<erhe::graphics::Texture>&              texture,
        unsigned int                                                 texture_layer,
        std::span<const std::shared_ptr<erhe::primitive::Primitive>> primitives,
        std::span<const std::shared_ptr<erhe::primitive::Material>>  materials,
        const glm::quat&                                             orientation,
        const Preview_edge_lines_config*                             edge_lines
    );

    // The preview mesh keeps the last rendered primitive and its material
    // bound (and listed in the preview scene's Material_set) until the next
    // render. Scene close / items removed: when that material is content of
    // the closing scene or names a removed item, drop the mesh so the
    // preview holds nothing of dead content.
    void on_close_scene  (erhe::Item_host* closing_host);
    void on_items_removed(const Removed_items& removed);

private:
    void render_primitives(
        erhe::graphics::Command_buffer&                              command_buffer,
        const std::shared_ptr<erhe::graphics::Texture>&              texture,
        unsigned int                                                 texture_layer,
        std::span<const std::shared_ptr<erhe::primitive::Primitive>> primitives,
        std::span<const std::shared_ptr<erhe::primitive::Material>>  materials,
        const glm::quat&                                             orientation,
        Preview_lighting                                             lighting,
        const Preview_edge_lines_config*                             edge_lines
    );
    void make_preview_scene();
    void release_preview_mesh();
    [[nodiscard]] auto get_preview_material() const -> const erhe::primitive::Material*;

    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;

    bool                                       m_solid_wireframe_supported;
    erhe::graphics::Base_render_pipeline       m_wireframe_pipeline;
    std::shared_ptr<Composition_pass>          m_wireframe_pass;
    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::primitive::Material> m_headlight_material;
    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Light>        m_key_light;
    std::shared_ptr<erhe::scene::Light>        m_fill_light;
};

}
