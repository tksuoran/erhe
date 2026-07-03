#pragma once

#include "erhe_texgen/composer.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace erhe::texgen {
    class Compose_node;
}

namespace editor {

// Compose_options matching the editor's render helper shader contract
// (Texture_renderer): entry point main(), output_variable out_color, uv from
// the v_uv varying, and Uniform_declaration_mode::none with the "params." UBO
// accessor prefix (Vulkan rejects default-initialized plain uniforms, so
// parameters live in a std140 UBO built from Shader_code::get_uniforms()).
[[nodiscard]] auto texture_graph_compose_options() -> erhe::texgen::Compose_options;

class Texture_graph_node;

// Result of assembling an editor texture graph subtree into an
// erhe::texgen::Compose_node DAG. Owns every Compose_node created for the
// composition; the whole result must outlive any Composer::compose() call that
// reads it (the composer holds raw pointers into this DAG).
class Texture_compose_dag
{
public:
    Texture_compose_dag();
    ~Texture_compose_dag() noexcept;
    Texture_compose_dag(Texture_compose_dag&&) noexcept;
    auto operator=(Texture_compose_dag&&) noexcept -> Texture_compose_dag&;
    Texture_compose_dag(const Texture_compose_dag&) = delete;
    void operator=(const Texture_compose_dag&) = delete;

    std::vector<std::unique_ptr<erhe::texgen::Compose_node>> nodes;   // owns all compose nodes
    erhe::texgen::Compose_node*                              sink{nullptr};
    std::size_t                                             sink_output_index{0};
    bool                                                   ok{false};
};

// Walks the editor texture graph upstream from sink_node's output slot,
// creating one erhe::texgen::Compose_node per reachable descriptor-driven
// editor node (stable small int ids), calling each node's configure() to push
// its live parameter values, and wiring set_input for connected inputs.
// Unconnected inputs are left for the descriptor's default expression.
//
// sink_node must expose a descriptor(); a node without one (e.g. the output
// node, which has no GLSL of its own) is not a valid compose sink - compose the
// node feeding its input instead. Cycles are skipped defensively (the graph
// already rejects them on connect, and texgen has its own cycle guard).
//
// ok is false when the sink has no descriptor; nodes is then empty.
[[nodiscard]] auto build_texture_compose_dag(Texture_graph_node& sink_node, std::size_t output_index) -> Texture_compose_dag;

} // namespace editor
