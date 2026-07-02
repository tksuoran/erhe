#include "geometry_graph/nodes/geometry_output_node.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <cfloat>
#include <mutex>

namespace editor {

Geometry_output_node::Geometry_output_node(App_context& context)
    : Geometry_graph_node{"Output"}
    , m_context          {context}
{
    make_input_pin(Geometry_pin_key::geometry, "in");
}

Geometry_output_node::~Geometry_output_node() noexcept
{
    remove_scene_node();
}

void Geometry_output_node::on_removed_from_graph()
{
    remove_scene_node();
}

void Geometry_output_node::remove_scene_node()
{
    if (m_node) {
        m_node->set_parent({});
    }
    m_node.reset();
    m_mesh.reset();
}

void Geometry_output_node::apply_scene_node_name()
{
    if (m_node) {
        m_node->set_name(m_scene_node_name);
    }
    if (m_mesh) {
        m_mesh->set_name(m_scene_node_name + " Mesh");
    }
}

void Geometry_output_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();

    if (!m_scene_root) {
        m_scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (!m_scene_root) {
        return;
    }

    if (!source) {
        if (m_mesh) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
            m_mesh->clear_primitives();
        }
        return;
    }

    if (!m_material) {
        const std::shared_ptr<Content_library> library = m_scene_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                m_material = materials.front();
            }
        }
    }

    // Copy before render processing; the input geometry is shared with
    // upstream nodes and must not be mutated.
    std::shared_ptr<erhe::geometry::Geometry> render_geometry = std::make_shared<erhe::geometry::Geometry>(source->get_name());
    GEO::mat4f identity;
    identity.load_identity();
    render_geometry->copy_with_transform(*source.get(), identity);
    render_geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect                            |
                erhe::geometry::Geometry::process_flag_build_edges                        |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids            |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals      |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates |
                erhe::geometry::Geometry::process_flag_generate_tangents
        }
    );

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
    };

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(render_geometry);
    const bool renderable_ok = primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::point_normals);
    if (!renderable_ok) {
        log_graph_editor->warn("Geometry_output_node: failed to build renderable mesh for '{}'", render_geometry->get_name());
        return;
    }
    if (!primitive->make_raytrace()) {
        log_graph_editor->warn("Geometry_output_node: failed to build raytrace shape for '{}'", render_geometry->get_name());
    }

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
    if (!m_node) {
        m_node = std::make_shared<erhe::scene::Node>(m_scene_node_name);
        m_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        m_mesh = std::make_shared<erhe::scene::Mesh>(m_scene_node_name + " Mesh");
        m_mesh->layer_id = m_scene_root->layers().content()->id;
        m_mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::shadow_cast);
        m_node->attach(m_mesh);
        m_node->set_parent(m_scene_root->get_scene().get_root_node());
    }
    m_mesh->clear_primitives();
    m_mesh->add_primitive(primitive, m_material);
}

void Geometry_output_node::imgui()
{
    // Scene node name; committed (and made undoable via the parameter
    // edit gesture) when the input defocuses, not on every keystroke.
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##scene_node_name", &m_scene_node_name);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        apply_scene_node_name();
        mark_dirty();
    }

    // Scene selection
    const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
    if (scene_roots.size() > 1) {
        int scene_index = 0;
        for (std::size_t i = 0, end = scene_roots.size(); i < end; ++i) {
            if (scene_roots[i] == m_scene_root) {
                scene_index = static_cast<int>(i);
                break;
            }
        }
        if (imgui_index_stepper("scene", scene_index, static_cast<int>(scene_roots.size()))) {
            remove_scene_node();
            m_scene_root = scene_roots.at(static_cast<std::size_t>(scene_index));
            m_material.reset();
            mark_dirty();
        }
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(m_scene_root ? m_scene_root->get_name().c_str() : "(no scene)");

    // Material selection
    if (m_scene_root) {
        const std::shared_ptr<Content_library> library = m_scene_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                int material_index = 0;
                for (std::size_t i = 0, end = materials.size(); i < end; ++i) {
                    if (materials[i] == m_material) {
                        material_index = static_cast<int>(i);
                        break;
                    }
                }
                if (imgui_index_stepper("material", material_index, static_cast<int>(materials.size()))) {
                    m_material = materials.at(static_cast<std::size_t>(material_index));
                    mark_dirty();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(m_material ? m_material->get_name().c_str() : "(no material)");
            }
        }
    }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_input(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    } else {
        ImGui::TextUnformatted("(no geometry)");
    }
}

void Geometry_output_node::write_parameters(nlohmann::json& out) const
{
    out["name"] = m_scene_node_name;
    if (m_scene_root) {
        out["scene"] = m_scene_root->get_name();
    }
    if (m_material) {
        out["material"] = m_material->get_name();
    }
}

void Geometry_output_node::read_parameters(const nlohmann::json& in)
{
    const std::string scene_node_name = in.value("name", m_scene_node_name);
    if (scene_node_name != m_scene_node_name) {
        m_scene_node_name = scene_node_name;
        apply_scene_node_name();
    }
    const std::string scene_name = in.value("scene", "");
    if (!scene_name.empty()) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (scene_root->get_name() == scene_name) {
                if (m_scene_root != scene_root) {
                    // Release the scene node from the previous scene; the
                    // next evaluate() recreates it in the new one.
                    remove_scene_node();
                    m_material.reset();
                    m_scene_root = scene_root;
                }
                break;
            }
        }
    }
    const std::string material_name = in.value("material", "");
    if (!material_name.empty() && m_scene_root) {
        const std::shared_ptr<Content_library> library = m_scene_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            for (const std::shared_ptr<erhe::primitive::Material>& material : materials) {
                if (material->get_name() == material_name) {
                    m_material = material;
                    break;
                }
            }
        }
    }
    mark_dirty();
}

}
