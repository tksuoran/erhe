#pragma once

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace erhe::application {

class Render_graph_node
{
public:
    explicit Render_graph_node(const std::string_view name);
    virtual ~Render_graph_node();

    virtual void execute_render_graph_node() = 0;

    [[nodiscard]] auto get_dependencies() const -> const std::vector<Render_graph_node*>&;
    [[nodiscard]] auto name            () const -> const std::string&;
    void add_dependency(Render_graph_node* dependency);

protected:
    std::mutex                      m_mutex;
    std::vector<Render_graph_node*> m_dependencies;
    std::string                     m_name;
};

} // namespace erhe::application
