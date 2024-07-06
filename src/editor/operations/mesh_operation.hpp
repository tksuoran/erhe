#pragma once

#include "operations/ioperation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"

#include <functional>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class Editor_context;
class Node_physics;

class Mesh_operation
    : public IOperation
{
public:
    class Parameters
    {
    public:
        Editor_context&             context;
        erhe::primitive::Build_info build_info;
    };

protected:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;

        class Version
        {
        public:
            std::shared_ptr<Node_physics>           node_physics{};
            std::vector<erhe::primitive::Primitive> primitives{};
        };
        Version before{};
        Version after{};
    };

    explicit Mesh_operation(Parameters&& parameters);
    ~Mesh_operation() noexcept override;

    // Implements IOperation
    auto describe() const -> std::string override;
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

    // Public API
    void add_entry(Entry&& entry);
    void make_entries(
        const std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation
    );

protected:
    Parameters         m_parameters;
    std::vector<Entry> m_entries;
};

}
