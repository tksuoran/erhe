#pragma once

#include "operations/operation.hpp"
#include "scene/generated/scene_export_settings.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace erhe { class Hierarchy; }

namespace editor {

class App_context;
class Scene_root;

// Bakes the static meshes of a scene into world space (doc/editor/world_space_bake.md).
class Bake_world_space_operation : public Operation
{
public:
    class Parameters
    {
    public:
        App_context&                context;
        erhe::primitive::Build_info build_info;
        std::shared_ptr<Scene_root> scene_root;
        Scene_export_settings       settings;
    };

    explicit Bake_world_space_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

    [[nodiscard]] auto get_baked_count      () const -> std::size_t { return m_entries.size(); } // planned until the first execute()
    [[nodiscard]] auto get_placeholder_count() const -> std::size_t;
    [[nodiscard]] auto get_kept_reasons     () const -> const std::map<std::string, std::size_t>& { return m_kept_reasons; }

private:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>               mesh;
        std::size_t                                      depth{0};              // ancestor count: execute() runs parents first
        glm::mat4                                        world_from_node{1.0f}; // relative to the root node
        std::string                                      before_name;           // a name taken under World Space is renamed
        erhe::scene::Trs_transform                       before_parent_from_node;
        std::vector<erhe::scene::Mesh_primitive>         primitives_before;
        std::vector<erhe::scene::Mesh_primitive>         primitives_after;
        std::shared_ptr<erhe::scene::Node>               placeholder;           // null: no descendant stays in place
        std::shared_ptr<erhe::Hierarchy>                 execute_parent;        // the parent execute() took the mesh from
        std::size_t                                      execute_position{0};
    };

    void build_primitives();
    void assign_placeholders();

    Parameters                           m_parameters;
    bool                                 m_built{false};
    std::shared_ptr<erhe::scene::Node>   m_world_space;
    bool                                 m_world_space_created{false};
    std::vector<Entry>                   m_entries;
    std::map<std::string, std::size_t>   m_kept_reasons;
};

} // namespace editor
