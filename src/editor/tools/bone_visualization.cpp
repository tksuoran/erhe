#include "tools/bone_visualization.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "app_settings.hpp"
#include "rig/bone_tail.hpp"
#include "scene/node_raytrace_mask.hpp"
#include "scene/rig_properties.hpp"
#include "scene/scene_root.hpp"
#include "tools/mesh_component_selection.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace editor {

namespace {

// Fraction of the bone length at which the octahedron's widest ring sits.
// Matches the long-standing line visualization (mid_point = mix(a, b, 0.1)).
constexpr float c_ring_position = 0.1f;

// Smallest half-width a bone proxy may have, in world units. A zero-length or
// hairline bone would otherwise collapse to a degenerate shape that cannot be
// clicked; picking has to stay possible for every joint. The proxy's scale is
// expressed in the joint's local space, so set_proxy_transform divides this by
// the joint's world scale: a skeleton under a 100x parent (CarbonFrameBike's
// armatures) has bones 0.0002 units long locally, and a local-space clamp of
// 0.002 made every one of them ten times wider than long.
constexpr float c_min_half_width = 0.002f;

// Unit bone: head at the origin, tail at +Y, square ring at y = c_ring_position
// with half-width 1 in x and z. The instance transform scales x/z by the bone
// half-width and y by the bone length, so this one geometry serves every bone -
// which is exactly what lets the raytrace side keep a single BVH and pose it per
// instance.
void make_bone(GEO::Mesh& mesh)
{
    mesh.vertices.set_double_precision();
    {
        const GEO::vec3 vertices[] = {
            { 0.0, 0.0,             0.0}, // 0 head
            { 0.0, 1.0,             0.0}, // 1 tail
            { 1.0, c_ring_position, 0.0}, // 2 +x
            { 0.0, c_ring_position, 1.0}, // 3 +z
            {-1.0, c_ring_position, 0.0}, // 4 -x
            { 0.0, c_ring_position,-1.0}  // 5 -z
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex  = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }
    {
        // Counter-clockwise seen from outside: 4 facets from the head down to
        // the ring, 4 from the ring up to the tail. Check against the head
        // facet {0, 2, 3}: (v2 - v0) x (v3 - v0) = (0.1, -1, 0.1), pointing out
        // of and below the head cone in the +x +z quadrant, which is outward.
        // The reverse order normals the whole shape inward - it renders inside
        // out, and the raytrace hover normal comes back negated with it.
        const std::array<GEO::index_t, 3> facets[] = {
            { 0, 2, 3 }, { 0, 3, 4 }, { 0, 4, 5 }, { 0, 5, 2 },
            { 1, 3, 2 }, { 1, 4, 3 }, { 1, 5, 4 }, { 1, 2, 5 }
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet  = mesh.facets.create_facets(facet_count, 3);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 3; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();
    // The builder reads points through get_pointf(); leaving the mesh in the
    // double precision used above trips a geogram assertion.
    mesh.vertices.set_single_precision();
}

// Unit prism of the stick and box shapes: x and z in [-1, 1], y from 0 (head)
// to 1 (tail); scaled like the octahedron by the instance transform.
void make_box(GEO::Mesh& mesh)
{
    mesh.vertices.set_double_precision();
    {
        const GEO::vec3 vertices[] = {
            {-1.0, 0.0, -1.0}, // 0
            { 1.0, 0.0, -1.0}, // 1
            { 1.0, 0.0,  1.0}, // 2
            {-1.0, 0.0,  1.0}, // 3
            {-1.0, 1.0, -1.0}, // 4
            { 1.0, 1.0, -1.0}, // 5
            { 1.0, 1.0,  1.0}, // 6
            {-1.0, 1.0,  1.0}  // 7
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex  = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }
    {
        // Counter-clockwise seen from outside, two triangles per side; e.g.
        // the bottom {0, 1, 2}: (v1 - v0) x (v2 - v0) = (0, -4, 0), outward.
        const std::array<GEO::index_t, 3> facets[] = {
            { 0, 1, 2 }, { 0, 2, 3 }, // -y (head end)
            { 4, 6, 5 }, { 4, 7, 6 }, // +y (tail end)
            { 3, 2, 6 }, { 3, 6, 7 }, // +z
            { 0, 5, 1 }, { 0, 4, 5 }, // -z
            { 1, 5, 6 }, { 1, 6, 2 }, // +x
            { 0, 3, 7 }, { 0, 7, 4 }  // -x
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet  = mesh.facets.create_facets(facet_count, 3);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 3; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();
    mesh.vertices.set_single_precision();
}

// Half-width of a bone shape as a fraction of the octahedron's (the style's
// aspect ratio times the bone length): the stick is a quarter as wide.
[[nodiscard]] auto get_shape_width_factor(const Bone_display_shape shape) -> float
{
    return (shape == Bone_display_shape::stick) ? 0.25f : 1.0f;
}

// The shared primitive of one unit shape: render and raytrace geometry built
// by `build`, facet normals for the N.V shading.
[[nodiscard]] auto make_shape_primitive(
    void                               (*build)(GEO::Mesh&),
    erhe::scene_renderer::Mesh_memory& mesh_memory
) -> std::shared_ptr<erhe::primitive::Primitive>
{
    auto render_geometry   = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    build(render_geometry->get_mesh());
    build(raytrace_geometry->get_mesh());
    // A hand-built GEO::Mesh carries no normal attribute, and the primitive
    // builder writes vertex normals from facet_normal. The solid bone style
    // shades with Shader_debug::vdotn_tinted - dot(V, N) times the material
    // color - so without this the bones would come out flat black.
    {
        erhe::geometry::Mesh_attributes attributes{render_geometry->get_mesh()};
        erhe::geometry::compute_facet_normals(render_geometry->get_mesh(), attributes);
    }

    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(render_geometry, raytrace_geometry);
    const bool render_ok = primitive->make_renderable_mesh(
        erhe::primitive::Build_info{
            .primitive_types{ .fill_triangles = true },
            .buffer_info = mesh_memory.make_primitive_buffer_info()
        },
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(render_ok);
    const bool raytrace_ok = primitive->make_raytrace();
    ERHE_VERIFY(raytrace_ok);
    return primitive;
}

// Rotation taking +Y onto `direction` (unit). Uses an arbitrary perpendicular
// when the two are antiparallel, where the axis is undefined.
[[nodiscard]] auto orient_y_to(const glm::vec3 direction) -> glm::mat4
{
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const float     d = glm::dot(up, direction);
    if (d > 0.9999f) {
        return glm::mat4{1.0f};
    }
    if (d < -0.9999f) {
        return glm::rotate(glm::mat4{1.0f}, glm::pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    }
    const glm::vec3 axis = glm::normalize(glm::cross(up, direction));
    return glm::rotate(glm::mat4{1.0f}, std::acos(d), axis);
}

} // anonymous namespace

Bone_visualization::Bone_visualization(App_context& context, App_message_bus& app_message_bus, erhe::scene_renderer::Mesh_memory& mesh_memory)
    : m_context    {context}
    , m_mesh_memory{mesh_memory}
{
    // No initial style / mode read here: this runs inside the init taskflow,
    // before App_context is filled. The style is read at first use
    // (ensure_primitive), the mode starts at its Mesh_component_selection
    // default (object) and every later change arrives as a message. No scene
    // sweep either: no scene exists yet, and skin messages queued before the
    // first pump are delivered to these subscriptions regardless of order.
    m_skin_registered_subscription = app_message_bus.skin_registered.subscribe(
        [this](Skin_registered_message& message) { on_skin_registered(message); }
    );
    m_bone_changed_subscription = app_message_bus.bone_changed.subscribe(
        [this](Bone_changed_message& message) { on_bone_changed(message); }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) { on_close_scene(message); }
    );
    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message& message) { on_selection(message); }
    );
    m_node_touched_subscription = app_message_bus.node_touched.subscribe(
        [this](Node_touched_message& message) { on_node_touched(message.node); }
    );
    m_animation_update_subscription = app_message_bus.animation_update.subscribe(
        [this](Animation_update_message&) { on_animation_update(); }
    );
    m_mode_changed_subscription = app_message_bus.mesh_component_mode_changed.subscribe(
        [this](Mesh_component_mode_changed_message&) { on_mode_changed(); }
    );
}

Bone_visualization::~Bone_visualization() noexcept = default;


void Bone_visualization::ensure_primitive()
{
    if (m_bone_primitive) {
        return;
    }

    // First real work also snaps up the initial style values (the constructor
    // runs before App_context is filled and cannot). Later changes arrive via
    // apply_style_shape(), called from the settings UI at the edit.
    {
        const Debug_visualizations_style& style = m_context.editor_settings->debug_visualizations_style;
        m_aspect_ratio = style.bone_aspect_ratio;
        m_solid        = style.bone_solid;
    }

    m_bone_primitive = make_shape_primitive(make_bone, m_mesh_memory);
    m_box_primitive  = make_shape_primitive(make_box,  m_mesh_memory);

    // The material of a bone in the style colors: white, so the solid pass's
    // tinted N.V (Shader_debug::vdotn_tinted) is the plain N.V grey.
    m_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "bone",
            .values = {
                .base_color = glm::vec3{1.0f, 1.0f, 1.0f},
                .bxdf_model = erhe::primitive::Bxdf_model::unlit
            }
        }
    );
    // Selected bones read as a different color in the solid style. The solid
    // pass's N.V override skips selected and hovered proxies (its filter), so
    // they draw flat in this material's color.
    m_selected_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "bone selected",
            .values = {
                .bxdf_model = erhe::primitive::Bxdf_model::unlit
            }
        }
    );
    // The bone under the pointer, same idea as the selected material.
    m_hover_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "bone hover",
            .values = {
                .bxdf_model = erhe::primitive::Bxdf_model::unlit
            }
        }
    );
    apply_style_colors();

    // Builtin-scope assets ({builtin, material, <name>}): the three materials
    // are editor-owned and outlive every scene on purpose (they are shared by
    // the proxies of all scenes), and as builtins they are "intentionally
    // pinned by the asset manager" for the scene-close leak watchdog.
    //
    // No scene's content library lists them: the index lists what a scene
    // OWNS (content_library.hpp), and a material a mesh of the scene binds
    // reaches the render through the material set's own per-object
    // membership, which the mesh hooks feed
    // (Scene_root::enqueue_mesh_materials) - the same way the material
    // preview's inspected material does. Listing them made every save of a
    // skinned scene write three `bone` Material prims into the file, which a
    // reload then owned alongside these, duplicating them once per round
    // trip.
    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->register_builtin(Asset_type::material, m_material);
        m_context.asset_manager->register_builtin(Asset_type::material, m_selected_material);
        m_context.asset_manager->register_builtin(Asset_type::material, m_hover_material);
    }
}

void Bone_visualization::apply_style_colors()
{
    if (!m_material || !m_selected_material || !m_hover_material) {
        return;
    }
    const Debug_visualizations_style& style = m_context.editor_settings->debug_visualizations_style;
    m_selected_material->set_base_color(glm::vec3{style.bone_selected_color});
    m_hover_material   ->set_base_color(glm::vec3{style.bone_hover_color});
}

auto Bone_visualization::make_proxy(const std::shared_ptr<erhe::scene::Node>& joint) -> Proxy
{
    Proxy proxy;
    proxy.joint = joint;
    // Named after the joint the bone stands for: the proxy mesh's name is what
    // every hover / pick readout displays, and "bone proxy" told the user
    // nothing about which bone they were pointing at. The node keeps the
    // "bone proxy" prefix so the two stay distinguishable in scene dumps.
    // A joint renamed later keeps the old name here until the proxies are
    // rebuilt; the Hover tool's Bone: line reads the joint's live name.
    const std::string& joint_name = joint->get_name();
    proxy.node  = std::make_shared<erhe::scene::Xform>(fmt::format("bone proxy {}", joint_name));
    proxy.mesh  = std::make_shared<erhe::scene::Mesh>(joint_name);
    proxy.shape = joint->get_value(Rig::display_shape_property());
    proxy.mesh->add_primitive(get_shape_primitive(proxy.shape), get_display_material(*joint));
    proxy.mesh->layer_id = Mesh_layer_id::bone;

    // bone_proxy is what keeps this out of the item tree, save, export and
    // prefabs; id is added/removed by apply_proxy_flags() to gate picking.
    // Deliberately no `content` bit - a proxy must never be mistaken for scene
    // content.
    proxy.mesh->enable_flag_bits(erhe::Item_flags::bone_proxy);
    proxy.node->enable_flag_bits(erhe::Item_flags::bone_proxy);

    erhe::scene::set_mesh_parent(proxy.mesh, proxy.node);
    proxy.node->set_parent(joint);
    return proxy;
}

void Bone_visualization::set_proxy_transform(Proxy& proxy, const glm::vec3 tail_local)
{
    const float length = glm::length(tail_local);

    // c_min_half_width is a world-space floor; the proxy is scaled in the
    // joint's local space, so the floor is brought into that space through
    // the joint's world scale (the length of one axis of world_from_node).
    const std::shared_ptr<erhe::scene::Node> joint       = proxy.joint.lock();
    const float                              world_scale = joint ? glm::length(glm::vec3{joint->world_from_node()[0]}) : 1.0f;
    const float                              min_local   = (world_scale > 0.0f) ? (c_min_half_width / world_scale) : c_min_half_width;
    const float                              half_width  = std::max(get_shape_width_factor(proxy.shape) * m_aspect_ratio * length, min_local);

    glm::mat4 transform{1.0f};
    if (length > 0.0f) {
        transform = orient_y_to(tail_local / length);
    }
    transform = glm::scale(transform, glm::vec3{half_width, std::max(length, min_local), half_width});

    // Head is the joint origin, so the proxy's local translation stays zero.
    proxy.node->set_parent_from_node(transform);
    proxy.tail_local   = tail_local;
    proxy.aspect_ratio = m_aspect_ratio;
}

void Bone_visualization::refresh_proxy_shape(Proxy& proxy)
{
    const std::shared_ptr<erhe::scene::Node> joint = proxy.joint.lock();
    if (!joint) {
        return;
    }
    // The bone's Rig.tail (R3): a local value when one is authored, else the
    // computed default (rig/bone_tail.hpp). Only rebuild the transform when
    // the bone shape actually changed. Under a rotation-only animation - the
    // common case - the child's local translation is constant, so this is a
    // compare and nothing else; the joint's own animation reaches the proxy
    // through the parent link.
    const glm::vec3          tail_local = joint->get_value(Rig::tail_property());
    const Bone_display_shape shape      = joint->get_value(Rig::display_shape_property());
    const bool               reshape    = (shape != proxy.shape);
    if (reshape) {
        // R17: the other unit shape; the material stays.
        const std::vector<erhe::scene::Mesh_primitive>& primitives = proxy.mesh->get_primitives();
        const std::shared_ptr<erhe::primitive::Material> material = primitives.empty() ? m_material : primitives.front().material;
        proxy.mesh->set_primitives({erhe::scene::Mesh_primitive{get_shape_primitive(shape), material}});
        proxy.shape = shape;
    }
    if (reshape || (tail_local != proxy.tail_local) || (m_aspect_ratio != proxy.aspect_ratio)) {
        set_proxy_transform(proxy, tail_local);
    }
}

void Bone_visualization::apply_proxy_flags(Proxy& proxy)
{
    // Visible for the solid display style OR whenever bone mode is on (you
    // cannot click what you cannot see); pickable only in bone mode.
    const bool visible  = m_bone_mode || m_solid;
    const bool pickable = m_bone_mode;
    proxy.mesh->set_visible(visible);
    if (pickable) {
        proxy.mesh->enable_flag_bits(erhe::Item_flags::id);
    } else {
        proxy.mesh->disable_flag_bits(erhe::Item_flags::id);
    }
    // Raytrace: set the instance mask directly rather than deriving it from the
    // flags, because bone_proxy must stay set (it is the proxy's identity)
    // while pickability toggles. Mask 0 = unhittable, so in object mode a ray
    // passes straight through to the mesh.
    proxy.mesh->set_rt_mask(pickable ? Raytrace_node_mask::bone : Raytrace_node_mask::none);
}

void Bone_visualization::update_proxy_material(Proxy& proxy)
{
    const std::shared_ptr<erhe::scene::Node> joint = proxy.joint.lock();
    if (!joint) {
        return;
    }
    // Selected bones render in a distinct color. Swap only on change:
    // set_primitives() rebuilds the raytrace primitives.
    // Mirror the joint's selection onto the proxy mesh. The flag is what splits
    // the two bone composition passes: unselected bones take the vdotn (N.V)
    // variant, selected ones render with their plain unlit material so the
    // selected color actually survives - vdotn overrides the fragment color
    // outright and would swallow it.
    // Hover wins over selection, matching the content mesh convention.
    // Hover_tool sets hovered_in_viewport on the JOINT (get_hover_node resolves
    // a hovered proxy to its joint), so it is read from there.
    // The bone's own display color (R17) when neither applies.
    const bool selected = joint->is_selected();
    const bool hovered  = joint->is_hovered();
    const std::shared_ptr<erhe::primitive::Material> material =
        hovered  ? m_hover_material    :
        selected ? m_selected_material : get_display_material(*joint);
    const std::vector<erhe::scene::Mesh_primitive>& primitives = proxy.mesh->get_primitives();
    const bool material_applied = !primitives.empty() && (primitives.front().material == material);
    if ((selected == proxy.selected) && (hovered == proxy.hovered) && material_applied) {
        return;
    }
    if (!material_applied) {
        proxy.mesh->set_primitive_material(0, material);
    }
    if (selected) {
        proxy.mesh->enable_flag_bits(erhe::Item_flags::selected);
    } else {
        proxy.mesh->disable_flag_bits(erhe::Item_flags::selected);
    }
    if (hovered) {
        proxy.mesh->enable_flag_bits(erhe::Item_flags::hovered_in_viewport);
    } else {
        proxy.mesh->disable_flag_bits(erhe::Item_flags::hovered_in_viewport);
    }
    proxy.selected = selected;
    proxy.hovered  = hovered;
}

void Bone_visualization::reconcile_bone(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (!node) {
        return;
    }
    const auto found = m_proxies.find(node.get());
    // A proxy stands for a bone of a scene: a node that left its scene (a
    // removal, the undo of an insert) or stopped being a bone loses it, and
    // gets it back when it returns.
    const bool wanted = (node->get_scene() != nullptr) && erhe::scene::is_bone(node.get());
    if (!wanted) {
        if (found != m_proxies.end()) {
            remove_proxy(found->second);
            m_proxies.erase(found);
        }
        return;
    }
    ensure_primitive();
    auto i = found;
    if ((i != m_proxies.end()) && (i->second.joint.lock() != node)) {
        // A stale entry of a destroyed node whose address was reused.
        remove_proxy(i->second);
        m_proxies.erase(i);
        i = m_proxies.end();
    }
    if (i == m_proxies.end()) {
        Proxy proxy = make_proxy(node);
        m_joint_by_proxy_mesh[proxy.mesh.get()] = node;
        i = m_proxies.emplace(node.get(), std::move(proxy)).first;
    }
    Proxy& proxy = i->second;
    refresh_proxy_shape(proxy);
    apply_proxy_flags(proxy);
    update_proxy_material(proxy);
}

void Bone_visualization::remove_proxy(Proxy& proxy)
{
    if (proxy.mesh) {
        m_joint_by_proxy_mesh.erase(proxy.mesh.get());
    }
    if (proxy.node) {
        proxy.node->set_node_parent(nullptr);
    }
}

void Bone_visualization::drop_expired_proxies()
{
    for (auto i = m_proxies.begin(); i != m_proxies.end(); ) {
        if (!i->second.joint.expired()) {
            ++i;
            continue;
        }
        remove_proxy(i->second);
        i = m_proxies.erase(i);
    }
}

void Bone_visualization::on_skin_registered(Skin_registered_message& message)
{
    if (!message.skin) {
        return;
    }
    // Registering marks the joints as bones (their own Bone_changed_message
    // follows for a joint whose flag changed); either way the skin is now
    // (or no longer) where the joints' default tails come from, so each
    // joint is reconciled. A joint keeps its proxy when the skin leaves: the
    // bone flag is authored and stays.
    for (const std::shared_ptr<erhe::scene::Node>& joint : message.skin->skin_data.joints) {
        reconcile_bone(joint);
    }
}

void Bone_visualization::on_bone_changed(Bone_changed_message& message)
{
    const std::shared_ptr<erhe::Item_base> item = message.node.lock();
    if (!item) {
        drop_expired_proxies();
    } else {
        reconcile_bone(std::dynamic_pointer_cast<erhe::scene::Node>(item));
    }
    // The parent's default tail is its first bone child's head.
    const std::shared_ptr<erhe::Item_base> parent_item = message.parent.lock();
    const erhe::scene::Node* const parent = dynamic_cast<const erhe::scene::Node*>(parent_item.get());
    if (parent != nullptr) {
        const auto parent_proxy = m_proxies.find(parent);
        if (parent_proxy != m_proxies.end()) {
            refresh_proxy_shape(parent_proxy->second);
        }
    }
}

auto Bone_visualization::get_bone_tail(const erhe::scene::Node& joint) const -> glm::vec3
{
    const auto i = m_proxies.find(&joint);
    if (i != m_proxies.end()) {
        return i->second.tail_local;
    }
    return joint.get_value(Rig::tail_property());
}

void Bone_visualization::on_close_scene(Close_scene_message& message)
{
    // Skin unregistration normally precedes this, but a teardown that skips it
    // (or a joint freed with the scene) must not leave proxies keeping items of
    // the closed scene alive - the scene-close leak watchdog would flag them.
    Scene_root* const scene_root = message.scene_root.get();
    for (auto i = m_proxies.begin(); i != m_proxies.end(); ) {
        const std::shared_ptr<erhe::scene::Node> joint = i->second.joint.lock();
        const bool drop = !joint || (joint->get_item_host() == scene_root) || (joint->get_item_host() == nullptr);
        if (!drop) {
            ++i;
            continue;
        }
        remove_proxy(i->second);
        i = m_proxies.erase(i);
    }
}

void Bone_visualization::on_selection(Selection_message& message)
{
    // The selected flags are already set when the message fires
    // (Selection::end_selection_change), so the changed items can be
    // reconciled directly.
    const auto reconcile = [this](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
        for (const std::shared_ptr<erhe::Item_base>& item : items) {
            const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(item.get());
            if (node == nullptr) {
                continue;
            }
            const auto i = m_proxies.find(node);
            if (i != m_proxies.end()) {
                update_proxy_material(i->second);
            }
        }
    };
    reconcile(message.selection_change.no_longer_selected);
    reconcile(message.selection_change.newly_selected);
}

void Bone_visualization::on_node_touched(erhe::scene::Node* node)
{
    if (node == nullptr) {
        return;
    }
    // A node's local translation feeds two default tails (rig/bone_tail.hpp):
    // its own (a skinned leaf bone's length is the joint's offset from its
    // parent) and its parent's (a parent bone's tail is the first child's
    // head).
    const auto self = m_proxies.find(node);
    if (self != m_proxies.end()) {
        refresh_proxy_shape(self->second);
    }
    const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
    if (parent) {
        const auto parent_proxy = m_proxies.find(parent.get());
        if (parent_proxy != m_proxies.end()) {
            refresh_proxy_shape(parent_proxy->second);
        }
    }
}

void Bone_visualization::on_animation_update()
{
    // The animation message does not say which nodes it moved, so every proxy
    // is compared; refresh_proxy_shape rebuilds only those whose tail actually
    // changed (translation-animated joints - rotation-only channels never do).
    for (auto& [joint_ptr, proxy] : m_proxies) {
        refresh_proxy_shape(proxy);
    }
}

void Bone_visualization::on_mode_changed()
{
    const bool bone_mode = (m_context.mesh_component_selection != nullptr) &&
        (m_context.mesh_component_selection->get_mode() == Mesh_component_mode::bone);
    if (bone_mode == m_bone_mode) {
        return;
    }
    m_bone_mode = bone_mode;
    for (auto& [joint_ptr, proxy] : m_proxies) {
        apply_proxy_flags(proxy);
    }
}

void Bone_visualization::apply_style_shape()
{
    const Debug_visualizations_style& style = m_context.editor_settings->debug_visualizations_style;
    if (style.bone_aspect_ratio != m_aspect_ratio) {
        m_aspect_ratio = style.bone_aspect_ratio;
        for (auto& [joint_ptr, proxy] : m_proxies) {
            refresh_proxy_shape(proxy);
        }
    }
    if (style.bone_solid != m_solid) {
        m_solid = style.bone_solid;
        for (auto& [joint_ptr, proxy] : m_proxies) {
            apply_proxy_flags(proxy);
        }
    }
}

void Bone_visualization::update_hover(const erhe::scene::Node* old_joint, const erhe::scene::Node* new_joint)
{
    for (const erhe::scene::Node* joint : { old_joint, new_joint }) {
        if (joint == nullptr) {
            continue;
        }
        const auto i = m_proxies.find(joint);
        if (i != m_proxies.end()) {
            update_proxy_material(i->second);
        }
    }
}

auto Bone_visualization::get_shape_primitive(const Bone_display_shape shape) const -> const std::shared_ptr<erhe::primitive::Primitive>&
{
    return (shape == Bone_display_shape::octahedral) ? m_bone_primitive : m_box_primitive;
}

auto Bone_visualization::get_display_material(const erhe::scene::Node& joint) -> std::shared_ptr<erhe::primitive::Material>
{
    const std::optional<glm::vec3> color = get_bone_display_color(joint);
    if (!color.has_value()) {
        return m_material;
    }
    // One material per distinct 8-bit color, so bones of one color share it
    // and the count stays bounded by the colors actually used.
    const auto to_byte = [](const float value) -> uint32_t {
        return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    const uint32_t r   = to_byte(color.value().r);
    const uint32_t g   = to_byte(color.value().g);
    const uint32_t b   = to_byte(color.value().b);
    const uint32_t key = (r << 16u) | (g << 8u) | b;
    const auto i = m_display_color_materials.find(key);
    if (i != m_display_color_materials.end()) {
        return i->second;
    }
    std::shared_ptr<erhe::primitive::Material> material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = fmt::format("bone color #{:06x}", key),
            .values = {
                .base_color = glm::vec3{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)} / 255.0f,
                .bxdf_model = erhe::primitive::Bxdf_model::unlit
            }
        }
    );
    // A builtin like the other bone materials: editor-owned, shared by the
    // proxies of every scene, so a mesh registration finds it a home.
    if (m_context.asset_manager != nullptr) {
        m_context.asset_manager->register_builtin(Asset_type::material, material);
    }
    m_display_color_materials.emplace(key, material);
    return material;
}

auto Bone_visualization::get_joint_for_proxy(const erhe::scene::Mesh* mesh) const -> std::shared_ptr<erhe::scene::Node>
{
    const auto i = m_joint_by_proxy_mesh.find(mesh);
    if (i == m_joint_by_proxy_mesh.end()) {
        return {};
    }
    return i->second.lock();
}

}
