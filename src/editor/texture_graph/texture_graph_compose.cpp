#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_payload.hpp"

#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

    // Returns the Compose_node for node (creating it on first visit), or nullptr
    // when node has no descriptor or a cycle was hit.
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
            erhe::texgen::Compose_node* source = build(*root.source_node);
            if (source != nullptr) {
                combiner->set_input(i, source, root.output_index);
            }
        }
        return combiner;
    }

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
            erhe::texgen::Compose_node* source_compose = build(*source.source_node);
            if (source_compose != nullptr) {
                compose_node->set_input(input_index, source_compose, source.output_index);
            }
        }

        m_visiting.erase(&node);
        return compose_node;
    }

private:
    Texture_compose_dag&                                             m_dag;
    std::unordered_map<Texture_graph_node*, erhe::texgen::Compose_node*> m_by_node;
    std::unordered_set<Texture_graph_node*>                          m_visiting;
    int                                                              m_next_id{1};
};

} // namespace

auto build_texture_compose_dag(Texture_graph_node& sink_node, const std::size_t output_index) -> Texture_compose_dag
{
    Texture_compose_dag dag{};
    Dag_builder builder{dag};
    erhe::texgen::Compose_node* sink = builder.build(sink_node);
    if (sink == nullptr) {
        dag.ok = false;
        return dag;
    }
    dag.sink              = sink;
    dag.sink_output_index = output_index;
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
