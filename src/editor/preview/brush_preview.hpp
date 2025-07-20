#pragma once

#include "preview/scene_preview.hpp"

namespace editor {

class Brush;

class Brush_preview : public Scene_preview
{
public:
    Brush_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::scene::Scene_message_bus& scene_message_bus,
        App_context&                    app_context,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );
    ~Brush_preview();

    void render_preview(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const std::shared_ptr<Brush>&                   brush,
        int64_t                                         time
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
