// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/application_log.hpp"
//#include <mango/include/mango/math/vector256_int8x32.hpp>

namespace erhe::application {

Rendergraph_node::Rendergraph_node(const std::string_view name)
    : m_name{name}
{
}

Rendergraph_node::~Rendergraph_node()
{
    if (m_rendergraph)
    {
        m_rendergraph->unregister_node(this);
    }
}

[[nodiscard]] auto Rendergraph_node::get_input(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> const Rendergraph_consumer_connector*
{
    static_cast<void>(resource_routing);

    SPDLOG_LOGGER_TRACE(
        log_rendergraph,
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
        [&key](
            const Rendergraph_consumer_connector& entry
        )
        {
            return entry.key == key;
        }
    );
    if (i != m_inputs.end())
    {
        return &*i;
    }

    log_rendergraph->error("Node '{}' input for key '{}' is not registered", get_name(), key);
    return nullptr;
}

[[nodiscard]] auto Rendergraph_node::get_consumer_input_node(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::weak_ptr<Rendergraph_node>
{
    if (!inputs_allowed())
    {
        log_rendergraph->error("Node '{}' inputs are not allowed ('{}')", get_name(), key);
        return std::weak_ptr<Rendergraph_node>{};
    }

    const auto* input = get_input(resource_routing, key, depth + 1);
    if (input == nullptr)
    {
        log_rendergraph->error("Node '{}' input for key '{}' is not registered", get_name(), key);
        return std::weak_ptr<Rendergraph_node>{};
    }

    if (input->producer_nodes.empty())
    {
        //log_rendergraph->warning("Node '{}' input for key '{}' is not connected", get_name(), key);
        return std::weak_ptr<Rendergraph_node>{};
    }

    return input->producer_nodes.front();
}

[[nodiscard]] auto Rendergraph_node::get_consumer_input_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    const auto& producer_node = get_consumer_input_node(resource_routing, key, depth).lock();
    return producer_node
        ? producer_node->get_producer_output_texture(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Texture>{};
}

[[nodiscard]] auto Rendergraph_node::get_consumer_input_framebuffer(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    const auto& producer_node = get_consumer_input_node(resource_routing, key, depth).lock();
    return producer_node
        ? producer_node->get_producer_output_framebuffer(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Framebuffer>{};
}

auto Rendergraph_node::get_consumer_input_viewport(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> erhe::scene::Viewport
{
    const auto& producer_node = get_consumer_input_node(resource_routing, key, depth).lock();
    return producer_node
        ? producer_node->get_producer_output_viewport(resource_routing, key, depth + 1)
        : erhe::scene::Viewport{};
}

[[nodiscard]] auto Rendergraph_node::get_output(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> const Rendergraph_producer_connector*
{
    static_cast<void>(resource_routing);

    SPDLOG_LOGGER_TRACE(
        log_rendergraph,
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
        [&key](
            const Rendergraph_producer_connector& entry
        )
        {
            return entry.key == key;
        }
    );
    if (i != m_outputs.end())
    {
        return &*i;
    };

    log_rendergraph->error("Node '{}' output for key '{}' is not registered", get_name(), key);
    return nullptr;
}

[[nodiscard]] auto Rendergraph_node::get_producer_output_node(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::weak_ptr<Rendergraph_node>
{
    if (!outputs_allowed())
    {
        log_rendergraph->error("Node '{}' outputs are not allowed ('{}')", get_name(), key);
        return std::weak_ptr<Rendergraph_node>{};
    }

    const auto* output = get_output(resource_routing, key, depth + 1);
    if (output == nullptr)
    {
        log_rendergraph->error("Node '{}' output for key '{}' is not registered", get_name(), key);
        return std::weak_ptr<Rendergraph_node>{};
    }

    if (output->consumer_nodes.empty())
    {
        log_rendergraph->error("Node '{}' output for key '{}' is not connected", get_name(), key);
        return std::weak_ptr<Rendergraph_node>{};
    }

    return output->consumer_nodes.front();
}

[[nodiscard]] auto Rendergraph_node::get_producer_output_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    const auto& consumer_node = get_producer_output_node(resource_routing, key, depth).lock();
    return consumer_node
        ? consumer_node->get_consumer_input_texture(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Texture>{};
}

[[nodiscard]] auto Rendergraph_node::get_producer_output_framebuffer(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    const auto& consumer_node = get_producer_output_node(resource_routing, key, depth).lock();
    return consumer_node
        ? consumer_node->get_consumer_input_framebuffer(resource_routing, key, depth + 1)
        : std::shared_ptr<erhe::graphics::Framebuffer>{};
}

auto Rendergraph_node::get_producer_output_viewport(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> erhe::scene::Viewport
{
    const auto& consumer_node = get_producer_output_node(resource_routing, key, depth).lock();
    return consumer_node
        ? consumer_node->get_consumer_input_viewport(resource_routing, key, depth + 1)
        : erhe::scene::Viewport{};
}

[[nodiscard]] auto Rendergraph_node::get_inputs(
) const -> const std::vector<Rendergraph_consumer_connector>&
{
    return m_inputs;
}

[[nodiscard]] auto Rendergraph_node::get_inputs(
) -> std::vector<Rendergraph_consumer_connector>&
{
    return m_inputs;
}

[[nodiscard]] auto Rendergraph_node::get_outputs(
) const -> const std::vector<Rendergraph_producer_connector>&
{
    return m_outputs;
}

[[nodiscard]] auto Rendergraph_node::get_outputs(
) -> std::vector<Rendergraph_producer_connector>&
{
    return m_outputs;
}

[[nodiscard]] auto Rendergraph_node::is_enabled() const -> bool
{
    return m_enabled;
}

[[nodiscard]] auto Rendergraph_node::get_name() const -> const std::string&
{
    return m_name;
}

void Rendergraph_node::connect(Rendergraph* rendergraph)
{
    m_rendergraph = rendergraph;
}

void Rendergraph_node::set_enabled(bool value)
{
    m_enabled = value;
}

auto Rendergraph_node::register_input(
    const Resource_routing resource_routing,
    const std::string_view label,
    const int              key
) -> bool
{
    if (!inputs_allowed())
    {
        log_rendergraph->error("Node '{}' inputs are not allowed (label = {}, key = {})", get_name(), label, key);
        return false;
    }

    std::lock_guard<std::mutex> lock{m_mutex};

    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry)
        {
            return entry.key == key;
        }
    );
    if (i != m_inputs.end())
    {
        log_rendergraph->error("Node '{}' input key '{}' is already registered", get_name(), key);
        return false;
    }
    m_inputs.push_back(
        Rendergraph_consumer_connector{
            .resource_routing = resource_routing,       // Resource_routing
            .label = std::string{label},
            .key = key
        }
    );
    return true;
}

auto Rendergraph_node::register_output(
    const Resource_routing resource_routing,
    const std::string_view label,
    const int              key
) -> bool
{
    if (!outputs_allowed())
    {
        log_rendergraph->error("Node '{}' outputs are not allowed (label = {}, key = {})", get_name(), label, key);
        return false;
    }

    std::lock_guard<std::mutex> lock{m_mutex};

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry)
        {
            return entry.key == key;
        }
    );
    if (i != m_outputs.end())
    {
        log_rendergraph->error("Node '{}' output key '{}' is already registered", get_name(), key);
        return false;
    }
    m_outputs.push_back(
        Rendergraph_producer_connector{
            resource_routing,
            std::string{label},
            key,
            std::vector<
                std::weak_ptr<Rendergraph_node>
            >{}
        }
    );
    return true;
}

auto Rendergraph_node::connect_input(
    const int                       key,
    std::weak_ptr<Rendergraph_node> producer_node
) -> bool
{
    if (!inputs_allowed())
    {
        log_rendergraph->error("Node '{}' inputs are not allowed (key = {})", get_name(), key);
        return false;
    }

    const auto& producer = producer_node.lock();
    if (!producer)
    {
        log_rendergraph->error("Node '{}' input key '{}' producer node is expired or not not set, can not connect", get_name(), key);
        return false;
    }

    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry)
        {
            return entry.key == key;
        }
    );
    if (i == m_inputs.end())
    {
        log_rendergraph->error("Node '{}' does not have input '{}' registered", get_name(), key);
        return false;
    }

    auto& producer_nodes = i->producer_nodes;

    // Only single input with 'Resource_provided_by_producer' allowed
    if (
        (!producer_nodes.empty()) &&
        (i->resource_routing == Resource_routing::Resource_provided_by_producer)
    )
    {
        log_rendergraph->warn(
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
        [&producer](const std::weak_ptr<Rendergraph_node>& entry)
        {
            return entry.lock() == producer;
        }
    );
    if (j != producer_nodes.end())
    {
        log_rendergraph->error(
            "Node '{}' input key '{}' already has producer '{}' connected",
            get_name(),
            key,
            producer->get_name()
        );
        return false;
    }

    producer_nodes.push_back(producer_node);

    set_depth(producer->get_depth() + 1);
    return true;
}

auto Rendergraph_node::connect_output(
    const int                       key,
    std::weak_ptr<Rendergraph_node> consumer_node
) -> bool
{
    if (!outputs_allowed())
    {
        log_rendergraph->error("Node '{}' outputs are not allowed (key = {})", get_name(), key);
        return false;
    }

    const auto& consumer = consumer_node.lock();
    if (!consumer)
    {
        log_rendergraph->error("Node '{}' output key '{}' consumer node is expired or not not set, can not connect", get_name(), key);
        return false;
    }

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry)
        {
            return entry.key == key;
        }
    );
    if (i == m_outputs.end())
    {
        log_rendergraph->error("Node '{}' does not have output '{}' registered", get_name(), key);
        return false;
    }

    auto& consumer_nodes = i->consumer_nodes;

    // Only single output with 'Resource_provided_by_consumer' allowed
    if (
        (!consumer_nodes.empty()) &&
        (i->resource_routing == Resource_routing::Resource_provided_by_consumer)
    )
    {
        log_rendergraph->warn(
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
        [&consumer](const std::weak_ptr<Rendergraph_node>& entry)
        {
            return entry.lock() == consumer;
        }
    );
    if (j != consumer_nodes.end())
    {
        log_rendergraph->error(
            "Node '{}' output key '{}' already has consumer '{}' connected",
            get_name(),
            key,
            consumer->get_name()
        );
        return false;
    }

    consumer_nodes.push_back(consumer_node);
    return true;
}

auto Rendergraph_node::disconnect_input(
    const int                       key,
    std::weak_ptr<Rendergraph_node> producer_node
) -> bool
{
    const auto& producer = producer_node.lock();
    auto i = std::find_if(
        m_inputs.begin(),
        m_inputs.end(),
        [&key](const Rendergraph_consumer_connector& entry)
        {
            return entry.key == key;
        }
    );
    if (i == m_inputs.end())
    {
        log_rendergraph->error("Node '{}' does not have input '{}' registered", get_name(), key);
        return false;
    }

    auto& producer_nodes = i->producer_nodes;

    auto j = std::remove_if(
        producer_nodes.begin(),
        producer_nodes.end(),
        [&producer](const std::weak_ptr<Rendergraph_node>& entry)
        {
            return entry.lock() == producer;
        }
    );
    if (j == producer_nodes.end())
    {
        log_rendergraph->error(
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

auto Rendergraph_node::disconnect_output(
    const int                       key,
    std::weak_ptr<Rendergraph_node> consumer_node
) -> bool
{
    const auto& consumer = consumer_node.lock();
    if (!consumer)
    {
        log_rendergraph->error("Node '{}' output key '{}' consumer node is expired or not not set, can not disconnect", get_name(), key);
        return false;
    }

    auto i = std::find_if(
        m_outputs.begin(),
        m_outputs.end(),
        [&key](const Rendergraph_producer_connector& entry)
        {
            return entry.key == key;
        }
    );
    if (i == m_outputs.end())
    {
        log_rendergraph->error("Node '{}' does not have output '{}' registered", get_name(), key);
        return false;
    }

    auto& consumer_nodes = i->consumer_nodes;
    auto j = std::remove_if(
        consumer_nodes.begin(),
        consumer_nodes.end(),
        [&consumer](const std::weak_ptr<Rendergraph_node>& entry)
        {
            return entry.lock() == consumer;
        }
    );
    if (j == consumer_nodes.end())
    {
        log_rendergraph->error(
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
    for (const auto& output : m_outputs)
    {
        for (const auto& node : output.consumer_nodes)
        {
            const auto n = node.lock();
            if (n)
            {
                if (n->get_depth() <= depth)
                {
                    n->set_depth(depth + 1);
                }
            }
        }
    }
}

[[nodiscard]] auto Rendergraph_node::get_depth() const -> int
{
    return m_depth;
}

void Rendergraph_node::set_position(glm::vec2 position)
{
    m_position = position;
}

[[nodiscard]] auto Rendergraph_node::get_position() const -> glm::vec2
{
    return m_position;
}

void Rendergraph_node::set_selected(bool selected)
{
    m_selected = selected;
}

[[nodiscard]] auto Rendergraph_node::get_selected() const -> bool
{
    return m_selected;
}

} // namespace erhe::application
