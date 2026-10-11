#pragma once

#include "erhe_graphics/texture_reference.hpp"
#include "erhe_rendergraph/resource_routing.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/debug_label.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Texture;
}

namespace erhe::rendergraph {

class Rendergraph;
class Rendergraph_node;

constexpr int rendergraph_max_depth = 10;

// The deleter of every Rendergraph_node (make_rendergraph_node()). Releasing
// the last owner unregisters the node from its Rendergraph at once, and
// destroys it once no frame still to be rendered names it (Rendergraph
// "Node lifetime" in doc/erhe/rendergraph.md).
class Rendergraph_node_deleter
{
public:
    void operator()(Rendergraph_node* node) const;

    // make_rendergraph_node(): marks the node as owned through this deleter.
    static void adopt(Rendergraph_node& node);
};

// Node for rendergraph
//
// Inherits from erhe::graph::Node to participate directly in
// erhe::graph topological sort and pin/link connectivity.
class Rendergraph_node
    : public erhe::graph::Node
    , public erhe::graphics::Texture_reference
{
public:
    Rendergraph_node(Rendergraph& rendergraph, erhe::utility::Debug_label name);
    ~Rendergraph_node() noexcept override;

    // Overrides Item virtuals from erhe::graph::Node
    static constexpr std::string_view static_type_name{"Rendergraph_node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::rendergraph_node; }
    auto get_type     () const -> uint64_t                        override { return get_static_type(); }
    auto get_type_name() const -> std::string_view                override { return static_type_name; }
    auto clone        () const -> std::shared_ptr<erhe::Item_base> override { return {}; }

    // Implements Texture_reference
    auto get_referenced_texture() const -> const erhe::graphics::Texture* override;

    [[nodiscard]] auto get_rendergraph () -> Rendergraph&;
    [[nodiscard]] auto get_rendergraph () const -> const Rendergraph&;
    [[nodiscard]] auto get_debug_label () const -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_size        () const -> std::optional<glm::vec2>;
    // Main thread: the live enabled flag (set_enabled()). A change reaches
    // the render phase at the next Rendergraph::prepare_frame(), which copies
    // the flag (with the node's connections) into the frame being prepared.
    [[nodiscard]] auto is_enabled      () const -> bool;
    // Render phase: the node takes part in frame frame_index - it was
    // enabled when the prepare phase of that frame copied the flags
    // (Rendergraph::prepare_frame()). A node registered after that copy
    // takes part from the next frame on. Rendergraph::execute() runs only
    // the nodes of the frame it renders that are enabled in it.
    [[nodiscard]] auto is_enabled_in_frame(uint64_t frame_index) const -> bool;

    [[nodiscard]] auto get_depth   () const -> int;

    void set_depth        (int depth);
    void set_enabled      (bool value);
    auto register_input   (erhe::utility::Debug_label label, int key) -> bool;
    auto register_output  (erhe::utility::Debug_label label, int key) -> bool;

    virtual void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) = 0;

    // A node returning true declares that execute_rendergraph_node()
    // ends and submits the frame command buffer (and continues in fresh
    // cbs), e.g. the XR headset node's mid-frame fan-out. Rendergraph::
    // execute() then labels such a node (and the whole graph) with
    // queue-level debug groups instead of cb-level ones: a label region
    // spanning a cb end + submit cannot live in any single cb, and
    // Command_buffer end() verifies no cb-level group is open.
    [[nodiscard]] virtual auto submits_command_buffer() const -> bool;

    // The node connected to this node's input / output pin of the key. In
    // the render phase (Frame_phase::r_render) the connection as the prepare
    // phase of the frame being rendered copied it (Rendergraph::
    // prepare_frame()); nullptr for a node that frame does not name. In
    // every other phase the live connection.
    [[nodiscard]] virtual auto get_consumer_input_node    (int key, int depth = 0) const -> Rendergraph_node*;
    [[nodiscard]] virtual auto get_producer_output_node   (int key, int depth = 0) const -> Rendergraph_node*;

    [[nodiscard]] virtual auto get_consumer_input_texture (int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] virtual auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture>;

protected:
    virtual auto inputs_allowed () const -> bool;
    virtual auto outputs_allowed() const -> bool;

    // Prepare phase hook (main thread, Rendergraph::prepare_frame()): called
    // for every node enabled at the call, before the enabled flags are
    // copied. A node copies the live parameters its execute of frame
    // frame_index reads (edited by the ImGui windows, tools, MCP) into its
    // slot of that frame, so a later prepare phase or the ImGui phase never
    // writes what the render phase reads. A hook may change live enabled
    // flags, its own or another node's: the copy follows every hook.
    // Default: nothing.
    virtual void prepare_rendergraph_node(uint64_t frame_index);

    // Release hook (Rendergraph_node_deleter, on the releasing thread): the
    // last owner released the node, which the rendergraph unregisters next
    // and then destroys, or retires while a frame still to be rendered
    // names it. A node registered with a registry outside the rendergraph
    // (one that a prepare phase iterates) unregisters from it here, so no
    // later prepare phase reaches a released node while it is retired.
    // Called once, before the destructor. Default: nothing.
    virtual void on_released();

    friend class Rendergraph;
    friend class Rendergraph_node_deleter;

    Rendergraph&                                m_rendergraph;
    ERHE_PROFILE_MUTEX(std::mutex,              m_mutex);
    erhe::utility::Debug_label                  m_debug_label;
    bool                                        m_is_registered{false};
    bool                                        m_has_deleter  {false}; // created by make_rendergraph_node()
    bool                                        m_enabled      {true};  // live flag, main thread
    int                                         m_depth        {0};

private:
    // The first link of one pin as a frame's prepare phase saw it.
    class Frame_link
    {
    public:
        std::size_t       key {0};
        Rendergraph_node* node{nullptr}; // producer (input pin) / consumer (output pin); nullptr: not connected
    };
    class Frame_record
    {
    public:
        uint64_t                                             frame_index {std::numeric_limits<uint64_t>::max()}; // the frame whose prepare phase made the record
        bool                                                 enabled     {false};
        std::size_t                                          input_count {0};
        std::size_t                                          output_count{0};
        std::array<Frame_link, erhe::graph::max_pin_count>   inputs      {};
        std::array<Frame_link, erhe::graph::max_pin_count>   outputs     {};
    };

    // Rendergraph::prepare_frame(): copies the live enabled flag and the
    // first link of every pin into the record of frame_index.
    void record_frame(uint64_t frame_index);
    // The record of the frame the calling render phase renders (role-keyed
    // Device::get_frame_index()); nullptr when that frame does not name the
    // node (registered after its prepare phase).
    [[nodiscard]] auto get_render_frame_record() const -> const Frame_record*;

    // One per device frame in flight, indexed by frame index modulo their
    // count; written by Rendergraph::prepare_frame() (main thread, M2), read
    // by the render phase of that frame.
    std::vector<Frame_record>                   m_frame_records;
};

// Creates a rendergraph node owned by a std::shared_ptr whose deleter is
// Rendergraph_node_deleter. Every Rendergraph_node is created this way
// (Rendergraph::prepare_frame() verifies it): a frame the render phase is
// still rendering may name a node its owner has already released, so the
// rendergraph, not the owner, decides when the node is destroyed.
template <typename T, typename... Args>
[[nodiscard]] auto make_rendergraph_node(Args&&... args) -> std::shared_ptr<T>
{
    static_assert(std::is_base_of_v<Rendergraph_node, T>);
    std::shared_ptr<T> node{new T(std::forward<Args>(args)...), Rendergraph_node_deleter{}};
    Rendergraph_node_deleter::adopt(*node);
    return node;
}

} // namespace erhe::rendergraph
