// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::rendergraph {

Rendergraph_node::Rendergraph_node(Rendergraph& rendergraph, const erhe::utility::Debug_label debug_label)
    : erhe::graph::Node{debug_label.string_view()}
    , m_rendergraph    {rendergraph}
    , m_debug_label    {debug_label}
{
    m_rendergraph.register_node(this);
}

Rendergraph_node::~Rendergraph_node() noexcept
{
    log_tail->trace("~Rendergraph_node() {}", get_debug_label().string_view());
    // m_is_registered is set to false by Rendergraph::~Rendergraph() when the
    // rendergraph is destroyed before this node. In that case, skip unregister
    // since the rendergraph no longer exists.
    if (m_is_registered) {
        m_rendergraph.unregister_node(this);
    }
}

auto Rendergraph_node::get_referenced_texture() const -> const erhe::graphics::Texture*
{
    return get_producer_output_texture(erhe::rendergraph::Rendergraph_node_key::wildcard, 0).get();
}

auto Rendergraph_node::get_consumer_input_node(const int key, const int depth) const -> Rendergraph_node*
{
    if (!inputs_allowed()) {
        log_tail->error("Node '{}' inputs are not allowed ('{}')", get_debug_label().string_view(), key);
        return nullptr;
    }

    ERHE_VERIFY(depth < rendergraph_max_depth);

    // Find input pin with matching key
    for (const erhe::graph::Pin& pin : get_input_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            // Follow link to source pin
            const std::vector<erhe::graph::Link*>& links = pin.get_links();
            if (links.empty()) {
                return nullptr;
            }
            erhe::graph::Pin* source_pin = links.front()->get_source();
            if (source_pin == nullptr) {
                return nullptr;
            }
            return static_cast<Rendergraph_node*>(source_pin->get_owner_node());
        }
    }

    log_tail->error("Node '{}' input for key '{}' is not registered", get_debug_label().string_view(), key);
    return nullptr;
}

auto Rendergraph_node::get_consumer_input_texture(const int key, const int depth) const -> std::shared_ptr<erhe::graphics::Texture>
{
    Rendergraph_node* producer = get_consumer_input_node(key, depth);
    return (producer != nullptr)
        ? producer->get_producer_output_texture(key, depth + 1)
        : std::shared_ptr<erhe::graphics::Texture>{};
}

auto Rendergraph_node::get_producer_output_node(const int key, const int depth) const -> Rendergraph_node*
{
    if (!outputs_allowed()) {
        log_tail->error("Node '{}' outputs are not allowed ('{}')", get_debug_label().string_view(), key);
        return nullptr;
    }

    ERHE_VERIFY(depth < rendergraph_max_depth);

    // Find output pin with matching key
    for (const erhe::graph::Pin& pin : get_output_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            const std::vector<erhe::graph::Link*>& links = pin.get_links();
            if (links.empty()) {
                return nullptr;
            }
            erhe::graph::Pin* sink_pin = links.front()->get_sink();
            if (sink_pin == nullptr) {
                return nullptr;
            }
            return static_cast<Rendergraph_node*>(sink_pin->get_owner_node());
        }
    }

    log_tail->error("Node '{}' output for key '{}' is not registered", get_debug_label().string_view(), key);
    return nullptr;
}

auto Rendergraph_node::get_producer_output_texture(int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return {};
}

auto Rendergraph_node::get_size() const -> std::optional<glm::vec2>
{
    std::optional<glm::vec2> size;

    for (const erhe::graph::Pin& pin : get_output_pins()) {
        const int key = static_cast<int>(pin.get_key());
        const std::shared_ptr<erhe::graphics::Texture>& texture = get_producer_output_texture(key);
        if (
            texture &&
            (texture->get_texture_type() == erhe::graphics::Texture_type::texture_2d) &&
            (texture->get_width       () >= 1) &&
            (texture->get_height      () >= 1) &&
            (erhe::dataformat::has_color(texture->get_pixelformat()))
        ) {
            if (!size.has_value()) {
                size = glm::vec2{texture->get_width(), texture->get_height()};
            } else {
                size = glm::max(size.value(), glm::vec2{texture->get_width(), texture->get_height()});
            }
        }
    }
    return size;
}

auto Rendergraph_node::is_enabled() const -> bool
{
    return m_enabled;
}

auto Rendergraph_node::get_rendergraph() -> Rendergraph&
{
    return m_rendergraph;
}

auto Rendergraph_node::get_rendergraph() const -> const Rendergraph&
{
    return m_rendergraph;
}

auto Rendergraph_node::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Rendergraph_node::set_enabled(bool value)
{
    m_enabled = value;
}

auto Rendergraph_node::register_input(const erhe::utility::Debug_label debug_label, const int key) -> bool
{
    if (!inputs_allowed()) {
        log_tail->error("Node '{}' inputs are not allowed (label = {}, key = {})", get_debug_label().string_view(), debug_label.string_view(), key);
        return false;
    }

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    // Check for duplicate key
    for (const erhe::graph::Pin& pin : get_input_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            log_tail->error("Node '{}' input key '{}' is already registered", get_debug_label().string_view(), key);
            return false;
        }
    }

    base_make_input_pin(static_cast<std::size_t>(key), debug_label.string_view());
    return true;
}

auto Rendergraph_node::register_output(const erhe::utility::Debug_label debug_label, const int key) -> bool
{
    if (!outputs_allowed()) {
        log_tail->error("Node '{}' outputs are not allowed (label = {}, key = {})", get_debug_label().string_view(), debug_label.string_view(), key);
        return false;
    }

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    // Check for duplicate key
    for (const erhe::graph::Pin& pin : get_output_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            log_tail->error("Node '{}' output key '{}' is already registered", get_debug_label().string_view(), key);
            return false;
        }
    }

    base_make_output_pin(static_cast<std::size_t>(key), debug_label.string_view());
    return true;
}

auto Rendergraph_node::inputs_allowed() const -> bool
{
    return true;
}

auto Rendergraph_node::outputs_allowed() const -> bool
{
    return true;
}

void Rendergraph_node::set_depth(int depth)
{
    m_depth = depth;

    // Propagate depth to downstream consumers via output pin links
    for (const erhe::graph::Pin& pin : get_output_pins()) {
        for (erhe::graph::Link* link : pin.get_links()) {
            erhe::graph::Pin* sink_pin = link->get_sink();
            if (sink_pin == nullptr) {
                continue;
            }
            Rendergraph_node* consumer = static_cast<Rendergraph_node*>(sink_pin->get_owner_node());
            if ((consumer != nullptr) && (consumer->get_depth() <= depth)) {
                consumer->set_depth(depth + 1);
            }
        }
    }
}

auto Rendergraph_node::get_depth() const -> int
{
    return m_depth;
}

void Rendergraph_node::set_position(glm::vec2 position)
{
    m_position = position;
}

auto Rendergraph_node::get_position() const -> glm::vec2
{
    return m_position;
}

void Rendergraph_node::set_selected(bool selected)
{
    m_selected = selected;
}

auto Rendergraph_node::get_selected() const -> bool
{
    return m_selected;
}

} // namespace erhe::rendergraph
