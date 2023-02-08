#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/mesh.hpp"

#include <functional>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::primitive
{
    class Build_info;
}

namespace erhe::scene
{
    class Mesh;
}

namespace editor
{

class Mesh_operation
    : public IOperation
{
public:
    class Parameters
    {
    public:
        erhe::primitive::Build_info& build_info;
    };

protected:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        erhe::scene::Mesh_data             before;
        erhe::scene::Mesh_data             after;
    };

    explicit Mesh_operation(Parameters&& parameters);
    ~Mesh_operation() noexcept override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

    // Public API
    void add_entry(Entry&& entry);
    void make_entries(
        const std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation
    );

private:
    Parameters         m_parameters;
    std::vector<Entry> m_entries;
};

}
