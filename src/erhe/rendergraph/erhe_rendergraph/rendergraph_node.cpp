// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::rendergraph {

void Rendergraph_node_deleter::operator()(Rendergraph_node* node) const
{
    if (node == nullptr) {
        return;
    }
    node->on_released();
    // m_is_registered is false here only when the rendergraph was destroyed
    // first (Rendergraph::~Rendergraph()): no frame can render the node.
    if (!node->m_is_registered) {
        delete node;
        return;
    }
    node->m_rendergraph.release_node(node);
}

void Rendergraph_node_deleter::adopt(Rendergraph_node& node)
{
    node.m_has_deleter = true;
}

Rendergraph_node::Rendergraph_node(Rendergraph& rendergraph, const erhe::utility::Debug_label debug_label)
    : erhe::graph::Node{debug_label.string_view()}
    , m_rendergraph    {rendergraph}
    , m_debug_label    {debug_label}
{
    const std::size_t frames_in_flight = m_rendergraph.get_graphics_device().get_number_of_frames_in_flight();
    ERHE_VERIFY(frames_in_flight > 0);
    m_frame_records.resize(frames_in_flight);
    m_rendergraph.register_node(this);
}

Rendergraph_node::~Rendergraph_node() noexcept
{
    log_tail->trace("~Rendergraph_node() {}", get_debug_label().string_view());
    // Destroyed only through Rendergraph_node_deleter: after its rendergraph
    // unregistered it (Rendergraph::release_node()), or after the rendergraph
    // itself was destroyed (Rendergraph::~Rendergraph() clears the flag). A
    // registered node here was not created by make_rendergraph_node().
    ERHE_VERIFY(!m_is_registered);
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

    if (erhe::utility::Thread_roles::get_frame_phase() == erhe::utility::Frame_phase::r_render) {
        const Frame_record* record = get_render_frame_record();
        if (record == nullptr) {
            return nullptr;
        }
        for (std::size_t i = 0; i < record->input_count; ++i) {
            if (record->inputs[i].key == static_cast<std::size_t>(key)) {
                return record->inputs[i].node;
            }
        }
        log_tail->error("Node '{}' input for key '{}' is not registered", get_debug_label().string_view(), key);
        return nullptr;
    }

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

    if (erhe::utility::Thread_roles::get_frame_phase() == erhe::utility::Frame_phase::r_render) {
        const Frame_record* record = get_render_frame_record();
        if (record == nullptr) {
            return nullptr;
        }
        for (std::size_t i = 0; i < record->output_count; ++i) {
            if (record->outputs[i].key == static_cast<std::size_t>(key)) {
                return record->outputs[i].node;
            }
        }
        log_tail->error("Node '{}' output for key '{}' is not registered", get_debug_label().string_view(), key);
        return nullptr;
    }

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

auto Rendergraph_node::is_enabled_in_frame(const uint64_t frame_index) const -> bool
{
    const Frame_record& record = m_frame_records[frame_index % m_frame_records.size()];
    return (record.frame_index == frame_index) && record.enabled;
}

namespace {

// Which end of a pin's first link names the connected node.
enum class Link_end : unsigned int {
    source, // of an input pin: the producer
    sink    // of an output pin: the consumer
};

[[nodiscard]] auto first_linked_node(const erhe::graph::Pin& pin, const Link_end link_end) -> Rendergraph_node*
{
    const std::vector<erhe::graph::Link*>& links = pin.get_links();
    if (links.empty()) {
        return nullptr;
    }
    erhe::graph::Pin* other_pin = (link_end == Link_end::source) ? links.front()->get_source() : links.front()->get_sink();
    return (other_pin != nullptr) ? static_cast<Rendergraph_node*>(other_pin->get_owner_node()) : nullptr;
}

}

void Rendergraph_node::record_frame(const uint64_t frame_index)
{
    Frame_record& record = m_frame_records[frame_index % m_frame_records.size()];
    record.frame_index  = frame_index;
    record.enabled      = m_enabled;
    record.input_count  = 0;
    record.output_count = 0;
    for (const erhe::graph::Pin& pin : get_input_pins()) {
        record.inputs[record.input_count++] = Frame_link{
            .key  = pin.get_key(),
            .node = first_linked_node(pin, Link_end::source)
        };
    }
    for (const erhe::graph::Pin& pin : get_output_pins()) {
        record.outputs[record.output_count++] = Frame_link{
            .key  = pin.get_key(),
            .node = first_linked_node(pin, Link_end::sink)
        };
    }
}

auto Rendergraph_node::get_render_frame_record() const -> const Frame_record*
{
    const uint64_t      frame_index = m_rendergraph.get_graphics_device().get_frame_index();
    const Frame_record& record      = m_frame_records[frame_index % m_frame_records.size()];
    return (record.frame_index == frame_index) ? &record : nullptr;
}

void Rendergraph_node::prepare_rendergraph_node(const uint64_t frame_index)
{
    static_cast<void>(frame_index);
}

void Rendergraph_node::on_released()
{
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

auto Rendergraph_node::submits_command_buffer() const -> bool
{
    return false;
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

} // namespace erhe::rendergraph
