#pragma once

#include "erhe_profile/profile.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graph {
    class Graph;
}

namespace erhe::graphics {
    class Command_buffer;
    class Device;
}

namespace erhe::rendergraph {

class Rendergraph_node;
class Rendergraph_node_deleter;

class Rendergraph final
{
public:
    explicit Rendergraph(erhe::graphics::Device& graphics_device);
    ~Rendergraph() noexcept;

    // Public API
    // Main thread: the live node list (registered nodes, in execution order
    // once sorted).
    [[nodiscard]] auto get_nodes() const -> const std::vector<Rendergraph_node*>&;
    // Prepare phase (main thread, after every edit of the frame): destroys
    // the released nodes no frame still to be rendered names, sorts the
    // live list, calls each enabled node's prepare_rendergraph_node() hook,
    // then copies the live list (in execution order) into the slot of the
    // frame being prepared (the role-keyed Device::get_frame_index(); one
    // slot per frame in flight) and every node's live enabled flag and
    // connections into that node's record of the frame. execute() of that
    // frame runs exactly the nodes of the slot that are enabled in their
    // record, so a register, release, connect, enable or disable from the
    // ImGui phase or a later prepare phase never reaches a frame the render
    // phase is already rendering.
    void prepare_frame  ();
    // Render phase: executes the nodes of the slot of the frame being
    // rendered (prepare_frame() of the same frame filled it; verified) that
    // are enabled in it. Never takes m_mutex: it reads only the slot and
    // the nodes' records of its frame.
    void execute        (erhe::graphics::Command_buffer& command_buffer);
    // Shutdown, after the last render phase returned (the render thread is
    // stopped): no frame renders any more. Destroys every retired node now;
    // a node released later is destroyed at once.
    void end_rendering  ();
    void register_node  (Rendergraph_node* node);
    auto connect        (int key, Rendergraph_node* source_node, Rendergraph_node* sink_node) -> bool;
    auto disconnect     (int key, Rendergraph_node* source_node, Rendergraph_node* sink_node) -> bool;

    [[nodiscard]] auto get_graphics_device() -> erhe::graphics::Device&;

    // The internal erhe::graph holding the node / link topology. Used by the
    // editor's rendergraph viewer (Graph_editor_window_base machinery).
    [[nodiscard]] auto get_graph() -> erhe::graph::Graph&;

    // Defer resource destruction until after execute() completes.
    // Use this when a node replaces resources during execution that
    // may still be referenced by nodes executing later in the same frame.
    // Render phase only (from a node's execute): execute() releases the
    // deferred resources when it returns, so a resource deferred from any
    // other thread or phase would race with that release.
    void defer_resource(std::shared_ptr<void> resource);

private:
    friend class Rendergraph_node_deleter;

    // The nodes one frame renders, in execution order, as its prepare phase
    // copied them.
    class Frame_slot
    {
    public:
        uint64_t                       frame_index{std::numeric_limits<uint64_t>::max()};
        std::vector<Rendergraph_node*> nodes;
    };
    // A node its last owner released while a frame still to be rendered
    // names it; destroyed once the render phase finished that frame.
    class Retired_node
    {
    public:
        Rendergraph_node* node      {nullptr};
        uint64_t          last_frame{0}; // the latest frame whose slot names the node
    };

    // Rendergraph_node_deleter, the last owner of node released it (any
    // thread): unregisters it at once, then destroys it, or retires it when
    // a frame not yet rendered names it.
    void release_node                 (Rendergraph_node* node);
    // Under m_mutex.
    void sort                         ();
    void unregister_node_locked       (Rendergraph_node* node);
    // Main thread, prepare phase (prepare_frame()), outside m_mutex.
    void destroy_released_retired_nodes();
    // Under m_rendered_mutex: frame was rendered (the render phase finished
    // it, or no frame renders any more).
    [[nodiscard]] auto is_rendered_locked(uint64_t frame_index) const -> bool;

    erhe::graphics::Device&        m_graphics_device;
    // Guards the live node list, the graph, the frame slots (written by
    // prepare_frame()) and the retired list. Never taken by execute().
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Rendergraph_node*> m_nodes;
    bool                           m_is_sorted{false};

    // Internal erhe::graph for topological sort
    std::unique_ptr<erhe::graph::Graph> m_graph;

    // One per device frame in flight, indexed by frame index modulo their
    // count; sized once, node vectors cleared and refilled.
    std::vector<Frame_slot>        m_frame_slots;
    std::vector<Retired_node>      m_retired_nodes;
    std::vector<Rendergraph_node*> m_destroy_scratch; // destroy_released_retired_nodes()

    // The latest frame whose execute() returned; written by the render
    // phase, read by release_node() and prepare_frame().
    ERHE_PROFILE_MUTEX(std::mutex, m_rendered_mutex);
    bool                           m_has_rendered_frame{false};
    uint64_t                       m_rendered_frame    {0};
    bool                           m_rendering_ended   {false}; // end_rendering()

    // Resources deferred for destruction until after execute() completes
    std::vector<std::shared_ptr<void>> m_deferred_resources;
};

} // namespace erhe::rendergraph
