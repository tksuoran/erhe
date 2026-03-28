#pragma once

#include "erhe_graphics/texture.hpp"
#include "erhe_rendergraph/resource_routing.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/debug_label.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics {
    class Render_pass;
    class Texture;
}

namespace erhe::rendergraph {

class Rendergraph;
class Rendergraph_node;

constexpr int rendergraph_max_depth = 10;

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
    [[nodiscard]] auto is_enabled      () const -> bool;

    [[nodiscard]] auto get_depth   () const -> int;
    [[nodiscard]] auto get_position() const -> glm::vec2;
    [[nodiscard]] auto get_selected() const -> bool;

    void set_depth        (int depth);
    void set_position     (glm::vec2 position);
    void set_selected     (bool selected);
    void set_enabled      (bool value);
    auto register_input   (erhe::utility::Debug_label label, int key) -> bool;
    auto register_output  (erhe::utility::Debug_label label, int key) -> bool;

    virtual void execute_rendergraph_node() = 0;

    [[nodiscard]] virtual auto get_consumer_input_node    (int key, int depth = 0) const -> Rendergraph_node*;
    [[nodiscard]] virtual auto get_producer_output_node   (int key, int depth = 0) const -> Rendergraph_node*;

    [[nodiscard]] virtual auto get_consumer_input_texture (int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] virtual auto get_producer_output_texture(int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture>;

protected:
    virtual auto inputs_allowed () const -> bool;
    virtual auto outputs_allowed() const -> bool;

    friend class Rendergraph;

    Rendergraph&                                m_rendergraph;
    ERHE_PROFILE_MUTEX(std::mutex,              m_mutex);
    erhe::utility::Debug_label                  m_debug_label;
    bool                                        m_is_registered{false};
    bool                                        m_enabled {true};
    int                                         m_depth   {0};

    // For GUI
    glm::vec2                                   m_position{};
    bool                                        m_selected{false};
};

} // namespace erhe::rendergraph
