#pragma once

#include "erhe/application/render_graph_node.hpp"
#include "erhe/application/render_graph.hpp"

namespace erhe::application {

Render_graph_node::Render_graph_node(const std::string_view name)
    : m_name{name}
{
}

Render_graph_node::~Render_graph_node()
{
}

[[nodiscard]] auto Render_graph_node::get_dependencies() const -> const std::vector<Render_graph_node*>&
{
    return m_dependencies;
}

[[nodiscard]] auto Render_graph_node::name() const -> const std::string&
{
    return m_name;
}

void Render_graph_node::add_dependency(Render_graph_node* dependency)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_dependencies.push_back(dependency);
}

} // namespace erhe::application
