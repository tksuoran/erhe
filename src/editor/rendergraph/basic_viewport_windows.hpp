#pragma once

#include "erhe/rendergraph/rendergraph_node.hpp"

#include <memory>

namespace editor
{

class Basic_viewport_window;

class Basic_viewport_windows
    : public erhe::rendergraph::Rendergraph_node
{
public:
    Basic_viewport_windows();

private:
    std::vector<std::shared_ptr<Basic_viewport_window>> m_viewport_windows;
};

} // namespace editor
