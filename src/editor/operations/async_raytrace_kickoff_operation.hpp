#pragma once

#include "operations/operation.hpp"

#include <memory>
#include <vector>

namespace erhe {
    class Item_base;
}

namespace editor {

class Scene_root;

class Async_raytrace_kickoff_operation : public Operation
{
public:
    Async_raytrace_kickoff_operation(
        std::shared_ptr<Scene_root>                   scene_root,
        std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items
    );
    ~Async_raytrace_kickoff_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::shared_ptr<Scene_root>                   m_scene_root;
    std::vector<std::shared_ptr<erhe::Item_base>> m_mesh_node_items;
};

}
