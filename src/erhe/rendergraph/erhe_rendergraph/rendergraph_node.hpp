#pragma once

#include "erhe_rendergraph/resource_routing.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace igl {
    class IFramebuffer;
    class ITexture;
}

namespace erhe::rendergraph {

class Rendergraph;
class Rendergraph_node;

class Rendergraph_producer_connector
{
public:
    Resource_routing               resource_routing{Resource_routing::None};
    std::string                    label           {};
    int                            key             {0};
    std::vector<Rendergraph_node*> consumer_nodes  {};
};

class Rendergraph_consumer_connector
{
public:
    Resource_routing               resource_routing{Resource_routing::None};
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
class Rendergraph_node
{
public:
    Rendergraph_node(
        Rendergraph&           rendergraph,
        const std::string_view name
    );
    virtual ~Rendergraph_node() noexcept;

    [[nodiscard]] virtual auto get_type_name() const -> std::string_view { return "Rendergraph_node"; }
    [[nodiscard]] auto get_name   () const -> const std::string&;
    [[nodiscard]] auto get_inputs () const -> const std::vector<Rendergraph_consumer_connector>&;
    [[nodiscard]] auto get_inputs ()       ->       std::vector<Rendergraph_consumer_connector>&;
    [[nodiscard]] auto get_outputs() const -> const std::vector<Rendergraph_producer_connector>&;
    [[nodiscard]] auto get_outputs()       ->       std::vector<Rendergraph_producer_connector>&;
    [[nodiscard]] auto get_size   () const -> std::optional<glm::vec2>;
    [[nodiscard]] auto is_enabled () const -> bool;

    void set_enabled      (bool value);
    auto register_input   (Resource_routing resource_routing, const std::string_view label, int key) -> bool;
    auto register_output  (Resource_routing resource_routing, const std::string_view label, int key) -> bool;
    auto connect_input    (int key, Rendergraph_node* producer_node) -> bool;
    auto connect_output   (int key, Rendergraph_node* consumer_node) -> bool;
    auto disconnect_input (int key, Rendergraph_node* producer_node) -> bool;
    auto disconnect_output(int key, Rendergraph_node* consumer_node) -> bool;

    virtual void execute_rendergraph_node() = 0;

    [[nodiscard]] auto get_input(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> const Rendergraph_consumer_connector*;

    [[nodiscard]] virtual auto get_consumer_input_node(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> Rendergraph_node*;

    [[nodiscard]] virtual auto get_consumer_input_texture(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::ITexture>;

    [[nodiscard]] virtual auto get_consumer_input_framebuffer(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::IFramebuffer>;

    [[nodiscard]] virtual auto get_consumer_input_viewport(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> erhe::math::Viewport;

    [[nodiscard]] auto get_output(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> const Rendergraph_producer_connector*;

    [[nodiscard]] virtual auto get_producer_output_node(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> Rendergraph_node*;

    [[nodiscard]] virtual auto get_producer_output_texture(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::ITexture>;

    [[nodiscard]] virtual auto get_producer_output_framebuffer(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> std::shared_ptr<igl::IFramebuffer>;

    [[nodiscard]] virtual auto get_producer_output_viewport(
        Resource_routing resource_routing,
        int              key,
        int              depth = 0
    ) const -> erhe::math::Viewport;

    [[nodiscard]] auto get_depth   () const -> int;
    [[nodiscard]] auto get_position() const -> glm::vec2;
    [[nodiscard]] auto get_selected() const -> bool;

    void set_depth   (int depth);
    void set_position(glm::vec2 position);
    void set_selected(bool selected);

protected:
    virtual auto inputs_allowed () const -> bool;
    virtual auto outputs_allowed() const -> bool;

    Rendergraph&                                m_rendergraph;

    std::mutex                                  m_mutex;
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
