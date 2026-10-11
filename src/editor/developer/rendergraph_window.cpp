#include "developer/rendergraph_window.hpp"

#include "app_context.hpp"
#include "graph_editor/graph_editor_node.hpp"

#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/resource_routing.hpp"
#include "erhe_utility/thread_roles.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <mutex>

namespace editor {

namespace {

[[nodiscard]] auto rendergraph_pin_key_color(const std::size_t key) -> ImU32
{
    switch (static_cast<int>(key)) {
        case erhe::rendergraph::Rendergraph_node_key::viewport_texture:     return IM_COL32(102, 128, 204, 255);
        case erhe::rendergraph::Rendergraph_node_key::shadow_maps:          return IM_COL32(153, 153, 153, 255);
        case erhe::rendergraph::Rendergraph_node_key::depth_visualization:  return IM_COL32( 26, 204, 204, 255);
        case erhe::rendergraph::Rendergraph_node_key::texture_for_gui:      return IM_COL32(102, 204, 128, 255);
        case erhe::rendergraph::Rendergraph_node_key::rendertarget_texture: return IM_COL32(204, 128, 102, 255);
        default:                                                            return IM_COL32(255,   0, 255, 255);
    }
}

// One output of a rendergraph node as a texture reference for the
// thumbnail. The node's output textures are created and replaced by the
// render phase, which may run while the ImGui phase draws this window, so
// the window never reads them: the render thread resolves the reference
// when it draws the thumbnail and records the size it found, and the window
// sizes the next thumbnail by that record (a one-frame-late copy). Outputs
// that are not 2D color textures resolve to none and record 0 x 0.
class Rendergraph_output_reference : public erhe::graphics::Texture_reference
{
public:
    Rendergraph_output_reference(erhe::rendergraph::Rendergraph_node& node, const int key)
        : m_node{node.weak_from_this()}
        , m_key {key}
    {
    }

    // Render thread.
    auto get_referenced_texture() const -> const erhe::graphics::Texture* override
    {
        erhe::utility::Thread_roles::assert_render_thread();
        const erhe::graphics::Texture* result = nullptr;
        const std::shared_ptr<erhe::rendergraph::Rendergraph_node> node = std::dynamic_pointer_cast<erhe::rendergraph::Rendergraph_node>(m_node.lock());
        if (node) {
            const std::shared_ptr<erhe::graphics::Texture> texture = node->get_producer_output_texture(m_key);
            if (
                texture &&
                (texture->get_texture_type() == erhe::graphics::Texture_type::texture_2d) &&
                (texture->get_width () >= 1) &&
                (texture->get_height() >= 1) &&
                erhe::dataformat::has_color(texture->get_pixelformat())
            ) {
                result = texture.get();
            }
        }
        std::lock_guard<std::mutex> lock{m_mutex};
        m_width  = (result != nullptr) ? result->get_width () : 0;
        m_height = (result != nullptr) ? result->get_height() : 0;
        return result;
    }

    // Any thread: the size the render thread resolved last; 0 x 0 for none.
    void get_resolved_size(int& width, int& height) const
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        width  = m_width;
        height = m_height;
    }

    [[nodiscard]] auto is_valid() const -> bool { return !m_node.expired(); }

private:
    // Weak: a node is not kept alive by its thumbnail (the ImGui renderer
    // keeps the references of the frame it drew last). A node that is not
    // owned by a shared_ptr has no thumbnail.
    std::weak_ptr<erhe::Item_base> m_node;
    int                            m_key;
    mutable std::mutex             m_mutex;
    mutable int                    m_width {0};
    mutable int                    m_height{0};
};

} // namespace

// Proxy Graph_editor_node for one rendergraph node: replicates the real
// node's pins (same order, so slots match) and renders through the shared
// Graph_editor_node::node_editor() path, so rendergraph nodes look and
// behave (drag, select, resize) exactly like geometry / texture graph nodes.
// View-only: no parameters, so the parameter-undo hook never fires.
class Rendergraph_editor_node : public Graph_editor_node
{
public:
    explicit Rendergraph_editor_node(erhe::rendergraph::Rendergraph_node& rendergraph_node)
        : Graph_editor_node    {rendergraph_node.get_name().c_str()}
        , m_rendergraph_node   {&rendergraph_node}
        , m_rendergraph_node_id{rendergraph_node.get_id()}
    {
        for (const erhe::graph::Pin& pin : rendergraph_node.get_input_pins()) {
            base_make_input_pin(pin.get_key(), pin.get_name());
        }
        for (const erhe::graph::Pin& pin : rendergraph_node.get_output_pins()) {
            base_make_output_pin(pin.get_key(), pin.get_name());
            m_output_references.push_back(std::make_shared<Rendergraph_output_reference>(rendergraph_node, static_cast<int>(pin.get_key())));
        }
        // Rendergraph pin names are long ("rendertarget texture", "depth
        // visualization"), so reserve wider pin label columns and a wider
        // node than the graph editors' defaults.
        set_pin_label_width(150.0f);
        set_ui_size(520.0f, 0.0f);
    }

    [[nodiscard]] auto get_rendergraph_node() const -> erhe::rendergraph::Rendergraph_node* { return m_rendergraph_node; }
    [[nodiscard]] auto get_rendergraph_node_id() const -> std::size_t { return m_rendergraph_node_id; }

    void imgui() override
    {
        if (!m_rendergraph_node->is_enabled()) {
            ImGui::TextUnformatted("(disabled)");
        }
    }

protected:
    auto pin_key_color(const std::size_t key) const -> ImU32 override
    {
        return rendergraph_pin_key_color(key);
    }

    void commit_parameter_operation(App_context&, std::string&&, std::string&&) override
    {
        // View-only: the node has no editable parameters.
    }

    void after_node_content(App_context& app_context) override
    {
        // Producer output texture thumbnails, fit to the node's content space
        // like the texture graph previews (grow with node resize and zoom).
        if (app_context.imgui_renderer == nullptr) {
            return;
        }
        for (const std::shared_ptr<Rendergraph_output_reference>& reference : m_output_references) {
            if (!reference->is_valid()) {
                continue;
            }
            int texture_width {0};
            int texture_height{0};
            reference->get_resolved_size(texture_width, texture_height);
            if ((texture_width < 1) || (texture_height < 1)) {
                // Nothing displayable resolved yet: a 1 x 1 transparent draw
                // keeps the render thread resolving the output, so the
                // thumbnail appears once the output is a 2D color texture.
                app_context.imgui_renderer->image(
                    erhe::imgui::Draw_texture_parameters{
                        .texture_reference = reference,
                        .width             = 1,
                        .height            = 1,
                        .tint_color        = glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
                        .debug_label       = erhe::utility::Debug_label{"Rendergraph_editor_node preview probe"}
                    }
                );
                continue;
            }
            const float aspect = static_cast<float>(texture_width) / static_cast<float>(texture_height);
            const float width  = get_preview_fit_size();
            // Render-to-texture UV orientation (get_rtt_uv0/1), like the
            // texture graph previews - without it the thumbnails show
            // upside down on Vulkan.
            app_context.imgui_renderer->image(
                erhe::imgui::Draw_texture_parameters{
                    .texture_reference = reference,
                    .width             = static_cast<int>(width),
                    .height            = static_cast<int>(width / aspect),
                    .uv0               = app_context.imgui_renderer->get_rtt_uv0(),
                    .uv1               = app_context.imgui_renderer->get_rtt_uv1(),
                    .debug_label       = erhe::utility::Debug_label{"Rendergraph_editor_node preview"}
                }
            );
        }
    }

private:
    erhe::rendergraph::Rendergraph_node*                       m_rendergraph_node;
    std::size_t                                                m_rendergraph_node_id; // detects pointer reuse after destroy + recreate
    std::vector<std::shared_ptr<Rendergraph_output_reference>> m_output_references;   // one per output pin
};

Rendergraph_window::Rendergraph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : Graph_editor_window_base{imgui_renderer, imgui_windows, "Render Graph", "rendergraph", true}
    , m_context               {app_context}
{
    m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);

    // Same canvas setup as the geometry / texture graph editors: link
    // arrowheads (the pin PivotAlignment leaves a gap for them) and
    // interactive node resizing (adopted by apply_node_resize()).
    ax::NodeEditor::Style& style = m_node_editor->GetStyle();
    style.PinArrowSize  = 14.0f;
    style.PinArrowWidth = 14.0f;
    m_node_editor->EnableNodeResize(true);
}

Rendergraph_window::~Rendergraph_window() noexcept
{
}

auto Rendergraph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void Rendergraph_window::controls_imgui()
{
    // View-only window: no node palette.
}

void Rendergraph_window::collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out)
{
    for (const auto& entry : m_proxy_nodes) {
        const std::shared_ptr<Rendergraph_editor_node>& proxy = entry.second;
        if (m_node_editor->IsNodeSelected(ax::NodeEditor::NodeId{proxy->get_id()})) {
            out.push_back(proxy);
        }
    }
}

auto Rendergraph_window::find_graph_editor_node(const std::size_t node_id) -> std::shared_ptr<Graph_editor_node>
{
    for (const auto& entry : m_proxy_nodes) {
        const std::shared_ptr<Rendergraph_editor_node>& proxy = entry.second;
        if (proxy->get_id() == node_id) {
            return proxy;
        }
    }
    return {};
}

auto Rendergraph_window::get_node_position(const Graph_editor_node& node) -> ImVec2
{
    return m_node_editor->GetNodePosition(ax::NodeEditor::NodeId{node.get_id()});
}

void Rendergraph_window::set_node_position(Graph_editor_node& node, const ImVec2& position)
{
    m_node_editor->SetNodePosition(ax::NodeEditor::NodeId{node.get_id()}, position);
}

auto Rendergraph_window::get_node_size(const Graph_editor_node& node) -> ImVec2
{
    return m_node_editor->GetNodeSize(ax::NodeEditor::NodeId{node.get_id()});
}

auto Rendergraph_window::get_node_editor() -> ax::NodeEditor::EditorContext*
{
    return m_node_editor.get();
}

auto Rendergraph_window::get_layout_node_id(const erhe::graph::Node& node) const -> std::size_t
{
    // The canvas draws proxies, so the automatic layout must address a graph
    // node's proxy. A node without a proxy yet (registered after this
    // frame's sync) reports its own id; its canvas size reads zero, which
    // makes the pending layout retry the next frame.
    for (const auto& entry : m_proxy_nodes) {
        if (static_cast<const erhe::graph::Node*>(entry.first) == &node) {
            return entry.second->get_id();
        }
    }
    return node.get_id();
}

auto Rendergraph_window::clipboard_kind() const -> const char*
{
    return "rendergraph";
}

auto Rendergraph_window::get_current_graph() -> erhe::graph::Graph*
{
    return &m_context.rendergraph->get_graph();
}

auto Rendergraph_window::paste_nodes(const nlohmann::json&, const ImVec2&) -> std::vector<std::size_t>
{
    return {};
}

void Rendergraph_window::remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>&)
{
    // The rendergraph is owned by the renderer, not editable from here.
}

void Rendergraph_window::build_palette()
{
    // View-only window: nothing spawnable.
}

void Rendergraph_window::add_node_from_palette(const std::string&, const ImVec2*)
{
}

void Rendergraph_window::sync_proxy_nodes(erhe::rendergraph::Rendergraph& rendergraph)
{
    const std::vector<erhe::rendergraph::Rendergraph_node*>& nodes = rendergraph.get_nodes();

    bool changed = false;

    // Drop proxies of unregistered nodes. The item id check catches a new
    // node allocated at a destroyed node's address between frames, and the
    // pin count check catches pins registered after the proxy mirrored them
    // (e.g. the depth visualization window adds an input to the window
    // imgui host at runtime) - pins are append-only, so a count mismatch is
    // the complete drift signal; the proxy is then rebuilt below.
    for (auto i = m_proxy_nodes.begin(); i != m_proxy_nodes.end(); ) {
        const bool registered = std::find(nodes.begin(), nodes.end(), i->first) != nodes.end();
        const bool same_node  =
            registered &&
            (i->second->get_rendergraph_node_id() == i->first->get_id()) &&
            (i->second->get_input_pins ().size() == i->first->get_input_pins ().size()) &&
            (i->second->get_output_pins().size() == i->first->get_output_pins().size());
        if (!same_node) {
            i = m_proxy_nodes.erase(i);
            changed = true;
        } else {
            ++i;
        }
    }

    // Create proxies for newly registered nodes.
    for (erhe::rendergraph::Rendergraph_node* node : nodes) {
        if (!m_proxy_nodes.contains(node)) {
            m_proxy_nodes.emplace(node, std::make_shared<Rendergraph_editor_node>(*node));
            changed = true;
        }
    }

    if (!changed) {
        return;
    }

    // The node set changed (first show, viewport opened / closed): re-run
    // the shared automatic layout so the graph stays readable without
    // manual arranging.
    request_automatic_layout();

    m_pin_remap.clear();
    for (const auto& entry : m_proxy_nodes) {
        const erhe::rendergraph::Rendergraph_node*                 node  = entry.first;
        const std::shared_ptr<Rendergraph_editor_node>&            proxy = entry.second;
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& real_inputs   = node->get_input_pins();
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& proxy_inputs  = proxy->get_input_pins();
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& real_outputs  = node->get_output_pins();
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& proxy_outputs = proxy->get_output_pins();
        for (std::size_t i = 0, end = real_inputs.size(); i < end; ++i) {
            m_pin_remap.emplace(&real_inputs[i], &proxy_inputs[i]);
        }
        for (std::size_t i = 0, end = real_outputs.size(); i < end; ++i) {
            m_pin_remap.emplace(&real_outputs[i], &proxy_outputs[i]);
        }
    }
}

void Rendergraph_window::imgui()
{
    erhe::rendergraph::Rendergraph& rendergraph = *m_context.rendergraph;

    sync_proxy_nodes(rendergraph);

    m_node_editor->Begin("Rendergraph", ImVec2{0.0f, 0.0f});

    // Draw in the rendergraph's (sorted) node order for determinism.
    for (erhe::rendergraph::Rendergraph_node* node : rendergraph.get_nodes()) {
        const auto i = m_proxy_nodes.find(node);
        if (i != m_proxy_nodes.end()) {
            i->second->node_editor(m_context, *m_node_editor.get());
        }
    }

    // Links, identified by pointer like in the other graph editor windows
    // (Graph_editor_window_base::collect_selected_links relies on this).
    // Endpoints are remapped from the rendergraph's real pins to the drawn
    // proxy pins.
    for (const std::unique_ptr<erhe::graph::Link>& link : rendergraph.get_graph().get_links()) {
        const auto source = m_pin_remap.find(link->get_source());
        const auto sink   = m_pin_remap.find(link->get_sink());
        if ((source == m_pin_remap.end()) || (sink == m_pin_remap.end())) {
            continue; // endpoint of a node registered after sync ran this frame
        }
        m_node_editor->Link(
            ax::NodeEditor::LinkId{link.get()},
            ax::NodeEditor::PinId{source->second},
            ax::NodeEditor::PinId{sink->second}
        );
    }

    m_node_editor->End();

    // Interactive node resizing (edge / corner drags): adopt the dragged
    // size into the proxy's requested extent, like the other graph editors.
    apply_node_resize(*m_node_editor.get());

    // Pending automatic layout (requested by sync_proxy_nodes on node-set
    // changes); waits for stable measured node sizes, then frames the result.
    apply_automatic_layout();
}

}
