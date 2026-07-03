#include "geometry_graph/nodes/geometry_output_node.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <geogram/mesh/mesh.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <cfloat>
#include <mutex>

namespace editor {

namespace {

// Motion modes exposed on the output node, in stepper order. Serialized
// by name; maps to erhe::physics::Motion_mode e_static /
// e_kinematic_physical / e_dynamic.
constexpr const char* c_physics_motion_names[] = { "Static", "Kinematic", "Dynamic" };

[[nodiscard]] auto physics_motion_mode_to_string(erhe::physics::Motion_mode motion_mode) -> const char*
{
    switch (motion_mode) {
        case erhe::physics::Motion_mode::e_dynamic:             return "dynamic";
        case erhe::physics::Motion_mode::e_kinematic_physical:  return "kinematic";
        default:                                                return "static";
    }
}

[[nodiscard]] auto physics_motion_mode_from_string(const std::string& name, erhe::physics::Motion_mode fallback) -> erhe::physics::Motion_mode
{
    if (name == "dynamic")   { return erhe::physics::Motion_mode::e_dynamic; }
    if (name == "kinematic") { return erhe::physics::Motion_mode::e_kinematic_physical; }
    if (name == "static")    { return erhe::physics::Motion_mode::e_static; }
    return fallback;
}

}

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
    remove_node_physics();
    if (m_node) {
        m_node->set_parent({});
    }
    m_node.reset();
    m_mesh.reset();
}

void Geometry_output_node::remove_node_physics()
{
    if (m_node_physics && m_node) {
        m_node->detach(m_node_physics.get());
    }
    m_node_physics.reset();
}

void Geometry_output_node::update_node_physics()
{
    if (!m_physics_enabled || !m_node || !m_evaluated_collision_shape) {
        remove_node_physics();
        return;
    }

    if (!m_node_physics) {
        const erhe::physics::IRigid_body_create_info create_info{
            .collision_shape = m_evaluated_collision_shape,
            .debug_label     = m_scene_node_name,
            .motion_mode     = m_physics_motion_mode
        };
        m_node_physics = std::make_shared<Node_physics>(create_info);
        m_node->attach(m_node_physics);
    } else {
        m_node_physics->set_collision_shape(m_evaluated_collision_shape);
        m_node_physics->set_motion_mode(m_physics_motion_mode);
    }
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

    m_evaluated_geometry.reset();
    m_evaluated_primitive.reset();
    m_evaluated_collision_shape.reset();
    m_evaluated_valid = true;

    // A facet-less geometry (e.g. an out-of-range Conway operation index,
    // a boolean of empty inputs) has nothing to render; treat it like a
    // disconnected input rather than asking the primitive builder to
    // build zero-index buffers (which it refuses with a VERIFY abort).
    // A valid-but-empty result makes apply_evaluated_to_scene() clear
    // the scene mesh.
    if (!source || (source->get_mesh().facets.nb() == 0)) {
        return;
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
        // Leave the scene mesh as it is rather than clearing it.
        m_evaluated_valid = false;
        return;
    }
    if (!primitive->make_raytrace()) {
        log_graph_editor->warn("Geometry_output_node: failed to build raytrace shape for '{}'", render_geometry->get_name());
    }

    m_evaluated_geometry  = render_geometry;
    m_evaluated_primitive = primitive;

    if (m_physics_enabled) {
        GEO::Mesh convex_hull{};
        const bool convex_hull_ok = erhe::geometry::make_convex_hull(render_geometry->get_mesh(), convex_hull);
        if (!convex_hull_ok) {
            log_graph_editor->warn("Geometry_output_node: failed to build convex hull collision shape for '{}'", render_geometry->get_name());
        } else {
            std::vector<float> coordinates;
            coordinates.resize(3 * convex_hull.vertices.nb());
            for (GEO::index_t vertex : convex_hull.vertices) {
                const GEO::vec3f p = erhe::geometry::get_pointf(convex_hull.vertices, vertex);
                coordinates[(3 * vertex) + 0] = p.x;
                coordinates[(3 * vertex) + 1] = p.y;
                coordinates[(3 * vertex) + 2] = p.z;
            }
            m_evaluated_collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                coordinates.data(),
                static_cast<int>(convex_hull.vertices.nb()),
                static_cast<int>(3 * sizeof(float))
            );
        }
    }
}

void Geometry_output_node::take_evaluated(Geometry_output_node& from)
{
    m_evaluated_valid           = from.m_evaluated_valid;
    m_evaluated_geometry        = std::move(from.m_evaluated_geometry);
    m_evaluated_primitive       = std::move(from.m_evaluated_primitive);
    m_evaluated_collision_shape = std::move(from.m_evaluated_collision_shape);
    from.m_evaluated_valid = false;
}

void Geometry_output_node::apply_evaluated_to_scene()
{
    if (!m_evaluated_valid) {
        return;
    }
    m_evaluated_valid = false;

    if (!m_scene_root) {
        m_scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (!m_scene_root) {
        m_evaluated_geometry.reset();
        m_evaluated_primitive.reset();
        m_evaluated_collision_shape.reset();
        return;
    }

    if (!m_evaluated_geometry) {
        if (m_mesh) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root->item_host_mutex};
            m_mesh->clear_primitives();
            remove_node_physics();
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
    m_mesh->add_primitive(m_evaluated_primitive, m_material);
    update_node_physics();

    m_evaluated_geometry.reset();
    m_evaluated_primitive.reset();
    m_evaluated_collision_shape.reset();
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

    // Optional physics: convex hull collision shape + rigid body
    if (ImGui::Checkbox("Physics", &m_physics_enabled)) {
        mark_dirty();
    }
    if (m_physics_enabled) {
        ImGui::SameLine();
        int motion_index =
            (m_physics_motion_mode == erhe::physics::Motion_mode::e_dynamic            ) ? 2 :
            (m_physics_motion_mode == erhe::physics::Motion_mode::e_kinematic_physical ) ? 1 : 0;
        if (imgui_enum_stepper("physics_motion", motion_index, c_physics_motion_names, 3)) {
            m_physics_motion_mode =
                (motion_index == 2) ? erhe::physics::Motion_mode::e_dynamic :
                (motion_index == 1) ? erhe::physics::Motion_mode::e_kinematic_physical :
                                      erhe::physics::Motion_mode::e_static;
            mark_dirty();
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
    out["physics"] = m_physics_enabled;
    out["physics_motion"] = physics_motion_mode_to_string(m_physics_motion_mode);
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
    m_physics_enabled = in.value("physics", m_physics_enabled);
    m_physics_motion_mode = physics_motion_mode_from_string(in.value("physics_motion", ""), m_physics_motion_mode);
    mark_dirty();
}

}
