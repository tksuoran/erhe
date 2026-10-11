// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_log/log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>

namespace erhe::rendergraph {

Rendergraph::Rendergraph(erhe::graphics::Device& graphics_device)
    : m_graphics_device{graphics_device}
    , m_graph          {std::make_unique<erhe::graph::Graph>()}
{
    m_nodes.reserve(128);
    const std::size_t frames_in_flight = m_graphics_device.get_number_of_frames_in_flight();
    ERHE_VERIFY(frames_in_flight > 0);
    m_frame_slots.resize(frames_in_flight);
    for (Frame_slot& slot : m_frame_slots) {
        slot.nodes.reserve(128);
    }
    m_retired_nodes.reserve(16);
    m_destroy_scratch.reserve(16);
}

Rendergraph::~Rendergraph() noexcept
{
    // Mark all nodes as unregistered so their deleters (which run later, when
    // their owners release them) destroy them at once without touching this
    // destroyed Rendergraph.
    for (Rendergraph_node* node : m_nodes) {
        node->m_is_registered = false;
    }
    m_nodes.clear();
    for (Frame_slot& slot : m_frame_slots) {
        slot.nodes.clear();
    }
    // Retired nodes are owned here (their owners released them).
    std::vector<Retired_node> retired_nodes;
    retired_nodes.swap(m_retired_nodes);
    for (const Retired_node& retired : retired_nodes) {
        delete retired.node;
    }
}

auto Rendergraph::get_nodes() const -> const std::vector<Rendergraph_node*>&
{
    return m_nodes;
}

void Rendergraph::sort()
{
    if (m_is_sorted) {
        return;
    }

    m_graph->sort();

    if (!m_graph->m_is_sorted) {
        // Sort failed (cycle detected). Log detailed diagnostics.
        log_frame->error("Rendergraph sort failed - graph is not acyclic:");
        for (Rendergraph_node* node : m_nodes) {
            log_frame->info("    Node: {}", node->get_name());
            for (const erhe::graph::Pin& pin : node->get_input_pins()) {
                log_frame->info("        Input key: {}", pin.get_key());
                for (erhe::graph::Link* link : pin.get_links()) {
                    erhe::graph::Pin* source_pin = link->get_source();
                    if (source_pin != nullptr) {
                        Rendergraph_node* producer = static_cast<Rendergraph_node*>(source_pin->get_owner_node());
                        if (producer != nullptr) {
                            log_frame->info("          producer: {}", producer->get_name());
                        }
                    }
                }
            }
        }
        return;
    }

    // Map sorted graph nodes back to Rendergraph_node pointers
    m_nodes.clear();
    for (erhe::graph::Node* graph_node : m_graph->get_nodes()) {
        m_nodes.push_back(static_cast<Rendergraph_node*>(graph_node));
    }

    m_is_sorted = true;
}

void Rendergraph::prepare_frame()
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    // Outside m_mutex: a destroyed node's destructor may release nodes it owns.
    destroy_released_retired_nodes();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    sort();

    const uint64_t frame_index = m_graphics_device.get_frame_index();
    for (Rendergraph_node* node : m_nodes) {
        if (node->m_enabled) {
            node->prepare_rendergraph_node(frame_index);
        }
    }
    // After every hook: a hook may disable nodes (a one-frame capture chain).
    Frame_slot& slot = m_frame_slots[frame_index % m_frame_slots.size()];
    slot.frame_index = frame_index;
    slot.nodes.clear();
    for (Rendergraph_node* node : m_nodes) {
        // A node outside make_rendergraph_node() would be destroyed by its
        // owner while this frame may still name it.
        ERHE_VERIFY(node->m_has_deleter);
        node->record_frame(frame_index);
        slot.nodes.push_back(node);
    }
}

void Rendergraph::execute(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    // The nodes the prepare phase of this frame copied (prepare_frame()).
    // No m_mutex: the main thread prepares the next frame into another slot
    // meanwhile, and a node released since stays alive until this frame is
    // rendered (release_node()).
    const uint64_t    frame_index = m_graphics_device.get_frame_index();
    const Frame_slot& slot        = m_frame_slots[frame_index % m_frame_slots.size()];
    ERHE_VERIFY(slot.frame_index == frame_index);

    SPDLOG_LOGGER_TRACE(log_frame, "Execute render graph with {} nodes:", slot.nodes.size());

    // Debug-label scope selection: cb-targeted scopes nest each node's
    // draws under the node's name in RenderDoc, but a cb label region
    // must be contained in the cb's recording lifetime. A node that
    // declares submits_command_buffer() ends + submits the frame cb
    // mid-execution (XR headset fan-out), so its region -- and the
    // whole-graph region enclosing it -- span cbs and must be
    // queue-level labels instead. On the desktop path (no submitting
    // node) everything stays cb-level; queue-level would be useless
    // there anyway because the frame's submit happens only after
    // execute() returns, outside the region.
    bool any_enabled_node_submits = false;
    for (const Rendergraph_node* node : slot.nodes) {
        if (node->is_enabled_in_frame(frame_index) && node->submits_command_buffer()) {
            any_enabled_node_submits = true;
            break;
        }
    }

    {
        std::optional<erhe::graphics::Scoped_debug_group>       render_graph_cb_scope{};
        std::optional<erhe::graphics::Scoped_queue_debug_group> render_graph_queue_scope{};
        const erhe::utility::Debug_label execute_label{"Rendergraph::execute()"};
        if (any_enabled_node_submits) {
            render_graph_queue_scope.emplace(m_graphics_device, execute_label);
        } else {
            render_graph_cb_scope.emplace(command_buffer, execute_label);
        }

        for (Rendergraph_node* node : slot.nodes) {
            if (node->is_enabled_in_frame(frame_index)) {
                SPDLOG_LOGGER_TRACE(log_frame, "Execute render graph node '{}'", node->get_name());
                // Breadcrumb so the main-loop watchdog can name the node a spinning
                // tick is stuck in. get_name() returns a stored string, so passing
                // it as string_view does not allocate.
                erhe::log::set_breadcrumb(node->get_name());
                std::optional<erhe::graphics::Scoped_debug_group>       node_cb_scope{};
                std::optional<erhe::graphics::Scoped_queue_debug_group> node_queue_scope{};
                if (node->submits_command_buffer()) {
                    node_queue_scope.emplace(m_graphics_device, node->get_debug_label());
                } else {
                    node_cb_scope.emplace(command_buffer, node->get_debug_label());
                }
                node->execute_rendergraph_node(command_buffer);
            }
        }

        // Release deferred resources now that all nodes have completed execution.
        // Nodes may defer resource destruction during execute when old resources
        // are still referenced by nodes that execute later in the same frame.
        m_deferred_resources.clear();
    }

    // The frame's nodes are no longer used: a node released meanwhile may be
    // destroyed (by the next prepare_frame()).
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> rendered_lock{m_rendered_mutex};
    m_has_rendered_frame = true;
    m_rendered_frame     = frame_index;
}

void Rendergraph::end_rendering()
{
    erhe::utility::Thread_roles::assert_main_thread();
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> rendered_lock{m_rendered_mutex};
        m_rendering_ended = true;
    }
    destroy_released_retired_nodes();
}

auto Rendergraph::is_rendered_locked(const uint64_t frame_index) const -> bool
{
    return m_rendering_ended || (m_has_rendered_frame && (frame_index <= m_rendered_frame));
}

void Rendergraph::release_node(Rendergraph_node* node)
{
    bool retired = false;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        unregister_node_locked(node);

        // The latest frame whose slot names the node.
        bool     named      = false;
        uint64_t last_frame = 0;
        for (const Frame_slot& slot : m_frame_slots) {
            if (slot.frame_index == std::numeric_limits<uint64_t>::max()) {
                continue;
            }
            for (const Rendergraph_node* entry : slot.nodes) {
                if (entry == node) {
                    if (!named || (slot.frame_index > last_frame)) {
                        last_frame = slot.frame_index;
                    }
                    named = true;
                    break;
                }
            }
        }
        if (named) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> rendered_lock{m_rendered_mutex};
            if (!is_rendered_locked(last_frame)) {
                m_retired_nodes.push_back(Retired_node{.node = node, .last_frame = last_frame});
                retired = true;
            }
        }
    }
    if (retired) {
        log_tail->trace("Retired Rendergraph_node {}", node->get_name());
        return;
    }
    delete node;
}

void Rendergraph::destroy_released_retired_nodes()
{
    ERHE_VERIFY(m_destroy_scratch.empty());
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        if (m_retired_nodes.empty()) {
            return;
        }
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> rendered_lock{m_rendered_mutex};
        std::size_t kept_count = 0;
        for (const Retired_node& retired : m_retired_nodes) {
            if (is_rendered_locked(retired.last_frame)) {
                m_destroy_scratch.push_back(retired.node);
            } else {
                m_retired_nodes[kept_count++] = retired;
            }
        }
        m_retired_nodes.resize(kept_count);
    }
    for (Rendergraph_node* node : m_destroy_scratch) {
        log_tail->trace("Destroying retired Rendergraph_node {}", node->get_name());
        delete node;
    }
    m_destroy_scratch.clear();
}

void Rendergraph::defer_resource(std::shared_ptr<void> resource)
{
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);
    m_deferred_resources.push_back(std::move(resource));
}

void Rendergraph::register_node(Rendergraph_node* node)
{
    erhe::utility::Thread_roles::assert_main_thread();
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [node](Rendergraph_node* entry) {
            return entry == node;
        }
    );
    if (i != m_nodes.end()) {
        log_tail->error("Rendergraph_node '{}' is already registered to Rendergraph", node->get_name());
        return;
    }
#endif
    m_nodes.push_back(node);
    m_is_sorted = false;

    // Register directly with erhe::graph for topological sort
    m_graph->register_node(node);
    node->m_is_registered = true;

    log_tail->trace("Registered Rendergraph_node {}", node->get_name());
}

void Rendergraph::unregister_node_locked(Rendergraph_node* node)
{
    const auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [node](Rendergraph_node* entry) {
            return entry == node;
        }
    );
    ERHE_VERIFY(i != m_nodes.end());

    // Unregister from erhe::graph (this also disconnects all its links)
    m_graph->unregister_node(node);
    node->m_is_registered = false;

    m_nodes.erase(i);
    m_is_sorted = false;

    log_tail->trace("Unregistered Rendergraph_node {}", node->get_name());
}

auto Rendergraph::get_graphics_device() -> erhe::graphics::Device&
{
    return m_graphics_device;
}

auto Rendergraph::get_graph() -> erhe::graph::Graph&
{
    return *m_graph.get();
}

auto Rendergraph::connect(const int key, Rendergraph_node* source, Rendergraph_node* sink) -> bool
{
    ERHE_VERIFY(source != nullptr);
    ERHE_VERIFY(sink != nullptr);

    // The live graph only: a frame already prepared keeps the connections
    // its nodes' records copied.
    erhe::utility::Thread_roles::assert_main_thread();
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    // Find output pin on source with matching key
    erhe::graph::Pin* source_pin = nullptr;
    for (erhe::graph::Pin& pin : source->get_output_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            source_pin = &pin;
            break;
        }
    }
    if (source_pin == nullptr) {
        log_tail->error("Rendergraph::connect(): source '{}' has no output pin for key {}", source->get_name(), key);
        return false;
    }

    // Find input pin on sink with matching key
    erhe::graph::Pin* sink_pin = nullptr;
    for (erhe::graph::Pin& pin : sink->get_input_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            sink_pin = &pin;
            break;
        }
    }
    if (sink_pin == nullptr) {
        log_tail->error("Rendergraph::connect(): sink '{}' has no input pin for key {}", sink->get_name(), key);
        return false;
    }

    erhe::graph::Link* link = m_graph->connect(source_pin, sink_pin);
    if (link == nullptr) {
        return false;
    }

    sink->set_depth(source->get_depth() + 1);

    m_is_sorted = false;
    log_tail->trace("Rendergraph: Connected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());
    return true;
}

auto Rendergraph::disconnect(const int key, Rendergraph_node* source, Rendergraph_node* sink) -> bool
{
    ERHE_VERIFY(source != nullptr);
    ERHE_VERIFY(sink != nullptr);

    erhe::utility::Thread_roles::assert_main_thread();
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    erhe::graph::Link* link_to_remove = nullptr;
    for (erhe::graph::Pin& pin : source->get_output_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            for (erhe::graph::Link* link : pin.get_links()) {
                if (link->get_sink() != nullptr && link->get_sink()->get_owner_node() == sink) {
                    link_to_remove = link;
                    break;
                }
            }
            if (link_to_remove != nullptr) {
                break;
            }
        }
    }
    if (link_to_remove != nullptr) {
        m_graph->disconnect(link_to_remove);
    }

    m_is_sorted = false;
    log_tail->trace("Rendergraph: disconnected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());

    return true;
}

} // namespace erhe::rendergraph
