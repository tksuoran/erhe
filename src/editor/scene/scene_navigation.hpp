#pragma once

#include "navigation/navigation_build_settings.hpp"
#include "navigation/navigation_mesh.hpp"
#include "navigation/navigation_mesh_io.hpp"

#include "erhe_scene/transform_observer.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Scene_export_settings; // generated into the global namespace by erhe_codegen

namespace erhe::primitive { class Primitive_render_shape; }
namespace erhe::scene {
    class Mesh;
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class Scene_root;

// One input mesh of a build: what the build read of it (doc/editor/navigation.md "Input").
class Navigation_input_entry
{
public:
    // Identities that are never reused: the mesh's item id (Unique_id, monotonic) and owned shapes, so a
    // freed address taken by a new mesh or shape never compares as the built one.
    std::size_t                                                            mesh_id{0}; // 0: a saved input mesh the opened scene lacks
    std::string                                                            uid;        // the glTF uid, which names it in the saved item
    glm::mat4                                                              root_from_node{1.0f};
    std::vector<std::shared_ptr<const erhe::primitive::Primitive_render_shape>> shapes; // parallel to the mesh's primitives
    std::vector<Navigation_shape_hash>                                     shape_hashes; // parallel to shapes: their content hashes at the build
    bool                                                                   shapes_known{true}; // false: the opened shapes' hashes differ from the saved ones
};

// One seed of a build: its node and its point in the scene root's space.
class Navigation_seed
{
public:
    std::weak_ptr<erhe::scene::Node> node;
    std::size_t                      node_id{0}; // the node's item id: identity, never reused
    std::string                      name;
    std::string                      uid;
    glm::vec3                        point{0.0f};
};

class Navigation_build_statistics
{
public:
    std::size_t              tile_count        {0};
    std::size_t              skipped_mesh_count{0};
    std::vector<std::string> skipped_meshes;           // names of the input meshes that yielded no triangles
    std::size_t              input_mesh_count    {0};
    std::size_t              input_shape_count   {0}; // distinct primitive shapes: a shape several nodes share counts once
    std::size_t              input_triangle_count{0};
    double                   build_ms          {0.0};  // input triangles and tiles
    double                   flood_ms          {0.0};
};

// Why the installed navigation mesh no longer matches the scene (doc/editor/navigation.md "Staleness").
class Navigation_staleness
{
public:
    bool geometry_changed{false};
    bool settings_changed{false};
    bool seeds_changed   {false};

    [[nodiscard]] auto is_stale() const -> bool { return geometry_changed || settings_changed || seeds_changed; }
};

// The navigation mesh item last stored in, or read from, the editor asset (doc/editor/navigation.md "Saving").
class Navigation_item_state
{
public:
    std::string   key;          // 32 hex digits
    bool          made {false}; // false: the editor asset held this item already
    std::uint64_t bytes{0};
};

class Navigation_status
{
public:
    bool                        available  {false};
    Navigation_staleness        staleness;
    Navigation_totals           totals;
    Navigation_build_statistics statistics;
    std::size_t                 build_count{0}; // full builds installed since the scene opened
    std::size_t                 flood_count{0}; // floods installed, a build's included
    std::optional<Navigation_item_state> item;
};

// The flag bits of a mesh admit it as navigation input: content, visible (the effective visibility), no proxy, not session-only.
[[nodiscard]] auto is_navigation_input_flag_admitted(uint64_t flag_bits) -> bool;
// The mesh is navigation input (doc/editor/navigation.md "Input"); animated_nodes from collect_animated_nodes().
[[nodiscard]] auto is_navigation_input_mesh(const erhe::scene::Mesh& mesh, const std::unordered_set<const erhe::scene::Node*>& animated_nodes) -> bool;
// The build settings without the seed search extents, which a flood alone reads.
[[nodiscard]] auto are_navigation_build_settings_equal(const Navigation_build_settings& lhs, const Navigation_build_settings& rhs) -> bool;
// Same meshes, transforms (bit for bit) and render shapes, in any order; an entry with unknown shapes or no mesh equals none.
[[nodiscard]] auto are_navigation_inputs_equal(std::span<const Navigation_input_entry> lhs, std::span<const Navigation_input_entry> rhs) -> bool;
// Same nodes at the same points (bit for bit), in any order.
[[nodiscard]] auto are_navigation_seeds_equal(std::span<const Navigation_seed> lhs, std::span<const Navigation_seed> rhs) -> bool;

// The scene's navigation mesh and what it was built from (doc/editor/navigation.md "Scene state").
// The Scene_host hooks call it from any thread; install() and clear() run on the main thread.
class Scene_navigation
{
public:
    explicit Scene_navigation(Scene_root& scene_root);
    ~Scene_navigation() noexcept;
    Scene_navigation(const Scene_navigation&) = delete;
    auto operator=(const Scene_navigation&) -> Scene_navigation& = delete;

    // Replaces the mesh, everything built with it and its visualization node within one call.
    void install(
        std::shared_ptr<Navigation_mesh>      mesh,
        std::vector<Navigation_seed_result>&& seed_results,
        const Navigation_build_settings&      settings,
        std::vector<Navigation_seed>&&        seeds,
        std::vector<Navigation_input_entry>&& input,
        Navigation_build_statistics&&         statistics,
        std::shared_ptr<erhe::scene::Node>    visualization
    );
    // A flood of the installed mesh; false (nothing changed) when `mesh` is no longer the installed one.
    [[nodiscard]] auto install_flood(
        const std::shared_ptr<Navigation_mesh>& mesh,
        std::span<const uint8_t>                reachable,
        std::vector<Navigation_seed_result>&&   seed_results,
        const Navigation_build_settings&        settings,
        std::vector<Navigation_seed>&&          seeds,
        double                                  flood_ms,
        std::shared_ptr<erhe::scene::Node>      visualization
    ) -> bool;
    // A mesh read from the saved item at the open: input_meshes (parallel to input; null where the scene lacks the
    // saved mesh) and added_meshes (input meshes the build did not read) give the staleness against the opened scene.
    void install_loaded(
        std::shared_ptr<Navigation_mesh>                        mesh,
        std::vector<Navigation_seed_result>&&                   seed_results,
        const Navigation_build_settings&                        settings,
        std::vector<Navigation_seed>&&                          seeds,
        std::vector<Navigation_input_entry>&&                   input,
        std::span<const std::shared_ptr<erhe::scene::Mesh>>     input_meshes,
        std::unordered_set<std::size_t>&&                       added_meshes,
        Navigation_build_statistics&&                           statistics,
        const Navigation_item_state&                            item,
        std::shared_ptr<erhe::scene::Node>                      visualization
    );
    // Removes the mesh and its visualization node; main thread, outside the ImGui phase. The saved item stays.
    void clear();

    void set_item  (const Navigation_item_state& item);
    void clear_item();

    // Scene_host hooks of Scene_root (change-driven staleness).
    void on_mesh_transform_changed (const erhe::scene::Mesh& mesh);
    void on_mesh_primitives_changed(const erhe::scene::Mesh& mesh);
    void on_mesh_registered        (const erhe::scene::Mesh& mesh);
    void on_mesh_unregistered      (const erhe::scene::Mesh& mesh);
    // A change of the flag bits the input admission reads (visible, content, proxy, session-only) admits or drops the mesh.
    void on_mesh_flags_changed     (const erhe::scene::Mesh& mesh, uint64_t old_flag_bits, uint64_t new_flag_bits);
    // The scene's export settings changed (Scene_root::on_export_settings_changed()).
    void on_export_settings_changed(const std::optional<Scene_export_settings>& export_settings);
    // The scene's seed list changed (Scene_root::on_navigation_seed_changed(), unregister_node()).
    void on_seeds_changed();

    [[nodiscard]] auto get_mesh        () const -> std::shared_ptr<Navigation_mesh>;
    // A mesh is installed, not stale, and flooded with the seed search extents of `settings`.
    [[nodiscard]] auto is_current      (const Navigation_build_settings& settings) const -> bool;
    [[nodiscard]] auto get_visualization() const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_status      () const -> Navigation_status;
    [[nodiscard]] auto get_status_text () const -> std::string;
    [[nodiscard]] auto get_settings    () const -> Navigation_build_settings;
    void get_seeds       (std::vector<Navigation_seed>& out_seeds) const;
    void get_seed_results(std::vector<Navigation_seed_result>& out_results) const;
    void get_input       (std::vector<Navigation_input_entry>& out_input) const;
    [[nodiscard]] auto get_statistics() const -> Navigation_build_statistics;
    [[nodiscard]] auto get_item      () const -> std::optional<Navigation_item_state>;

    // The scene's seed nodes and their points now (Scene_root's list order).
    void collect_current_seeds(std::vector<Navigation_seed>& out_seeds) const;

private:
    class Input_state
    {
    public:
        glm::mat4                                                              root_from_node{1.0f};
        std::vector<std::shared_ptr<const erhe::primitive::Primitive_render_shape>> shapes;
        bool shapes_known     {true};
        bool transform_matches{true};
        bool shapes_match     {true};
        bool registered       {true};
        bool admitted         {true}; // the flag bits still admit it
        [[nodiscard]] auto is_valid() const -> bool { return transform_matches && shapes_match && registered && admitted; }
    };

    // Applies one change to the state of an input mesh, keeping m_invalid_count; m_mutex held.
    template <typename F>
    void change_input_locked(const erhe::scene::Mesh& mesh, F&& change);
    void update_geometry_changed_locked();
    [[nodiscard]] auto root_relative(const erhe::scene::Mesh& mesh) const -> glm::mat4;
    void observe_seeds_locked();
    void update_seeds_changed(std::span<const Navigation_seed> current);
    void update_status_text_locked();
    // Detaches the previous node and attaches `visualization` under the scene root; m_mutex not held (the mesh hooks take it).
    void replace_visualization(std::shared_ptr<erhe::scene::Node> visualization);

    Scene_root&                                                 m_scene_root;
    mutable std::mutex                                          m_mutex;
    std::shared_ptr<Navigation_mesh>                            m_mesh;
    std::shared_ptr<erhe::scene::Node>                          m_visualization; // "Navigation Mesh" (doc/editor/navigation.md "Visualization")
    std::vector<Navigation_seed_result>                         m_seed_results;
    Navigation_build_settings                                   m_settings;
    std::vector<Navigation_seed>                                m_seeds;
    std::vector<Navigation_input_entry>                         m_input;
    Navigation_build_statistics                                 m_statistics;
    std::unordered_map<std::size_t, Input_state>                m_input_states; // by mesh item id
    std::size_t                                                 m_invalid_count{0};
    std::size_t                                                 m_missing_input_count{0}; // saved input meshes the opened scene lacks
    std::unordered_set<std::size_t>                             m_added_meshes; // item ids of admitted meshes the build did not read
    std::vector<erhe::scene::Transform_observer_token>          m_seed_observers;
    Navigation_staleness                                        m_staleness;
    std::size_t                                                 m_build_count{0};
    std::size_t                                                 m_flood_count{0};
    std::string                                                 m_status_text;
    std::optional<Navigation_item_state>                        m_item;
};

}
