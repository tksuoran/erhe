#pragma once

#include "erhe_rendergraph/resource_routing.hpp"
#include "erhe_item/unique_id.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics {
    class Framebuffer;
    class Texture;
}

namespace erhe::rendergraph {

class Rendergraph;
class Rendergraph_node;

class Rendergraph_id
{
public:
    Unique_id<Rendergraph_id> id{};
};

class Rendergraph_producer_connector : public Rendergraph_id
{
public:
    Rendergraph_producer_connector(
        Routing     resource_routing,
        std::string label,
        int         key
    )
        : resource_routing{resource_routing}
        , label           {label}
        , key             {key}
    {
    }

    Routing                        resource_routing{Routing::None};
    std::string                    label           {};
    int                            key             {0};
    std::vector<Rendergraph_node*> consumer_nodes  {};
};

class Rendergraph_consumer_connector : public Rendergraph_id
{
public:
    Rendergraph_consumer_connector(
        Routing     resource_routing,
        std::string label,
        int         key
    )
        : resource_routing{resource_routing}
        , label           {label}
        , key             {key}
    {
    }

    Routing                        resource_routing{Routing::None};
    std::string                    label           {};
    int                            key             {0};
    std::vector<Rendergraph_node*> producer_nodes  {};
};

constexpr int rendergraph_max_depth = 10;

/// <summary>
/// Node for rendergraph
/// </summary>
/// Rendergraph nodes have inputs and outputs (often both, but at least either input(s) or output(s).
/// Rendergraph nodes have named input and output slots.
/// Rendergraph nodes must have their inputs and outputs connected to other rendergraph nodes.
class Rendergraph_node : public Rendergraph_id
{
public:
    Rendergraph_node(Rendergraph& rendergraph, const std::string_view name);
    virtual ~Rendergraph_node() noexcept;

    [[nodiscard]] virtual auto get_type_name() const -> std::string_view { return "Rendergraph_node"; }

    [[nodiscard]] auto get_name   () const -> const std::string&;
    [[nodiscard]] auto get_inputs () const -> const std::vector<Rendergraph_consumer_connector>&;
    [[nodiscard]] auto get_inputs ()       ->       std::vector<Rendergraph_consumer_connector>&;
    [[nodiscard]] auto get_outputs() const -> const std::vector<Rendergraph_producer_connector>&;
    [[nodiscard]] auto get_outputs()       ->       std::vector<Rendergraph_producer_connector>&;
    [[nodiscard]] auto get_size   () const -> std::optional<glm::vec2>;
    [[nodiscard]] auto is_enabled () const -> bool;

    [[nodiscard]] auto get_input   (Routing resource_routing, int key, int depth = 0) const -> const Rendergraph_consumer_connector*;
    [[nodiscard]] auto get_output  (Routing resource_routing, int key, int depth = 0) const -> const Rendergraph_producer_connector*;
    [[nodiscard]] auto get_depth   () const -> int;
    [[nodiscard]] auto get_position() const -> glm::vec2;
    [[nodiscard]] auto get_selected() const -> bool;

    void set_depth        (int depth);
    void set_position     (glm::vec2 position);
    void set_selected     (bool selected);
    void set_enabled      (bool value);
    auto register_input   (Routing resource_routing, const std::string_view label, int key) -> bool;
    auto register_output  (Routing resource_routing, const std::string_view label, int key) -> bool;
    auto connect_input    (int key, Rendergraph_node* producer_node) -> bool;
    auto connect_output   (int key, Rendergraph_node* consumer_node) -> bool;
    auto disconnect_input (int key, Rendergraph_node* producer_node) -> bool;
    auto disconnect_output(int key, Rendergraph_node* consumer_node) -> bool;
    auto get_id           () const -> std::size_t;

    virtual void execute_rendergraph_node() = 0;

    [[nodiscard]] virtual auto get_consumer_input_node        (Routing resource_routing, int key, int depth = 0) const -> Rendergraph_node*;
    [[nodiscard]] virtual auto get_consumer_input_texture     (Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] virtual auto get_consumer_input_framebuffer (Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Framebuffer>;
    [[nodiscard]] virtual auto get_consumer_input_viewport    (Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport;
    [[nodiscard]] virtual auto get_producer_output_node       (Routing resource_routing, int key, int depth = 0) const -> Rendergraph_node*;
    [[nodiscard]] virtual auto get_producer_output_texture    (Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] virtual auto get_producer_output_framebuffer(Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Framebuffer>;
    [[nodiscard]] virtual auto get_producer_output_viewport   (Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport;

protected:
    virtual auto inputs_allowed () const -> bool;
    virtual auto outputs_allowed() const -> bool;

    Rendergraph&                                m_rendergraph;
    ERHE_PROFILE_MUTEX(std::mutex,              m_mutex);
    std::string                                 m_name;
    bool                                        m_enabled {true};
    std::vector<Rendergraph_consumer_connector> m_inputs;
    std::vector<Rendergraph_producer_connector> m_outputs;
    int                                         m_depth   {0};

    // For GUI
    glm::vec2                                   m_position{};
    bool                                        m_selected{false};
};

} // namespace erhe::rendergraph
