#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace erhe::primitive { class Primitive_render_shape; }
namespace erhe::scene {
    class Mesh;
    class Xformable;
    using Node = Xformable;
}

namespace editor {

// The world-space form's state as the toolbar and MCP report it.
class World_space_form_status
{
public:
    bool        baked           {false}; // a bake installed the form
    std::size_t admitted_count  {0};     // mesh nodes the bake admitted
    std::size_t valid_count     {0};
    std::size_t missing_count   {0};     // admitted nodes whose world-space mesh is not valid
    bool        scene_changed   {false}; // a content mesh entered or left the scene since the bake
    bool        settings_changed{false}; // the export settings the keys read were edited since the bake

    // Every admitted node's world-space mesh is valid (R8.8).
    [[nodiscard]] auto is_available() const -> bool;
};

// What the bake keyed one admitted mesh node with.
class World_space_validity_entry
{
public:
    const erhe::scene::Mesh*                                    mesh{nullptr};
    glm::mat4                                                   world_from_node{1.0f}; // relative to the scene root
    std::vector<const erhe::primitive::Primitive_render_shape*> shapes;                // parallel to the mesh's primitives
    bool                                                        complete{false};       // every primitive has a World-space mesh item
};

// R8.6: whether each admitted node's World-space mesh item is still the one
// its current key names, kept by the Scene_host hooks (change-driven; doc/editor/world_space_bake.md "Validity").
class World_space_validity
{
public:
    // root_node: what the transforms are relative to; it outlives the entries.
    // settings_signature: the settings the bake keyed with (make_world_space_settings_signature(), world_space_form.hpp).
    void reset(const erhe::scene::Node* root_node, std::vector<World_space_validity_entry>&& entries, std::string settings_signature);
    void clear();

    // Scene_host hooks; any thread. A mesh that leaves and enters again (an
    // undo) is as before, and so is a setting set back (R3.3).
    void on_mesh_transform_changed (const erhe::scene::Mesh& mesh);
    void on_mesh_primitives_changed(const erhe::scene::Mesh& mesh);
    void on_mesh_registered        (const erhe::scene::Mesh& mesh);
    void on_mesh_unregistered      (const erhe::scene::Mesh& mesh);
    void on_export_settings_changed(std::string_view settings_signature);

    [[nodiscard]] auto get_status() const -> World_space_form_status;

private:
    class Entry
    {
    public:
        glm::mat4                                                   world_from_node{1.0f};
        std::vector<const erhe::primitive::Primitive_render_shape*> shapes;
        bool complete         {false};
        bool transform_matches{true};
        bool shapes_match     {true};
        bool registered       {true};
        [[nodiscard]] auto is_valid() const -> bool;
    };

    // Applies one change to the entry of the mesh, under m_mutex, keeping m_valid_count.
    template <typename F>
    void change_entry(const erhe::scene::Mesh& mesh, F&& change);
    [[nodiscard]] auto root_relative(const erhe::scene::Mesh& mesh) const -> glm::mat4;

    mutable std::mutex                                  m_mutex;
    std::unordered_map<const erhe::scene::Mesh*, Entry> m_entries;
    const erhe::scene::Node*                            m_root_node{nullptr};
    std::size_t                                         m_valid_count{0};
    bool                                                m_baked{false};
    // Content meshes registered since the bake that it did not key, and admitted ones unregistered since.
    std::unordered_set<const erhe::scene::Mesh*>        m_added_meshes;
    std::size_t                                         m_removed_count{0};
    std::string                                         m_settings_signature;
    bool                                                m_settings_changed{false};
};

}
