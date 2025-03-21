// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::rendergraph {

Rendergraph_node::Rendergraph_node(Rendergraph& rendergraph, const std::string_view name)
    : m_rendergraph{rendergraph}
    , m_name       {name}
{
    m_rendergraph.register_node(this);
}

Rendergraph_node::~Rendergraph_node() noexcept
{
    log_tail->trace("~Rendergraph_node() {}", get_name());
    m_rendergraph.unregister_node(this);
}

auto Rendergraph_node::get_id() const -> std::size_t
{
    return id.get_id();
}

auto Rendergraph_node::get_input(Routing, const int key, const int depth) const -> const Rendergraph_consumer_connector*
{
    SPDLOG_LOGGER_TRACE(
        log_tail,
        "{} Rendergraph_node::get_input(resource_routing = {}, key = {}, depth = {})",
        get_name(),
        c_str(resource_routing),
        key,
        depth
    );

    ERHE_VERIFY(inputs_allowed());
    ERHE_VERIFY(depth < rendergraph_max_depth);

    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i != m_inputs.end()) {
        return &*i;
    }

    log_tail->error("Node '{}' input for key '{}' is not registered", get_name(), key);
    return nullptr;
}

auto Rendergraph_node::get_consumer_input_node(const Routing resource_routing, const int key, const int depth) const -> Rendergraph_node*
{
    if (!inputs_allowed()) {
        log_tail->error("Node '{}' inputs are not allowed ('{}')", get_name(), key);
        return nullptr;
    }

    const auto* input = get_input(resource_routing, key, depth + 1);
    if (input == nullptr) {
        log_tail->error("Node '{}' input for key '{}' is not registered", get_name(), key);
        return nullptr;
    }

    if (input->producer_nodes.empty()) {
        //log_tail->warning("Node '{}' input for key '{}' is not connected", get_name(), key);
        return nullptr;
    }

    return input->producer_nodes.front();
}

auto Rendergraph_node::get_consumer_input_texture(const Routing resource_routing, const int key, const int depth) const -> std::shared_ptr<erhe::graphics::Texture>
{
    auto* producer = get_consumer_input_node(resource_routing, key, depth);
    return (producer != nullptr)
        ? producer->get_producer_output_texture(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Texture>{};
}

auto Rendergraph_node::get_consumer_input_framebuffer(const Routing resource_routing, const int key, const int depth) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    auto* producer = get_consumer_input_node(resource_routing, key, depth);
    return (producer != nullptr)
        ? producer->get_producer_output_framebuffer(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Framebuffer>{};
}

auto Rendergraph_node::get_consumer_input_viewport(const Routing resource_routing, const int key, const int depth) const -> erhe::math::Viewport
{
    auto* producer = get_consumer_input_node(resource_routing, key, depth);
    return (producer != nullptr)
        ? producer->get_producer_output_viewport(resource_routing, key, depth + 1)
        : erhe::math::Viewport{};
}

auto Rendergraph_node::get_output(const Routing resource_routing, const int key, const int depth) const -> const Rendergraph_producer_connector*
{
    static_cast<void>(resource_routing);

    SPDLOG_LOGGER_TRACE(
        log_tail,
        "{} Rendergraph_node::get_output(resource_routing = {}, key = {}, depth = {})",
        get_name(),
        c_str(resource_routing),
        key,
        depth
    );

    ERHE_VERIFY(outputs_allowed());
    ERHE_VERIFY(depth < rendergraph_max_depth);

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i != m_outputs.end()) {
        return &*i;
    };

    log_tail->error("Node '{}' output for key '{}' is not registered", get_name(), key);
    return nullptr;
}

auto Rendergraph_node::get_producer_output_node(const Routing resource_routing, const int key, const int depth) const -> Rendergraph_node*
{
    if (!outputs_allowed()) {
        log_tail->error("Node '{}' outputs are not allowed ('{}')", get_name(), key);
        return nullptr;
    }

    const auto* output = get_output(resource_routing, key, depth + 1);
    if (output == nullptr) {
        log_tail->error("Node '{}' output for key '{}' is not registered", get_name(), key);
        return nullptr;
    }

    if (output->consumer_nodes.empty()) {
        log_tail->error("Node '{}' output for key '{}' is not connected", get_name(), key);
        return nullptr;
    }

    return output->consumer_nodes.front();
}

auto Rendergraph_node::get_producer_output_texture(const Routing resource_routing, const int key, const int depth) const -> std::shared_ptr<erhe::graphics::Texture>
{
    auto* consumer = get_producer_output_node(resource_routing, key, depth);
    return (consumer != nullptr)
        ? consumer->get_consumer_input_texture(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Texture>{};
}

auto Rendergraph_node::get_producer_output_framebuffer(const Routing resource_routing, const int key, const int depth) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    auto* consumer = get_producer_output_node(resource_routing, key, depth);
    return (consumer != nullptr)
        ? consumer->get_consumer_input_framebuffer(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Framebuffer>{};
}

auto Rendergraph_node::get_producer_output_viewport(const Routing resource_routing, const int key, const int depth) const -> erhe::math::Viewport
{
    auto* consumer = get_producer_output_node(resource_routing, key, depth);
    return (consumer != nullptr)
        ? consumer->get_consumer_input_viewport(resource_routing, key, depth + 1)
        : erhe::math::Viewport{};
}

auto Rendergraph_node::get_inputs() const -> const std::vector<Rendergraph_consumer_connector>&
{
    return m_inputs;
}

auto Rendergraph_node::get_inputs() -> std::vector<Rendergraph_consumer_connector>&
{
    return m_inputs;
}

auto Rendergraph_node::get_outputs() const -> const std::vector<Rendergraph_producer_connector>&
{
    return m_outputs;
}

auto Rendergraph_node::get_outputs() -> std::vector<Rendergraph_producer_connector>&
{
    return m_outputs;
}

auto Rendergraph_node::get_size() const -> std::optional<glm::vec2>
{
    std::optional<glm::vec2> size;

    for (const auto& output : m_outputs) {
        if (output.resource_routing == Routing::None) {
            continue;
        }

        const auto& texture = get_producer_output_texture(output.resource_routing, output.key);
        if (
            texture &&
            (texture->target() == gl::Texture_target::texture_2d) &&
            (texture->width () >= 1) &&
            (texture->height() >= 1) &&
            (gl_helpers::has_color(texture->internal_format()))
        ) {
            if (!size.has_value()) {
                size = glm::vec2{texture->width(), texture->height()};
            } else {
                size = glm::max(size.value(), glm::vec2{texture->width(), texture->height()});
            }
        }
    }
    return size;
}

auto Rendergraph_node::is_enabled() const -> bool
{
    return m_enabled;
}

auto Rendergraph_node::get_name() const -> const std::string&
{
    return m_name;
}

void Rendergraph_node::set_enabled(bool value)
{
    m_enabled = value;
}

auto Rendergraph_node::register_input(const Routing resource_routing, const std::string_view label, const int key) -> bool
{
    if (!inputs_allowed()) {
        log_tail->error("Node '{}' inputs are not allowed (label = {}, key = {})", get_name(), label, key);
        return false;
    }

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i != m_inputs.end()) {
        log_tail->error("Node '{}' input key '{}' is already registered", get_name(), key);
        return false;
    }
    m_inputs.push_back(Rendergraph_consumer_connector{resource_routing, std::string{label}, key});
    return true;
}

auto Rendergraph_node::register_output(const Routing resource_routing, const std::string_view label, const int key) -> bool
{
    if (!outputs_allowed()) {
        log_tail->error("Node '{}' outputs are not allowed (label = {}, key = {})", get_name(), label, key);
        return false;
    }

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i != m_outputs.end()) {
        log_tail->error("Node '{}' output key '{}' is already registered", get_name(), key);
        return false;
    }
    m_outputs.push_back(Rendergraph_producer_connector{resource_routing, std::string{label}, key,});
    return true;
}

auto Rendergraph_node::connect_input(const int key, Rendergraph_node* producer) -> bool
{
    if (!inputs_allowed()) {
        log_tail->error("Node '{}' inputs are not allowed (key = {})", get_name(), key);
        return false;
    }

    ERHE_VERIFY(producer != nullptr);

    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i == m_inputs.end()) {
        log_tail->error("Node '{}' does not have input '{}' registered", get_name(), key);
        return false;
    }

    auto& producer_nodes = i->producer_nodes;

    // Only single input with 'Resource_provided_by_producer' allowed
    if (
        (!producer_nodes.empty()) &&
        (i->resource_routing == Routing::Resource_provided_by_producer)
    ) {
        log_tail->warn(
            "Node '{}' input key {} already has {} producer{} registered",
            get_name(),
            key,
            producer_nodes.size(),
            producer_nodes.size() > 1 ? "s" : ""
        );
        return false;
    }

    auto j = std::find_if(
        producer_nodes.begin(),
        producer_nodes.end(),
        [producer](Rendergraph_node* entry) {
            return entry == producer;
        }
    );
    if (j != producer_nodes.end()) {
        log_tail->error(
            "Node '{}' input key '{}' already has producer '{}' connected",
            get_name(),
            key,
            producer->get_name()
        );
        return false;
    }

    producer_nodes.push_back(producer);

    set_depth(producer->get_depth() + 1);
    return true;
}

auto Rendergraph_node::connect_output(const int key, Rendergraph_node* consumer) -> bool
{
    if (!outputs_allowed()) {
        log_tail->error("Node '{}' outputs are not allowed (key = {})", get_name(), key);
        return false;
    }

    ERHE_VERIFY(consumer != nullptr);

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i == m_outputs.end()) {
        log_tail->error("Node '{}' does not have output '{}' registered", get_name(), key);
        return false;
    }

    auto& consumer_nodes = i->consumer_nodes;

    // Only single output with 'Resource_provided_by_consumer' allowed
    if (
        (!consumer_nodes.empty()) &&
        (i->resource_routing == Routing::Resource_provided_by_consumer)
    ) {
        log_tail->warn(
            "Node '{}' output key {} already has {} consumer{} registered",
            get_name(),
            key,
            consumer_nodes.size(),
            consumer_nodes.size() > 1 ? "s" : ""
        );
        return false;
    }

    auto j = std::find_if(
        consumer_nodes.begin(),
        consumer_nodes.end(),
        [consumer](Rendergraph_node* entry) {
            return entry == consumer;
        }
    );
    if (j != consumer_nodes.end()) {
        log_tail->error(
            "Node '{}' output key '{}' already has consumer '{}' connected",
            get_name(),
            key,
            consumer->get_name()
        );
        return false;
    }

    consumer_nodes.push_back(consumer);
    return true;
}

auto Rendergraph_node::disconnect_input(const int key, Rendergraph_node* producer) -> bool
{
    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i == m_inputs.end()) {
        log_tail->error("Node '{}' does not have input '{}' registered", get_name(), key);
        return false;
    }

    auto& producer_nodes = i->producer_nodes;

    auto j = std::remove_if(
        producer_nodes.begin(),
        producer_nodes.end(),
        [producer](Rendergraph_node* entry) {
            return entry == producer;
        }
    );
    if (j == producer_nodes.end()) {
        log_tail->error(
            "Node '{}' input key '{}' producer '{}' not found",
            get_name(),
            key,
            producer->get_name()
        );
        return false;
    }

    producer_nodes.erase(j, producer_nodes.end());
    return true;
}

auto Rendergraph_node::disconnect_output(const int key, Rendergraph_node* consumer) -> bool
{
    ERHE_VERIFY(consumer != nullptr);

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry) {
            return entry.key == key;
        }
    );
    if (i == m_outputs.end()) {
        log_tail->error("Node '{}' does not have output '{}' registered", get_name(), key);
        return false;
    }

    auto& consumer_nodes = i->consumer_nodes;
    auto j = std::remove_if(
        consumer_nodes.begin(),
        consumer_nodes.end(),
        [consumer](Rendergraph_node* entry) {
            return entry == consumer;
        }
    );
    if (j == consumer_nodes.end()) {
        log_tail->error(
            "Node '{}' output key '{}' consumer '{}' not found",
            get_name(),
            key,
            consumer->get_name()
        );
        return false;
    }

    consumer_nodes.erase(j, consumer_nodes.end());
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
    for (const auto& output : m_outputs) {
        for (auto* node : output.consumer_nodes) {
            if (node->get_depth() <= depth) {
                node->set_depth(depth + 1);
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
