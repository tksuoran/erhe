#include "geometry_graph/nodes/geometry_source_nodes.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

#include <geogram/mesh/mesh.h>

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

namespace editor {

namespace {

// Copy + graph-process a captured source geometry: the source is shared with
// the referenced item (brush / mesh primitive) and must not be mutated, and
// downstream operation nodes need connectivity / edges present.
[[nodiscard]] auto make_output_geometry(const std::shared_ptr<erhe::geometry::Geometry>& source) -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(source->get_name());
    GEO::mat4f identity;
    identity.load_identity();
    geometry->copy_with_transform(*source.get(), identity);
    process_for_graph(*geometry.get());
    return geometry;
}

void show_geometry_stats(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
{
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    } else {
        ImGui::TextUnformatted("(no geometry)");
    }
}

}

// Brush_geometry_node

Brush_geometry_node::Brush_geometry_node(App_context& context)
    : Geometry_graph_node{"Brush"}
    , m_context          {context}
{
    make_output_pin(Geometry_pin_key::geometry, "geometry");
    m_brush_reference.set_user_label("graph brush source node");
}

void Brush_geometry_node::set_brush(const std::shared_ptr<Brush>& brush)
{
    m_brush_reference.adopt(*m_context.asset_manager, brush);
    // Main thread: get_geometry() may lazily run the brush's geometry
    // generator, which is not safe on the evaluation worker.
    m_source_geometry = brush ? brush->get_geometry() : std::shared_ptr<erhe::geometry::Geometry>{};
    mark_dirty();
}

void Brush_geometry_node::resolve_reference()
{
    // Main thread only (the manager verifies). A successful resolve marks
    // the node dirty so the next evaluation picks up the captured geometry.
    if (m_brush_reference.get()) {
        if (!m_source_geometry) {
            const std::shared_ptr<Brush> brush = m_brush_reference.get_as<Brush>();
            if (brush) {
                m_source_geometry = brush->get_geometry();
            }
        }
        return;
    }
    if (m_brush_reference.get_key().name.empty()) {
        return;
    }
    m_brush_reference.resolve(*m_context.asset_manager);
    const std::shared_ptr<Brush> brush = m_brush_reference.get_as<Brush>();
    if (brush) {
        m_source_geometry = brush->get_geometry();
        mark_dirty();
    }
}

void Brush_geometry_node::prepare_for_evaluation()
{
    resolve_reference();
    if (!m_brush_reference.get() && !m_brush_reference.get_key().name.empty()) {
        log_graph_editor->warn("Brush_geometry_node: brush '{}' not found (unresolved asset reference)", m_brush_reference.get_key().name);
    }
}

void Brush_geometry_node::capture_evaluation_state(const Geometry_graph_node& live_node)
{
    // Shadow clones copy the live node's captured geometry instead of
    // re-resolving: a shadow must not touch the asset manager or hold a
    // usership (it can be destroyed off the main thread).
    const Brush_geometry_node* live = dynamic_cast<const Brush_geometry_node*>(&live_node);
    if (live != nullptr) {
        m_source_geometry = live->m_source_geometry;
    }
}

void Brush_geometry_node::evaluate(Geometry_graph&)
{
    if (!m_source_geometry || (m_source_geometry->get_mesh().facets.nb() == 0)) {
        set_output(0, Geometry_payload{});
        return;
    }
    set_output(0, Geometry_payload{.value = make_output_geometry(m_source_geometry)});
}

void Brush_geometry_node::imgui()
{
    // Per-frame retry of a deferred / broken reference while the node is
    // visible (the R2 slot-resolve cadence; scene_local misses do not latch).
    resolve_reference();
    const std::shared_ptr<Brush> current_brush = m_brush_reference.get_as<Brush>();

    // Brush selection, from the owning asset's scene content library (single
    // scene fallback) - mirrors the output node's material picker.
    std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(get_owning_graph_mesh().get());
    if (!scene_root) {
        scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (scene_root) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library && library->brushes) {
            const std::vector<std::shared_ptr<Brush>>& brushes = library->brushes->get_all<Brush>();
            if (!brushes.empty()) {
                int brush_index = 0;
                for (std::size_t i = 0, end = brushes.size(); i < end; ++i) {
                    if (brushes[i] == current_brush) {
                        brush_index = static_cast<int>(i);
                        break;
                    }
                }
                if (imgui_index_stepper("brush", brush_index, static_cast<int>(brushes.size()))) {
                    set_brush(brushes.at(static_cast<std::size_t>(brush_index)));
                }
                ImGui::SameLine();
                if (current_brush) {
                    ImGui::TextUnformatted(current_brush->get_name().c_str());
                } else if (!m_brush_reference.get_key().name.empty()) {
                    ImGui::Text("(unresolved: %s)", m_brush_reference.get_key().name.c_str());
                } else {
                    ImGui::TextUnformatted("(no brush)");
                }
            }
        }
    }

    // The captured geometry is a snapshot; re-capture on demand.
    if (current_brush && ImGui::Button("Refresh")) {
        set_brush(current_brush);
    }
    show_geometry_stats(m_source_geometry);
}

void Brush_geometry_node::write_parameters(nlohmann::json& out) const
{
    // The key is written even while unresolved, so a reference that never
    // resolved this session survives the next save (R4: no silent loss).
    const std::string& name = m_brush_reference.get_key().name;
    if (!name.empty()) {
        out["brush"] = name;
    }
}

void Brush_geometry_node::read_parameters(const nlohmann::json& in)
{
    if (in.contains("brush")) {
        // Store the key only; resolution is deferred to the main thread
        // (read_parameters can run off it - group subgraph loads evaluate
        // on the worker).
        Asset_key key;
        key.scope = Asset_scope::scene_local;
        key.type  = Asset_type::brush;
        key.name  = in.value("brush", std::string{});
        m_brush_reference.set_key(key);
        m_source_geometry.reset();
    }
    mark_dirty();
}

// Scene_mesh_geometry_node

Scene_mesh_geometry_node::Scene_mesh_geometry_node(App_context& context)
    : Geometry_graph_node{"Scene Mesh"}
    , m_context          {context}
{
    make_output_pin(Geometry_pin_key::geometry, "geometry");
    m_mesh_reference.set_user_label("graph scene mesh source node");
}

void Scene_mesh_geometry_node::set_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    m_mesh_reference.adopt(*m_context.asset_manager, mesh);
    capture_source_geometry();
    mark_dirty();
}

void Scene_mesh_geometry_node::capture_source_geometry()
{
    m_source_geometry.reset();
    const std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh_reference.get_as<erhe::scene::Mesh>();
    if (!mesh) {
        return;
    }
    // First primitive with a render-shape geometry. Main thread:
    // get_geometry() may lazily build the geometry (e.g. from an
    // imported triangle soup), which is not safe on the worker.
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        if (!mesh_primitive.primitive || !mesh_primitive.primitive->render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = mesh_primitive.primitive->render_shape->get_geometry();
        if (geometry) {
            m_source_geometry = geometry;
            break;
        }
    }
    if (!m_source_geometry) {
        log_graph_editor->warn("Scene_mesh_geometry_node: mesh '{}' has no primitive geometry", mesh->get_name());
    }
}

void Scene_mesh_geometry_node::resolve_reference()
{
    // Main thread only (the manager verifies). A successful resolve marks
    // the node dirty so the next evaluation picks up the captured geometry.
    if (m_mesh_reference.get()) {
        if (!m_source_geometry) {
            capture_source_geometry();
        }
        return;
    }
    if (m_mesh_reference.get_key().name.empty()) {
        return;
    }
    m_mesh_reference.resolve(*m_context.asset_manager);
    if (m_mesh_reference.get()) {
        capture_source_geometry();
        mark_dirty();
    }
}

void Scene_mesh_geometry_node::prepare_for_evaluation()
{
    resolve_reference();
    if (!m_mesh_reference.get() && !m_mesh_reference.get_key().name.empty()) {
        log_graph_editor->warn("Scene_mesh_geometry_node: mesh '{}' not found (unresolved asset reference)", m_mesh_reference.get_key().name);
    }
}

void Scene_mesh_geometry_node::capture_evaluation_state(const Geometry_graph_node& live_node)
{
    // Shadow clones copy the live node's captured geometry instead of
    // re-resolving: a shadow must not touch the asset manager or hold a
    // usership (it can be destroyed off the main thread).
    const Scene_mesh_geometry_node* live = dynamic_cast<const Scene_mesh_geometry_node*>(&live_node);
    if (live != nullptr) {
        m_source_geometry = live->m_source_geometry;
    }
}

void Scene_mesh_geometry_node::evaluate(Geometry_graph&)
{
    if (!m_source_geometry || (m_source_geometry->get_mesh().facets.nb() == 0)) {
        set_output(0, Geometry_payload{});
        return;
    }
    set_output(0, Geometry_payload{.value = make_output_geometry(m_source_geometry)});
}

void Scene_mesh_geometry_node::imgui()
{
    // Per-frame retry of a deferred / broken reference while the node is
    // visible (the R2 slot-resolve cadence; scene_local misses do not latch).
    resolve_reference();
    const std::shared_ptr<erhe::scene::Mesh> current_mesh = m_mesh_reference.get_as<erhe::scene::Mesh>();

    // Mesh selection, from the owning asset's scene (single scene fallback).
    std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(get_owning_graph_mesh().get());
    if (!scene_root) {
        scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (scene_root) {
        const erhe::scene::Mesh_layer* content_layer = scene_root->layers().content();
        if ((content_layer != nullptr) && !content_layer->meshes.empty()) {
            const std::vector<std::shared_ptr<erhe::scene::Mesh>>& meshes = content_layer->meshes;
            int mesh_index = 0;
            for (std::size_t i = 0, end = meshes.size(); i < end; ++i) {
                if (meshes[i] == current_mesh) {
                    mesh_index = static_cast<int>(i);
                    break;
                }
            }
            if (imgui_index_stepper("mesh", mesh_index, static_cast<int>(meshes.size()))) {
                set_mesh(meshes.at(static_cast<std::size_t>(mesh_index)));
            }
            ImGui::SameLine();
            if (current_mesh) {
                ImGui::TextUnformatted(current_mesh->get_name().c_str());
            } else if (!m_mesh_reference.get_key().name.empty()) {
                ImGui::Text("(unresolved: %s)", m_mesh_reference.get_key().name.c_str());
            } else {
                ImGui::TextUnformatted("(no mesh)");
            }
        }
    }

    // The captured geometry is a snapshot; re-capture on demand (e.g. after
    // mesh edit operations).
    if (current_mesh && ImGui::Button("Refresh")) {
        set_mesh(current_mesh);
    }
    show_geometry_stats(m_source_geometry);
}

void Scene_mesh_geometry_node::write_parameters(nlohmann::json& out) const
{
    // The key is written even while unresolved (R4: no silent loss).
    const std::string& name = m_mesh_reference.get_key().name;
    if (!name.empty()) {
        out["mesh"] = name;
    }
}

void Scene_mesh_geometry_node::read_parameters(const nlohmann::json& in)
{
    if (in.contains("mesh")) {
        // Store the key only; resolution is deferred to the main thread.
        Asset_key key;
        key.scope = Asset_scope::scene_local;
        key.type  = Asset_type::mesh;
        key.name  = in.value("mesh", std::string{});
        m_mesh_reference.set_key(key);
        m_source_geometry.reset();
    }
    mark_dirty();
}

}
