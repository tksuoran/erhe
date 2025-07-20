#pragma once

#include "preview/scene_preview.hpp"

namespace editor {

class Material_preview : public Scene_preview
{
public:
    Material_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::scene::Scene_message_bus& scene_message_bus,
        App_context&                    app_context,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );
    ~Material_preview();

    void render_preview(const std::shared_ptr<erhe::primitive::Material>& material);
    void show_preview  ();

private:
    void make_preview_scene(Mesh_memory& mesh_memory);
    //// void generate_torus_geometry();

    std::shared_ptr<erhe::primitive::Material> m_last_material;

    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Node>         m_key_light_node;
    std::shared_ptr<erhe::scene::Light>        m_key_light;

    int                                        m_slice_count{40};
    int                                        m_stack_count{22};
    float                                      m_radius{1.0f};
};

}
