#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_payload.hpp"

#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace editor {

auto texture_graph_compose_options() -> erhe::texgen::Compose_options
{
    erhe::texgen::Compose_options options{};
    options.function_name           = "main";
    options.output_variable_name     = "out_color";
    options.uv_source_expression     = "v_uv";
    options.uniform_declaration_mode = erhe::texgen::Uniform_declaration_mode::none;
    options.uniform_accessor_prefix  = "params.";
    return options;
}

Texture_compose_dag::Texture_compose_dag() = default;
Texture_compose_dag::~Texture_compose_dag() noexcept = default;
Texture_compose_dag::Texture_compose_dag(Texture_compose_dag&&) noexcept = default;
auto Texture_compose_dag::operator=(Texture_compose_dag&&) noexcept -> Texture_compose_dag& = default;

namespace {

// Depth-first builder mapping each editor node to one Compose_node, shared when
// reached through multiple paths (a DAG, not a tree). The visiting set breaks
// cycles: a source already on the recursion stack is skipped, leaving that
// input unconnected (its descriptor default is used).
class Dag_builder
{
public:
    explicit Dag_builder(Texture_compose_dag& dag)
        : m_dag{dag}
    {
    }

    // Creates a synthetic combiner Compose_node from descriptor (sharing this
    // builder's id space) and wires each of its inputs to the corresponding
    // channel root's source subtree. A null root leaves that input unconnected.
    auto build_combiner(
        const erhe::texgen::Node_descriptor& descriptor,
        const std::vector<Texture_payload>&  channel_roots
    ) -> erhe::texgen::Compose_node*
    {
        std::unique_ptr<erhe::texgen::Compose_node> owned = std::make_unique<erhe::texgen::Compose_node>(descriptor, m_next_id++);
        erhe::texgen::Compose_node* combiner = owned.get();
        m_dag.nodes.push_back(std::move(owned));

        const std::size_t input_count = descriptor.inputs.size();
        for (std::size_t i = 0, end = std::min(channel_roots.size(), input_count); i < end; ++i) {
            const Texture_payload& root = channel_roots[i];
            if (root.source_node == nullptr) {
                continue; // unconnected - the combiner input's default expression is used
            }
            bind_input(*combiner, i, root);
        }
        return combiner;
    }

    // Wires the sink's input `input_index` to the source described by `source`.
    // A buffer source becomes a sampler-source leaf (its texture is sampled, its
    // input subtree is not inlined); any other source is composed recursively.
    void bind_input(erhe::texgen::Compose_node& sink, std::size_t input_index, const Texture_payload& source)
    {
        if (source.source_node->is_buffer()) {
            erhe::texgen::Compose_node* sampler_source =
                get_sampler_source(*source.source_node, source.output_index, source.value_type);
            sink.set_input(input_index, sampler_source, 0);
            return;
        }
        erhe::texgen::Compose_node* source_compose = build(*source.source_node);
        if (source_compose != nullptr) {
            sink.set_input(input_index, source_compose, source.output_index);
        }
    }

    // Sampler-source leaf for a buffer node: one sampler binding per buffer
    // (shared across the buffer's output slots), one Compose_node per
    // (buffer, output slot) so each slot samples with its own value type.
    auto get_sampler_source(
        Texture_graph_node&      buffer,
        std::size_t              output_index,
        erhe::texgen::Value_type type
    ) -> erhe::texgen::Compose_node*
    {
        int binding;
        const auto binding_it = m_buffer_binding.find(&buffer);
        if (binding_it == m_buffer_binding.end()) {
            binding = m_next_binding++;
            m_buffer_binding.emplace(&buffer, binding);
            m_dag.sampler_sources.push_back(Texture_sampler_source{.binding = binding, .buffer_node = &buffer});
        } else {
            binding = binding_it->second;
        }
        const std::pair<Texture_graph_node*, std::size_t> key{&buffer, output_index};
        const auto slot_it = m_sampler_by_slot.find(key);
        if (slot_it != m_sampler_by_slot.end()) {
            return slot_it->second;
        }
        std::unique_ptr<erhe::texgen::Compose_node> owned =
            std::make_unique<erhe::texgen::Compose_node>(m_next_id++, binding, type);
        erhe::texgen::Compose_node* sampler_source = owned.get();
        m_dag.nodes.push_back(std::move(owned));
        m_sampler_by_slot.emplace(key, sampler_source);
        return sampler_source;
    }

    // Returns the Compose_node for a (non-buffer) node, creating it on first
    // visit, or nullptr when node has no descriptor or a cycle was hit.
    auto build(Texture_graph_node& node) -> erhe::texgen::Compose_node*
    {
        const auto existing = m_by_node.find(&node);
        if (existing != m_by_node.end()) {
            return existing->second;
        }
        const erhe::texgen::Node_descriptor* descriptor = node.descriptor();
        if (descriptor == nullptr) {
            return nullptr;
        }
        if (m_visiting.count(&node) != 0) {
            return nullptr; // cycle - defensive; connect() already rejects cycles
        }
        m_visiting.insert(&node);

        std::unique_ptr<erhe::texgen::Compose_node> owned = std::make_unique<erhe::texgen::Compose_node>(*descriptor, m_next_id++);
        erhe::texgen::Compose_node* compose_node = owned.get();
        m_dag.nodes.push_back(std::move(owned));
        m_by_node.emplace(&node, compose_node);

        node.configure(*compose_node);

        for (std::size_t input_index = 0, end = descriptor->inputs.size(); input_index < end; ++input_index) {
            const Texture_payload source = node.input_from_links(input_index);
            if (source.source_node == nullptr) {
                continue; // unconnected - descriptor default expression is used
            }
            bind_input(*compose_node, input_index, source);
        }

        m_visiting.erase(&node);
        return compose_node;
    }

private:
    Texture_compose_dag&                                                m_dag;
    std::unordered_map<Texture_graph_node*, erhe::texgen::Compose_node*> m_by_node;
    std::unordered_set<Texture_graph_node*>                             m_visiting;
    std::unordered_map<Texture_graph_node*, int>                       m_buffer_binding;   // buffer node -> sampler binding
    std::map<std::pair<Texture_graph_node*, std::size_t>, erhe::texgen::Compose_node*> m_sampler_by_slot;
    int                                                                m_next_id{1};
    int                                                                m_next_binding{0};
};

} // namespace

auto build_texture_compose_dag(Texture_graph_node& sink_node, const std::size_t output_index) -> Texture_compose_dag
{
    Texture_compose_dag dag{};
    Dag_builder builder{dag};
    erhe::texgen::Compose_node* sink = nullptr;
    if (sink_node.is_buffer()) {
        // Rendering / exporting a buffer node itself: sample its own texture
        // rather than re-inlining its input subtree. The buffer's own texture
        // is produced by its render_products() each dirty pass.
        sink = builder.get_sampler_source(sink_node, output_index, sink_node.get_output(output_index).value_type);
    } else {
        sink = builder.build(sink_node);
    }
    if (sink == nullptr) {
        dag.ok = false;
        return dag;
    }
    dag.sink              = sink;
    dag.sink_output_index = (sink->is_sampler_source() ? 0 : output_index);
    dag.ok                = true;
    return dag;
}

auto build_texture_combiner_dag(
    const erhe::texgen::Node_descriptor& combiner_descriptor,
    const std::vector<Texture_payload>&  channel_roots
) -> Texture_compose_dag
{
    Texture_compose_dag dag{};
    Dag_builder builder{dag};
    erhe::texgen::Compose_node* combiner = builder.build_combiner(combiner_descriptor, channel_roots);
    if (combiner == nullptr) {
        dag.ok = false;
        return dag;
    }
    dag.sink              = combiner;
    dag.sink_output_index = 0;
    dag.ok                = true;
    return dag;
}

} // namespace editor
