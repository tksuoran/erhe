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
}

void Brush_geometry_node::set_brush(const std::shared_ptr<Brush>& brush)
{
    m_brush = brush;
    // Main thread: get_geometry() may lazily run the brush's geometry
    // generator, which is not safe on the evaluation worker.
    m_source_geometry = brush ? brush->get_geometry() : std::shared_ptr<erhe::geometry::Geometry>{};
    mark_dirty();
}

void Brush_geometry_node::resolve_brush_by_name(const std::string& name)
{
    m_brush.reset();
    m_source_geometry.reset();
    if (name.empty()) {
        return;
    }
    // The owning asset's scene first; then every scene. The all-scene sweep
    // matters for shadow clones, which have no owning graph mesh.
    const auto try_library = [this, &name](const std::shared_ptr<Scene_root>& scene_root) -> bool {
        if (!scene_root) {
            return false;
        }
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (!library || !library->brushes) {
            return false;
        }
        const std::vector<std::shared_ptr<Brush>>& brushes = library->brushes->get_all<Brush>();
        for (const std::shared_ptr<Brush>& brush : brushes) {
            if (brush && (brush->get_name() == name)) {
                set_brush(brush);
                return true;
            }
        }
        return false;
    };
    if (try_library(get_hosting_scene_root(get_owning_graph_mesh().get()))) {
        return;
    }
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (try_library(scene_root)) {
                return;
            }
        }
    }
    log_graph_editor->warn("Brush_geometry_node: brush '{}' not found in any content library", name);
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
                    if (brushes[i] == m_brush) {
                        brush_index = static_cast<int>(i);
                        break;
                    }
                }
                if (imgui_index_stepper("brush", brush_index, static_cast<int>(brushes.size()))) {
                    set_brush(brushes.at(static_cast<std::size_t>(brush_index)));
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(m_brush ? m_brush->get_name().c_str() : "(no brush)");
            }
        }
    }

    // The captured geometry is a snapshot; re-capture on demand.
    if (m_brush && ImGui::Button("Refresh")) {
        set_brush(m_brush);
    }
    show_geometry_stats(m_source_geometry);
}

void Brush_geometry_node::write_parameters(nlohmann::json& out) const
{
    if (m_brush) {
        out["brush"] = m_brush->get_name();
    }
}

void Brush_geometry_node::read_parameters(const nlohmann::json& in)
{
    const std::string brush_name = in.value("brush", m_brush ? m_brush->get_name() : std::string{});
    resolve_brush_by_name(brush_name);
    mark_dirty();
}

// Scene_mesh_geometry_node

Scene_mesh_geometry_node::Scene_mesh_geometry_node(App_context& context)
    : Geometry_graph_node{"Scene Mesh"}
    , m_context          {context}
{
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Scene_mesh_geometry_node::set_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    m_mesh = mesh;
    m_source_geometry.reset();
    if (mesh) {
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
    mark_dirty();
}

void Scene_mesh_geometry_node::resolve_mesh_by_name(const std::string& name)
{
    m_mesh.reset();
    m_source_geometry.reset();
    if (name.empty()) {
        return;
    }
    // The owning asset's scene first; then every scene (shadow clones have
    // no owning graph mesh).
    const auto try_scene = [this, &name](const std::shared_ptr<Scene_root>& scene_root) -> bool {
        if (!scene_root) {
            return false;
        }
        const erhe::scene::Mesh_layer* content_layer = scene_root->layers().content();
        if (content_layer == nullptr) {
            return false;
        }
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
            if (mesh && (mesh->get_name() == name)) {
                set_mesh(mesh);
                return true;
            }
        }
        return false;
    };
    if (try_scene(get_hosting_scene_root(get_owning_graph_mesh().get()))) {
        return;
    }
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (try_scene(scene_root)) {
                return;
            }
        }
    }
    log_graph_editor->warn("Scene_mesh_geometry_node: mesh '{}' not found in any scene", name);
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
                if (meshes[i] == m_mesh) {
                    mesh_index = static_cast<int>(i);
                    break;
                }
            }
            if (imgui_index_stepper("mesh", mesh_index, static_cast<int>(meshes.size()))) {
                set_mesh(meshes.at(static_cast<std::size_t>(mesh_index)));
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(m_mesh ? m_mesh->get_name().c_str() : "(no mesh)");
        }
    }

    // The captured geometry is a snapshot; re-capture on demand (e.g. after
    // mesh edit operations).
    if (m_mesh && ImGui::Button("Refresh")) {
        set_mesh(m_mesh);
    }
    show_geometry_stats(m_source_geometry);
}

void Scene_mesh_geometry_node::write_parameters(nlohmann::json& out) const
{
    if (m_mesh) {
        out["mesh"] = m_mesh->get_name();
    }
}

void Scene_mesh_geometry_node::read_parameters(const nlohmann::json& in)
{
    const std::string mesh_name = in.value("mesh", m_mesh ? m_mesh->get_name() : std::string{});
    resolve_mesh_by_name(mesh_name);
    mark_dirty();
}

}
