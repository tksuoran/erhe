#pragma once

#include "app_message.hpp"
#include "scene/generated/scene_settings.hpp"

#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene_renderer/light_set.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "scene/draw_list_scene_dependencies.hpp"
#include "scene/variant_table.hpp"

#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class btCollisionShape;
struct Physics_config; // erhe_codegen generated, global namespace

namespace erhe {
    class Item_base;
}
namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
    class Texture;
    class Vertex_format;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::physics {
    class IRigid_body;
    class IWorld;
    class Trigger_event;
}
namespace erhe::primitive {
    class Material;
    class Buffer_mesh;
    class Primitive;
}
namespace erhe::raytrace {
    class IScene;
}
namespace erhe::scene_renderer {
    class Draw_list_scene;
}
namespace erhe::scene {
    using Layer_id = uint64_t;
    class Camera;
    class Layout;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Mesh_primitive;
    class Mesh_raytrace;
    class Message_bus;
    class Xformable; using Node = Xformable;
    class Scene;
}

namespace editor {

class Content_library;
class App_context;
class App_message_bus;
class App_scenes;
class App_settings;
class Item_tree_window;
class Draw_mode_system;
class Node_joint;
class Physics_drag_constraint;
class Node_physics;
class Raytrace_primitive;
class Rendertarget_mesh;
class Scene_root;

// The file format a scene is bound to. It decides what Save Scene writes:
// `gltf` writes the erhe-authored glTF of doc/editor/scene_serialization.md, `usd`
// writes a USDA layer through erhe::usd, and `none` is a scene that has no
// file yet - it saves as glTF, the editor's default. A scene never converts
// between the two formats (doc/erhe/usd_compatibility_design.md G3).
enum class Scene_source_format : unsigned int {
    none = 0,
    gltf = 1,
    usd  = 2
};

[[nodiscard]] auto c_str(Scene_source_format format) -> const char*;

// One `DomeLight` prim a USD-backed scene was opened from authored. erhe has
// no environment map, so a dome is read as the scene's ambient light
// (erhe::scene::Scene::ambient_light) and the prim itself is kept here so a
// save spells it back as the `DomeLight` it was. The record is USD-only
// state: an erhe-authored scene carries its ambient light in the scene block
// and holds no dome (doc/erhe/usd.md).
class Usd_dome_light_record
{
public:
    std::string name;
    glm::vec3   color    {1.0f, 1.0f, 1.0f};
    float       intensity{1.0f};
    float       exposure {0.0f};
    std::string texture_file;
};
// The time coordinates a USD-backed scene was opened from authored
// (doc/erhe/usd.md, "Time samples"). A time code becomes seconds by
// dividing by `time_codes_per_second`; the `*_authored` flags say which of
// the three the file spelled, so a save writes back what the file had. The
// record is USD-only state, held the way the dome lights are: an
// erhe-authored scene keeps USD's own defaults.
class Usd_time_code_record
{
public:
    double time_codes_per_second{24.0};
    double start_time_code      {0.0};
    double end_time_code        {0.0};
    bool   time_codes_per_second_authored{false};
    bool   start_time_code_authored      {false};
    bool   end_time_code_authored        {false};
};

class Scene_view;
class Viewport_scene_view;

class Mesh_layer_id
{
public:
    static constexpr erhe::scene::Layer_id brush        = 0;
    static constexpr erhe::scene::Layer_id content      = 1;
    static constexpr erhe::scene::Layer_id sky          = 2;
    static constexpr erhe::scene::Layer_id controller   = 3;
    static constexpr erhe::scene::Layer_id tool         = 4;
    static constexpr erhe::scene::Layer_id rendertarget = 5;
    static constexpr erhe::scene::Layer_id bone         = 6;
};

class Scene_layers
{
public:
    Scene_layers();

    void add_layers_to_scene(erhe::scene::Scene& scene);

    [[nodiscard]] auto brush       () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto content     () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto controller  () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto tool        () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto rendertarget() const -> erhe::scene::Mesh_layer*;
    // Editor-generated bone pick/display proxies (see Item_flags::bone_proxy).
    // A separate layer so they can be rendered, id-rendered and raytraced as a
    // group without ever being mistaken for scene content.
    [[nodiscard]] auto bone        () const -> erhe::scene::Mesh_layer*;
    [[nodiscard]] auto light       () const -> erhe::scene::Light_layer*;
    [[nodiscard]] auto mesh_layers () const -> std::array<erhe::scene::Mesh_layer*, 6>;

private:
    std::shared_ptr<erhe::scene::Mesh_layer>  m_content;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_controller;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_tool;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_brush;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_rendertarget;
    std::shared_ptr<erhe::scene::Mesh_layer>  m_bone;
    std::shared_ptr<erhe::scene::Light_layer> m_light;
};

// One entry of the shape-to-meshes index (Scene_root::m_meshes_by_primitive):
// a mesh that names the entry's Primitive. The raw pointer is the identity the
// index removes by, so removal and expiry scans compare pointers instead of
// locking every weak reference; the weak reference is what the query hands
// out, and it keeps the index from holding a mesh of a closed scene alive.
class Mesh_sharer
{
public:
    const erhe::scene::Mesh*        mesh{nullptr};
    std::weak_ptr<erhe::scene::Mesh> weak_mesh;
};

class Scene_root
    : public std::enable_shared_from_this<Scene_root>
    , public erhe::scene::Scene_host
{
public:
    // draw_list_dependencies: non-null and valid -> this scene root owns a
    // Draw_list_scene and its content renders through persistent draw lists
    // (doc/erhe/draw_list_renderer.md); null -> no Draw_list_scene,
    // Forward_renderer / Shadow_renderer fallback for every pass (R1b).
    //
    // material_set_create_info: what this root's FORWARD Material_set is built
    // from (doc/erhe/draw_list_material_set.md D3, D4). Every root has one,
    // including roots with no draw list - it is the slot space the
    // Forward_renderer bucket path and the shadow bucket path resolve
    // materials through, and its object references come from this root's own
    // mesh hooks. A default-constructed one (no device) leaves the set
    // membership-only, which is what the deviceless configurations get.
    Scene_root(
        App_message_bus*                                     app_message_bus,
        const std::shared_ptr<Content_library>&              content_library,
        std::string_view                                     name,
        bool                                                 enable_physics,
        const Draw_list_scene_dependencies*                  draw_list_dependencies,
        const erhe::scene_renderer::Material_set_create_info& material_set_create_info
    );
    ~Scene_root() noexcept override;

    // Implements erhe::Item_host
    auto get_host_name() const -> const char* override;
    // Scene nodes and attachments (Scene_host) plus the content library's
    // materials (expression references, D22).
    auto find_hosted_item(std::string_view name_or_path) -> erhe::Item_base* override;

    // Public API
    auto make_browser_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        App_settings&                app_settings
    ) -> std::shared_ptr<Item_tree_window>;
    void remove_browser_window();

    void register_to_editor_scenes    (App_scenes& app_scenes);
    void unregister_from_editor_scenes(App_scenes& app_scenes);
    // The one way to close a scene: queues the Close_scene_message that
    // Editor::on_close_scene tears the scene down from (the teardown destroys
    // ImGui windows, so it runs from the message bus pump on a following
    // frame, outside ImGui iteration). Returns false, queuing nothing, when a
    // close is already pending or the scene is not registered - a second
    // request would otherwise reach the teardown a second time after the
    // scene has already been unregistered.
    [[nodiscard]] auto request_close(App_message_bus& app_message_bus) -> bool;
    [[nodiscard]] auto is_close_requested() const -> bool { return m_close_requested; }
    // Clears this scene_root's registration state without touching the
    // App_scenes registry. Called by ~App_scenes while it tears down its
    // own list, so that the later ~Scene_root does not try to unregister
    // from a registry that has already released it.
    void detach_from_editor_scenes    (App_scenes& app_scenes);

    // Every `Typed` prim of this scene's tree that is not an `Xformable` (a
    // `Scope`, a resource prim): forwarded to the content library, which keeps
    // the scene's resource index (doc/erhe/usd_compatibility_design.md U4).
    void register_prim    (const std::shared_ptr<erhe::Typed>&         prim)   override;
    void unregister_prim  (const std::shared_ptr<erhe::Typed>&         prim)   override;

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override;
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>& camera) override;
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&   mesh)   override;
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&   skin)   override;
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&   skin)   override;
    void register_light   (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&  light)  override;
    void register_layout  (const std::shared_ptr<erhe::scene::Layout>& layout) override;
    void unregister_layout(const std::shared_ptr<erhe::scene::Layout>& layout) override;
    void on_mesh_primitives_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh) override;
    void on_mesh_material_changed  (const std::shared_ptr<erhe::scene::Mesh>& mesh) override;
    void on_mesh_flags_changed     (const std::shared_ptr<erhe::scene::Mesh>& mesh, uint64_t old_flag_bits, uint64_t new_flag_bits) override;
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>& mesh) override;
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>& mesh) override;
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>& mesh) override;
    void on_light_changed          (const std::shared_ptr<erhe::scene::Light>& light) override;

    // The registered meshes of this scene that reference any Primitive of
    // mesh_primitives, read from the shape-to-meshes index: out_meshes is
    // cleared and filled with `mesh` first (whether or not it is registered
    // here), then every other registered mesh naming one of those
    // primitives, each mesh once. The caller owns out_meshes and keeps its
    // capacity between calls. Callers hold item_host_mutex.
    //
    // Shapes are shared - glTF instances, brush instances and prefab clones
    // hold the same Primitive - so a shape-level swap has to refresh every
    // mesh naming the swapped primitive, not only the one that built it.
    void collect_meshes_sharing_primitives(
        const std::shared_ptr<erhe::scene::Mesh>&        mesh,
        const std::vector<erhe::scene::Mesh_primitive>&  mesh_primitives,
        std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes
    );

    // The meshes whose Gprim.display_color changed since the last call, moved
    // out of this scene root. App_scenes::rebuild_display_colors() is what
    // rebuilds them: the rebuild needs Mesh_memory, which the scene root does
    // not have, and the property may be written from a worker thread.
    void take_display_color_meshes(std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes);

    // The scene's resolved light set (which lights are shaded / shadow-mapped,
    // in light UBO slot order). Invalidated by the light hooks (register /
    // unregister / on_light_changed); renderers call
    // get_light_set().resolve(layers().light()->lights, limits) before use,
    // which recomputes only when invalidated or the limits changed.
    [[nodiscard]] auto get_light_set() -> erhe::scene_renderer::Light_set&;

    // This root's FORWARD material slot space (D0): the one the bucket path
    // binds. A root that also has a Draw_list_scene has a SECOND, independent
    // set inside that object - ask it, not this - and the same Material
    // normally holds a different slot in each. Each call site picks by the
    // path it is on, and that choice stays visible where it is made.
    [[nodiscard]] auto get_material_set()       -> erhe::scene_renderer::Material_set&;
    [[nodiscard]] auto get_material_set() const -> const erhe::scene_renderer::Material_set&;

    // Enqueue this mesh's current material list (or its release) into the
    // forward set. Called from the mesh hooks, which the Scene_host contract
    // allows to run on a worker thread, so both enqueue rather than apply.
    void enqueue_mesh_materials        (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void enqueue_release_mesh_materials(const std::shared_ptr<erhe::scene::Mesh>& mesh);

    // Draw lists (doc/erhe/draw_list_renderer.md). get_draw_list_scene()
    // is null for scene roots constructed without dependencies.
    [[nodiscard]] auto get_draw_list_scene() -> erhe::scene_renderer::Draw_list_scene*;
    // Main thread, once per frame before any rendering of this scene:
    // applies queued register / unregister / flag changes under
    // item_host_mutex. No-op without a Draw_list_scene.
    void flush_draw_lists();
    auto get_hosted_scene () -> erhe::scene::Scene* override;

    void begin_mesh_rt_update(const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void end_mesh_rt_update  (const std::shared_ptr<erhe::scene::Mesh>& mesh);

    // The runtime state of this scene's draw modes: the nodes carrying one,
    // their cached extent and their card proxies
    // (doc/erhe/usd_compatibility.md, "Draw modes"). The scene drives it from
    // the node-system change sites, so no pass scans the tree for them.
    [[nodiscard]] auto get_draw_mode_system() -> Draw_mode_system&;

    // The card images the draw-mode proxies of this scene read, keyed by the
    // file each was read from, so two attachments naming the same file share
    // one texture. Weakly held: the proxy materials own the textures, the
    // cache only finds them, and it dies with the scene.
    [[nodiscard]] auto find_card_texture(const std::string& path) const -> std::shared_ptr<erhe::graphics::Texture>;
    void add_card_texture(const std::string& path, const std::shared_ptr<erhe::graphics::Texture>& texture);

    void register_node_physics  (const std::shared_ptr<Node_physics>& node_physics);
    void unregister_node_physics(const std::shared_ptr<Node_physics>& node_physics);

    // Node_joint bookkeeping. All attached joints stay registered; a joint
    // without a live constraint is pending. register_node_physics() retries
    // pending joints after adding the new rigid body to the world, and
    // unregister_node_physics() tears down constraints referencing the
    // departing body (returning those joints to the pending state).
    void register_node_joint    (const std::shared_ptr<Node_joint>& node_joint);
    void unregister_node_joint  (const std::shared_ptr<Node_joint>& node_joint);
    // True when a live Node_joint constraint of this scene references
    // rigid_body. Asked at events (selection, drag start), never per frame:
    // a selected jointed dynamic body stays dynamic, and a Transform tool drag
    // pulls it through physics instead of writing its node transform.
    [[nodiscard]] auto is_jointed_rigid_body(const erhe::physics::IRigid_body* rigid_body) const -> bool;
    // Every registered Node_joint, live or pending (read at drag start to
    // build the joint-space projection of a physics drag).
    [[nodiscard]] auto get_node_joints() const -> const std::vector<std::shared_ptr<Node_joint>>&;
    // Attached interactive physics drags: update_physics_simulation_fixed_step()
    // calls Physics_drag_constraint::on_fixed_step() on each before stepping
    // the world. attach() registers, detach() unregisters.
    void register_physics_drag  (Physics_drag_constraint* drag);
    void unregister_physics_drag(Physics_drag_constraint* drag);

    void before_physics_simulation_steps     ();
    void update_physics_simulation_fixed_step(double dt, const Physics_config& physics);
    void after_physics_simulation_steps      ();

    // Item_flags::no_transform_update means "the physics simulation currently
    // drives this node's world transform". That is only true while this
    // scene's simulation is stepping: pausing the simulation leaves awake
    // bodies active forever (no deactivation events fire), which used to
    // strand the flag on their nodes so hierarchy edits no longer propagated
    // to them. App_scenes calls this every frame with the scene's resolved
    // physics gate; edges clear the flag from every body-driven node (pause)
    // or restore it on awake dynamic bodies (resume).
    void set_physics_simulation_running(bool running);

    [[nodiscard]] auto layers            () -> Scene_layers&;
    [[nodiscard]] auto layers            () const -> const Scene_layers&;
    [[nodiscard]] auto has_physics_world () const -> bool;
    [[nodiscard]] auto get_physics_world () -> erhe::physics::IWorld&;

    // Bounded log of recent sensor (trigger) overlap events, appended by the
    // physics world trigger callbacks at the end of update_fixed_step() and
    // shown in the Physics window. Lines are preformatted at event time;
    // the counter keeps counting after old lines fall out of the log.
    [[nodiscard]] auto get_trigger_event_log  () const -> const std::deque<std::string>&;
    [[nodiscard]] auto get_trigger_event_count() const -> uint64_t;
    void clear_trigger_event_log();
    [[nodiscard]] auto get_raytrace_scene() -> erhe::raytrace::IScene&;
    [[nodiscard]] auto get_scene         () -> erhe::scene::Scene&;
    [[nodiscard]] auto get_scene         () const -> const erhe::scene::Scene&;
    // Stable shared_ptr to the Scene item, used to make the Scene selectable and
    // to show it as the top row of the Hierarchy window (issue #240).
    [[nodiscard]] auto get_scene_item    () -> std::shared_ptr<erhe::scene::Scene>;
    [[nodiscard]] auto get_name          () const -> const std::string&;

    // Canonical path of the glTF file the scene was opened/loaded from
    // (Scene_open_operation, open_scene_gltf), or last saved to; empty for
    // scenes not yet associated with a file. Save Scene writes back here
    // without confirmation and reloads every prefab instance when the file
    // is a loaded prefab source.
    [[nodiscard]] auto get_source_path   () const -> const std::filesystem::path&;
    // The format is given with the path because the two always change
    // together: whatever opened or saved the scene knows both.
    void set_source_path(const std::filesystem::path& path, Scene_source_format format);
    [[nodiscard]] auto get_source_format () const -> Scene_source_format;

    // The `DomeLight` prims the opened USD file authored, in file order (see
    // Usd_dome_light_record). Empty for every scene that was not opened from
    // a USD file holding one.
    [[nodiscard]] auto get_usd_dome_lights() const -> const std::vector<Usd_dome_light_record>&;
    void set_usd_dome_lights(std::vector<Usd_dome_light_record>&& dome_lights);

    // The asset paths of the `subLayers` the opened USD file's root layer
    // listed, strongest first. Their content was composed into the scene, and
    // a save writes it back as one layer, so this is only what the save log
    // names. Empty for every scene that was not opened from a sublayered USD
    // file.
    [[nodiscard]] auto get_usd_sublayers() const -> const std::vector<std::string>&;
    void set_usd_sublayers(std::vector<std::string>&& sublayers);

    // The time coordinates the opened USD file authored (see
    // Usd_time_code_record): what turns an xformOp time sample into seconds,
    // and what a save spells back so the file keeps its own rate.
    [[nodiscard]] auto get_usd_time_codes() const -> const Usd_time_code_record&;
    void set_usd_time_codes(const Usd_time_code_record& time_codes);

    // Definition-vs-reference classification for an asset-typed item
    // entering this scene's content library (asset-manager plan, R5
    // sub-plan resolution 2): true = definition (owning entry), false =
    // reference entry (owned elsewhere). Pre-flip the decision derives
    // from item hosting; the R5.6 single-loader flip re-implements
    // exactly this predicate on the Asset_manager (defining container ==
    // this scene's record). Deliberately the ONE site that changes at
    // the flip - do not inline host comparisons for this purpose.
    [[nodiscard]] auto is_asset_definition(const erhe::Item_base& item) const -> bool;

    // Per-scene setting overrides (issue #239). Each field is an optional; a
    // disengaged optional means "use the editor-global default". Effective values
    // are resolved by the helpers in scene/scene_settings_resolve.hpp.
    [[nodiscard]] auto get_scene_settings()       -> Scene_settings&;
    [[nodiscard]] auto get_scene_settings() const -> const Scene_settings&;

    // The variant sets this scene's prims carry
    // (doc/erhe/usd_compatibility_design.md X4), filled by the USD parser when the
    // scene is opened or an asset is imported and dying with this scene root.
    [[nodiscard]] auto get_variant_table()       -> Variant_table&;
    [[nodiscard]] auto get_variant_table() const -> const Variant_table&;

    // Switches one variant set of this scene: the selection is recorded in
    // Scene_settings::variant_selections and the chosen variant's material
    // bindings are assigned, all as one undoable compound. Returns an error
    // text, empty on success. `mode` decides whether the switch goes on the
    // undo stack (a user switch) or is applied at once (a scene being opened
    // restoring the selection its file carried).
    enum class Variant_switch_mode : unsigned int {
        undoable  = 0,
        immediate = 1
    };
    auto select_variant(
        App_context&           context,
        const Variant_set_key& key,
        const std::string&     variant_name,
        Variant_switch_mode    mode
    ) -> std::string;

    // Applies every Scene_settings::variant_selections entry that names a set
    // of the table whose selection differs, without touching the undo stack:
    // what a scene being opened does once its variant table is filled. A set a
    // variant block declares is applied after the set carrying that block, so
    // the enclosing selection is standing when the inner one is applied
    // (doc/plans/usd_compatibility.md, "Variant opinions a variant set does
    // not carry").
    void apply_variant_selections(App_context& context);

    // Persistent scene identity (Scene_settings::scene_id, saved with the
    // scene): creation timestamp + random suffix, generated lazily here for
    // scenes that lack one (from-scratch scenes and pre-scene_id files
    // alike - no back-compat kept). Side data (the lightmap tile set
    // manifest) is stamped with it and rejected on mismatch, so a scratch
    // scene never adopts another scene's untitled.lightmap set.
    [[nodiscard]] auto get_scene_id() -> const std::string&;

    void imgui();

    auto camera_combo(const char* label, erhe::scene::Camera*& camera, bool nullptr_option = false) const -> bool;
    auto camera_combo(const char* label, std::shared_ptr<erhe::scene::Camera>& selected_camera, bool nullptr_option = false) const -> bool;
    auto camera_combo(const char* label, std::weak_ptr<erhe::scene::Camera>& selected_camera, bool nullptr_option = false) const -> bool;

    [[nodiscard]] auto get_content_library() const -> std::shared_ptr<Content_library>;

    void update_pointer_for_rendertarget_meshes(Scene_view* scene_view);
    void sanity_check();

private:
    void add_trigger_event(bool enter, const erhe::physics::Trigger_event& event);

    // Returns the raytrace IInstance mask for a mesh: the role bits of the
    // mesh's own flags and of its attachments. Skinned meshes get the
    // Raytrace_node_mask::skinned bit in lieu of the role bits, so
    // picking-tool rays (which use role bits) skip them and the ID renderer
    // handles them instead. See Raytrace_node_mask::skinned.
    [[nodiscard]] auto get_mesh_rt_mask(erhe::scene::Mesh* mesh) -> uint32_t;

    // Shape-to-meshes index (doc/erhe/usd_compatibility_design.md, "A load of a
    // stage holding thousands of prims"). For every
    // Primitive a registered mesh of this scene names, the meshes that name
    // it; m_primitives_by_mesh is the reverse list that makes removal exact,
    // so a mesh whose primitive list was replaced leaves the entries of the
    // primitives it USED to name. Maintained only at the three change sites -
    // register_mesh, unregister_mesh and on_mesh_primitives_changed - and
    // never scanned or refreshed per frame.
    //
    // Own mutex rather than item_host_mutex: register_mesh and the
    // primitives-changed hook run on tf::Executor workers during an async
    // load, which item_host_mutex does not cover at every one of those call
    // sites. It is only ever taken as the inner lock (the raytrace commit
    // queries while holding item_host_mutex), never the other way around.
    //
    // The sharer entries are weak so the index never keeps a mesh of a closed
    // scene alive; an expired entry is dropped whenever it is met.
    void index_mesh_primitives   (const std::shared_ptr<erhe::scene::Mesh>& mesh);
    void unindex_mesh_primitives (const erhe::scene::Mesh* mesh);
    void remove_mesh_from_primitive_index(const erhe::scene::Mesh* mesh); // m_mesh_primitive_index_mutex held
    ERHE_PROFILE_MUTEX(std::mutex, m_mesh_primitive_index_mutex);
    std::unordered_map<
        const erhe::primitive::Primitive*,
        std::vector<Mesh_sharer>
    > m_meshes_by_primitive;
    std::unordered_map<
        const erhe::scene::Mesh*,
        std::vector<const erhe::primitive::Primitive*>
    > m_primitives_by_mesh;
    // The primitive list being indexed, cleared at point of use and after
    // use; guarded by m_mesh_primitive_index_mutex like the two maps. A
    // member rather than a local because index_mesh_primitives runs once per
    // mesh refresh of every raytrace commit of a load.
    std::vector<const erhe::primitive::Primitive*> m_index_scratch;

    erhe::message_bus::Subscription<Selection_message>     m_selection_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    void on_items_removed(const Removed_items& removed);

    // Live longest
    mutable ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    ERHE_PROFILE_MUTEX        (std::mutex, m_rendertarget_meshes_mutex);
    ERHE_PROFILE_MUTEX        (std::mutex, m_display_color_meshes_mutex);
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_display_color_meshes;

    // Publisher for Skin_registered_message; nullptr for scenes that do not
    // take part in editor messaging (previews, the tool scene).
    App_message_bus*                                m_app_message_bus{nullptr};
    App_scenes*                                     m_app_scenes{nullptr};
    std::shared_ptr<Content_library>                m_content_library;
    std::filesystem::path                           m_source_path;
    Scene_source_format                             m_source_format{Scene_source_format::none};
    std::vector<Usd_dome_light_record>              m_usd_dome_lights;
    std::vector<std::string>                        m_usd_sublayers;
    Usd_time_code_record                            m_usd_time_codes;
    bool                                            m_is_registered{false};
    bool                                            m_close_requested{false};

    // Applies wind forces to wind-receptive dynamic bodies; called once per
    // fixed step from update_physics_simulation_fixed_step() before the world
    // steps (Jolt clears accumulated forces after every step).
    void apply_wind_forces(float dt, const Physics_config& physics);

    // Must live longer than m_scene for example
    bool                                            m_node_physics_sorted{false};
    bool                                            m_physics_simulation_running{true};
    double                                          m_wind_time{0.0};
    std::vector<std::shared_ptr<Node_physics>>      m_node_physics;
    std::unique_ptr<Draw_mode_system>               m_draw_mode_system;
    std::unordered_map<std::string, std::weak_ptr<erhe::graphics::Texture>> m_card_textures;
    std::vector<std::shared_ptr<Node_joint>>        m_node_joints;
    std::vector<Physics_drag_constraint*>           m_physics_drags;
    std::vector<std::shared_ptr<Rendertarget_mesh>> m_rendertarget_meshes;

    std::vector<std::shared_ptr<erhe::Item_base>>   m_physics_disabled_nodes;

    std::unique_ptr<erhe::physics::IWorld>          m_physics_world;
    std::unique_ptr<erhe::raytrace::IScene>         m_raytrace_scene;
    // Declared after m_raytrace_scene: the draw list scene keeps registered
    // meshes alive, and ~Mesh may detach from m_raytrace_scene, so it must be
    // destroyed first (also reset explicitly at the top of ~Scene_root).
    std::unique_ptr<erhe::scene_renderer::Draw_list_scene> m_draw_list_scene;
    // The FORWARD set (D0). Destroyed before the draw list scene, so the
    // strong material references it holds are dropped while the meshes that
    // named them are still alive.
    erhe::scene_renderer::Material_set                     m_material_set;
    erhe::scene_renderer::Light_set                        m_light_set;

    static constexpr std::size_t s_max_trigger_event_log_entries = 100;
    std::deque<std::string>                         m_trigger_event_log;
    uint64_t                                        m_trigger_event_counter{0};

    std::shared_ptr<erhe::scene::Scene>             m_scene;
    Scene_layers                                    m_layers;
    Scene_settings                                  m_scene_settings;
    Variant_table                                   m_variant_table;

    std::shared_ptr<Item_tree_window>               m_node_tree_window;
};

// Cameras offered for user camera selection (the "Scene and Camera" dialog
// combos, default camera picks for new viewport views, persisted-selection
// restore): the scene's own cameras, including cameras brought in by a glTF
// import (they sit under the Item_flags::import_root wrapper as ordinary
// scene content). Cameras inside a prefab instance are skipped, so a scene
// full of instanced assets does not offer every instance's cameras. When the
// scene has no other camera at all, all cameras are offered instead so such
// scenes remain viewable.
[[nodiscard]] auto get_selectable_cameras(const erhe::scene::Scene& scene) -> std::vector<std::shared_ptr<erhe::scene::Camera>>;

// Resolves the Scene_root hosting the given item: a content-library item's
// Item_host is its owning scene (scene items resolve through their scene the
// same way). Returns null when the item is not hosted by a scene - not in
// any library, or a shared prefab template resource (reference entries are
// deliberately non-hosted).
[[nodiscard]] auto get_hosting_scene_root(const erhe::Item_base* item) -> std::shared_ptr<Scene_root>;

}
