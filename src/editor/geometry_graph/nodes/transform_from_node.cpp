#include "geometry_graph/nodes/transform_from_node.hpp"

#include "app_context.hpp"
#include "graph_editor/graph_editor_widgets.hpp"
#include "windows/item_reference.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

Transform_from_node::Transform_from_node(App_context& context)
    : Geometry_graph_node{"Transform From Node"}
    , m_context          {context}
{
    make_input_pin(Geometry_pin_key::geometry,  "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
    m_transform_node_reference.set_user_label("transform-from-node driver");
}

void Transform_from_node::set_transform_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    m_transform_node_reference.adopt(*m_context.asset_manager, node);
    capture_transform();
    mark_dirty();
}

auto Transform_from_node::get_referenced_scene_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_transform_node_reference.get_as<erhe::scene::Node>();
}

void Transform_from_node::resolve_transform_reference()
{
    // Main thread only (the manager verifies).
    if (m_transform_node_reference.get()) {
        return;
    }
    if (m_transform_node_reference.get_key().name.empty()) {
        return;
    }
    m_transform_node_reference.resolve(*m_context.asset_manager);
}

auto Transform_from_node::capture_transform() -> bool
{
    const std::shared_ptr<erhe::scene::Node> node = m_transform_node_reference.get_as<erhe::scene::Node>();
    const glm::mat4 transform =
        node
            ? ((m_space == Space::world) ? node->world_from_node() : node->parent_from_node())
            : glm::mat4{1.0f};
    if (transform == m_captured_transform) {
        return false;
    }
    m_captured_transform = transform;
    return true;
}

void Transform_from_node::update_live()
{
    // Per-frame: retry a deferred driver reference (scene_local misses do
    // not latch) and track the driver's live transform, so dragging the
    // driver node in the viewport re-poses the geometry.
    resolve_transform_reference();
    if (capture_transform()) {
        mark_dirty();
    }
}

void Transform_from_node::prepare_for_evaluation()
{
    update_live();
}

void Transform_from_node::capture_evaluation_state(const Geometry_graph_node& live_node)
{
    // Shadow clones copy the live node's captured transform instead of
    // resolving: a shadow must not touch the asset manager or scene state
    // (it can be destroyed off the main thread).
    const Transform_from_node* live = dynamic_cast<const Transform_from_node*>(&live_node);
    if (live != nullptr) {
        m_captured_transform = live->m_captured_transform;
        m_space              = live->m_space;
    }
}

void Transform_from_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }

    if (m_captured_transform == glm::mat4{1.0f}) {
        // Copy-on-write: an identity transform (including "no driver set")
        // passes the upstream geometry through unchanged instead of copying
        // it. Safe because nodes never mutate shared upstream geometry.
        set_output(0, Geometry_payload{.value = source});
        return;
    }

    std::shared_ptr<erhe::geometry::Geometry> destination = std::make_shared<erhe::geometry::Geometry>("transformed");
    destination->copy_with_transform(*source.get(), erhe::geometry::to_geo_mat4f(m_captured_transform));
    set_output(0, Geometry_payload{.value = destination});
}

void Transform_from_node::imgui()
{
    // Driver row: the shared item-reference field is both a drop target for
    // a scene node dragged from the item tree and a drag source / clear
    // button for the current driver. It self-scales inside the node table
    // cell (it sizes from GetContentRegionAvail), so no content_scale()
    // multiplication here. Candidates (picker popup) stay empty on the
    // canvas: ax::NodeEditor popups need Suspend/Resume.
    resolve_transform_reference();
    ImGui::TextUnformatted("Transform node");
    std::shared_ptr<erhe::Item_base> value = m_transform_node_reference.get();
    const std::string none_text =
        !m_transform_node_reference.get_key().name.empty()
            ? ("(unresolved: " + m_transform_node_reference.get_key().name + ")")
            : std::string{"(drop a scene node)"};
    Item_reference_options options;
    options.none_text          = none_text.c_str();
    options.show_select_button = false; // node-editor content stays out of the global selection (issue #252)
    if (item_reference_imgui(m_context, "##transform_from_node", value, erhe::scene::Node::get_static_type(), options)) {
        set_transform_node(std::dynamic_pointer_cast<erhe::scene::Node>(value));
    }
    if (!value && !m_transform_node_reference.get_key().name.empty()) {
        // The widget's clear button shows only for a resolved value; offer
        // one for an unresolved key too so a stale reference can be dropped.
        ImGui::SameLine();
        if (ImGui::Button("x##clear_unresolved")) {
            set_transform_node({});
        }
    }

    const char* space_names[] = { "Local (parent-relative)", "World" };
    int space = static_cast<int>(m_space);
    if (imgui_enum_combo("space", space, space_names, IM_ARRAYSIZE(space_names), content_scale())) {
        m_space = static_cast<Space>(space);
        capture_transform(); // re-capture under the new space
        mark_dirty();
    }
}

void Transform_from_node::write_parameters(nlohmann::json& out) const
{
    // Written even while unresolved (and when empty), so an unresolved
    // reference survives save and an undo to the no-driver state clears it.
    out["transform_node"] = m_transform_node_reference.get_key().name;
    out["space"]          = static_cast<int>(m_space);
}

void Transform_from_node::read_parameters(const nlohmann::json& in)
{
    if (in.contains("transform_node")) {
        // Store the key only; resolution is deferred to the main thread
        // (read_parameters can run off it - shadow snapshots). The captured
        // transform resets so a cleared / changed driver does not leave a
        // stale transform; update_live() re-captures after resolution.
        const std::string name = in.value("transform_node", std::string{});
        if (name != m_transform_node_reference.get_key().name) {
            Asset_key key;
            key.scope = Asset_scope::scene_local;
            key.type  = Asset_type::node;
            key.name  = name;
            m_transform_node_reference.set_key(key);
            m_captured_transform = glm::mat4{1.0f};
        }
    }
    const int space = std::clamp(in.value("space", static_cast<int>(m_space)), 0, 1);
    if (space != static_cast<int>(m_space)) {
        m_space = static_cast<Space>(space);
        m_captured_transform = glm::mat4{1.0f}; // update_live() re-captures in the new space
    }
    mark_dirty();
}

}
