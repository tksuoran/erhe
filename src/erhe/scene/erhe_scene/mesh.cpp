#include "erhe_scene/mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene {

// Mesh_primitive (D29)

Mesh_primitive::Mesh_primitive() = default;

Mesh_primitive::Mesh_primitive(
    const std::shared_ptr<erhe::primitive::Primitive>& primitive_in,
    const std::shared_ptr<erhe::primitive::Material>&  material_in,
    const glm::vec4&                                   lightmap_uv_scale_offset_in
)
    : primitive               {primitive_in}
    , material                {material_in}
    , lightmap_uv_scale_offset{lightmap_uv_scale_offset_in}
{
}

Mesh_primitive::Mesh_primitive(const Mesh_primitive& other)
    : erhe::property::Dependency_object{other}
    , primitive               {other.primitive}
    , material                {other.material}
    , lightmap_uv_scale_offset{other.lightmap_uv_scale_offset}
{
}

Mesh_primitive& Mesh_primitive::operator=(const Mesh_primitive& other)
{
    if (this != &other) {
        erhe::property::Dependency_object::operator=(other);
        primitive                = other.primitive;
        material                 = other.material;
        lightmap_uv_scale_offset = other.lightmap_uv_scale_offset;
        // m_owner / m_index stay: they describe this object's slot.
    }
    return *this;
}

Mesh_primitive::~Mesh_primitive() noexcept = default;

auto Mesh_primitive::property_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type id = erhe::property::allocate_owner_type(erhe::property::root_owner_type, "Mesh_primitive");
    return id;
}

void Mesh_primitive::on_material_set(Mesh_primitive& primitive)
{
    if (primitive.m_owner != nullptr) {
        primitive.m_owner->notify_primitive_material_changed();
    }
}

const erhe::property::Property<erhe::property::Object_reference> Mesh_primitive::material_property =
    erhe::property::Property<erhe::property::Object_reference>::register_member(
        "material", Mesh_primitive::property_owner_type(), &Mesh_primitive::material,
        erhe::property::Property_metadata{
            // The glTF primitive's material index carries this (D28); a
            // member-backed property is excluded from a value pass anyway.
            .flags = erhe::property::Property_flags::serialize | erhe::property::Property_flags::native_gltf,
            .ui = erhe::property::Property_ui{
                .label                = "Material",
                .reference_item_types = erhe::Item_type::material,
                .show_clear_button    = false // a primitive keeps a material
            }
        },
        &Mesh_primitive::on_material_set
    );

// Mesh

auto Mesh::get_scene_host() const -> Scene_host*
{
    // Every Item_host a Mesh can be attached to is a Scene_host (see
    // handle_item_host_update).
    return static_cast<Scene_host*>(get_item_host());
}

namespace {

auto world_bounds_corner(const erhe::property::Dependency_object& object, const bool max_corner) -> erhe::property::Property_value
{
    const erhe::math::Aabb aabb = static_cast<const Mesh&>(object).get_aabb_world();
    if (!aabb.is_valid()) {
        return glm::vec3{0.0f};
    }
    return max_corner ? aabb.max : aabb.min;
}

} // anonymous namespace

// Computed world bounds (D26)
const erhe::property::Property<glm::vec3> Mesh::world_bounds_min_property = erhe::property::Property<glm::vec3>::register_computed(
    "world_bounds_min", Mesh::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return world_bounds_corner(object, false); },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "World", .tooltip = "Minimum corner of the world-space bounding box (computed)", .label = "Bounds Min"}
    }
);
const erhe::property::Property<glm::vec3> Mesh::world_bounds_max_property = erhe::property::Property<glm::vec3>::register_computed(
    "world_bounds_max", Mesh::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value { return world_bounds_corner(object, true); },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.group = "World", .tooltip = "Maximum corner of the world-space bounding box (computed)", .label = "Bounds Max"}
    }
);

// Inherited flags (D23), the flag word as the mirror. The callback runs on
// meshes only: deliver skips a metadata callback on a holder (a node or a
// style holding Mesh.shadow_cast for the meshes below it, D30).
const erhe::property::Property<bool> Mesh::shadow_cast_property = erhe::property::Property<bool>::register_property(
    "shadow_cast", Mesh::property_owner_type(),
    erhe::property::Property_metadata{.default_value = false, .property_changed = Mesh::on_render_flag_property_changed, .inherits = true, .ui = erhe::property::Property_ui{.group = "Rendering", .label = "Cast Shadow"}}
);
const erhe::property::Property<bool> Mesh::lightmapped_property = erhe::property::Property<bool>::register_property(
    "lightmapped", Mesh::property_owner_type(),
    erhe::property::Property_metadata{.default_value = false, .property_changed = Mesh::on_render_flag_property_changed, .inherits = true, .ui = erhe::property::Property_ui{.group = "Rendering", .label = "Lightmapped"}}
);

void Mesh::on_render_flag_property_changed(erhe::property::Dependency_object& object, const erhe::property::Property_changed_args& args)
{
    Mesh& mesh = static_cast<Mesh&>(object);
    const bool value = erhe::property::get_as<bool>(args.new_value);
    if (&args.property == &shadow_cast_property.get()) {
        mesh.set_derived_flag_bit(Item_flags::shadow_cast, value);
    } else if (&args.property == &lightmapped_property.get()) {
        mesh.set_derived_flag_bit(Item_flags::lightmapped, value);
    }
}

// After a copy (Item_base::rederive_flag_bits keeps only visible): the
// copy has no node, so an inherited value does not survive; the bits must
// agree with the copied entries.
void Mesh::rederive_render_flag_bits()
{
    set_derived_flag_bit(Item_flags::shadow_cast, get_value(shadow_cast_property));
    set_derived_flag_bit(Item_flags::lightmapped, get_value(lightmapped_property));
}

void Mesh::notify_primitives_changed()
{
    // The computed world bounds (D26) follow the primitives.
    invalidate_dependents(world_bounds_min_property);
    invalidate_dependents(world_bounds_max_property);
    // Constructors reach here through add_primitive() before shared_from_this
    // is usable (lock() yields null); a mesh under construction is not
    // attached to any host anyway.
    const std::shared_ptr<Mesh> shared_this = std::static_pointer_cast<Mesh>(weak_from_this().lock());
    if (!shared_this) {
        return;
    }
    Scene_host* scene_host = get_scene_host();
    if (scene_host == nullptr) {
        return;
    }
    scene_host->on_mesh_primitives_changed(shared_this);
}

void Mesh::stamp_primitive_owners()
{
    for (std::size_t i = 0, end = m_primitives.size(); i < end; ++i) {
        m_primitives[i].m_owner = this;
        m_primitives[i].m_index = i;
    }
}

auto Mesh::get_property_sub_object_count() const -> std::size_t
{
    return m_primitives.size();
}

auto Mesh::get_property_sub_object(const std::size_t index) -> erhe::property::Dependency_object*
{
    return (index < m_primitives.size()) ? &m_primitives[index] : nullptr;
}

auto Mesh::get_property_sub_object(const std::size_t index) const -> const erhe::property::Dependency_object*
{
    return (index < m_primitives.size()) ? &m_primitives[index] : nullptr;
}

auto Mesh::get_property_sub_object_label(const std::size_t index) const -> std::string
{
    if (index >= m_primitives.size()) {
        return {};
    }
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = m_primitives[index].primitive;
    const std::string_view name = primitive ? primitive->get_name() : std::string_view{};
    return name.empty() ? "Primitive " + std::to_string(index) : std::string{name};
}

void Mesh::clear_primitives()
{
    if (m_primitives.empty()) {
        return;
    }
    m_primitives.clear();
    m_rt_primitives.clear();
    notify_primitives_changed();
}

void Mesh::update_rt_primitives()
{
    m_rt_primitives.clear();
    for (std::size_t i = 0, end = m_primitives.size(); i < end; ++i) {
        const Mesh_primitive&                                    mesh_primitive = m_primitives[i];
        const erhe::primitive::Primitive&                        primitive      = *mesh_primitive.primitive.get();
        const std::shared_ptr<erhe::primitive::Primitive_shape>& shape          = primitive.get_shape_for_raytrace();
        if (!shape) {
            continue;
        }
        const erhe::primitive::Primitive_raytrace&        primitive_raytrace = shape->get_raytrace();
        const std::shared_ptr<erhe::raytrace::IGeometry>& rt_geometry        = primitive_raytrace.get_raytrace_geometry();
        if (rt_geometry) {
            m_rt_primitives.emplace_back(
                new Raytrace_primitive(this, i, rt_geometry.get())
            );
        }
    }
    // Freshly created instances default to the identity transform and are
    // uncommitted; seed them with the node's current world transform (and
    // commit) the same way a node move would. Without this, swapping
    // primitives on an already-placed node (e.g. a geometry graph re-bake)
    // leaves the raytrace instances at the origin - hover / picking misses
    // the mesh until the node next moves.
    handle_node_transform_update();
    notify_primitives_changed();
}

void Mesh::invalidate_optimized_primitive_variant(const std::size_t primitive_index)
{
    if (primitive_index >= m_primitives.size()) {
        return;
    }
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = m_primitives[primitive_index].primitive;
    if (!primitive || !primitive->optimized_render_shape) {
        // The common case by far, and why this is cheap enough to call per
        // painted corner: no variant, nothing to do.
        return;
    }

    // Drop first, re-register second: registration reads what is live, so the
    // order decides whether the sharers pick up the original or the shape being
    // retired. The dropped shape's GPU ranges go to the pool's retired list,
    // which is frame-gated, so a record not yet rebuilt still addresses valid
    // memory until it is.
    primitive->optimized_render_shape.reset();

    Scene_host* scene_host = get_scene_host();
    Scene*      scene      = (scene_host != nullptr) ? scene_host->get_hosted_scene() : nullptr;
    if (scene == nullptr) {
        // Not hosted: no sharers to find and no draw lists to update.
        notify_primitives_changed();
        return;
    }

    for (const std::shared_ptr<Mesh_layer>& mesh_layer : scene->get_mesh_layers()) {
        for (const std::shared_ptr<Mesh>& layer_mesh : mesh_layer->meshes) {
            if (!layer_mesh) {
                continue;
            }
            for (const Mesh_primitive& candidate : layer_mesh->get_primitives()) {
                if (candidate.primitive.get() == primitive.get()) {
                    layer_mesh->notify_primitives_changed();
                    break;
                }
            }
        }
    }
}

auto Mesh::begin_optimized_variant_edit(const std::size_t primitive_index) -> std::shared_ptr<erhe::primitive::Primitive>
{
    if (primitive_index >= m_primitives.size()) {
        return {};
    }
    const std::shared_ptr<erhe::primitive::Primitive> primitive = m_primitives[primitive_index].primitive;
    if (!primitive) {
        return {};
    }
    // The hold is taken even when no variant is live: it is what keeps a
    // concurrent finalize from publishing a pre-edit variant mid-edit.
    ++primitive->optimization_hold_count;
    invalidate_optimized_primitive_variant(primitive_index);
    return primitive;
}

void Mesh::add_primitive(
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const std::shared_ptr<erhe::primitive::Material>&  material
)
{
    m_primitives.emplace_back(primitive, material);
    stamp_primitive_owners();
    update_rt_primitives();
}

void Mesh::set_primitives(const std::vector<Mesh_primitive>& primitives)
{
    m_primitives = primitives;
    stamp_primitive_owners();
    update_rt_primitives();
}

void Mesh::set_primitive_material(const std::size_t primitive_index, const std::shared_ptr<erhe::primitive::Material>& material)
{
    if (primitive_index >= m_primitives.size()) {
        return;
    }
    // Unchanged value: the store notifies nothing (R4).
    m_primitives[primitive_index].set_value(Mesh_primitive::material_property, erhe::property::Object_reference{material});
}

void Mesh::notify_primitive_material_changed()
{
    const std::shared_ptr<Mesh> shared_this = std::static_pointer_cast<Mesh>(weak_from_this().lock());
    if (!shared_this) {
        return;
    }
    Scene_host* scene_host = get_scene_host();
    if (scene_host == nullptr) {
        return;
    }
    scene_host->on_mesh_material_changed(shared_this);
}

void Mesh::set_primitive_lightmap_uv_scale_offset(const std::size_t primitive_index, const glm::vec4& lightmap_uv_scale_offset)
{
    if (primitive_index >= m_primitives.size()) {
        return;
    }
    if (m_primitives[primitive_index].lightmap_uv_scale_offset == lightmap_uv_scale_offset) {
        return;
    }
    m_primitives[primitive_index].lightmap_uv_scale_offset = lightmap_uv_scale_offset;
    const std::shared_ptr<Mesh> shared_this = std::static_pointer_cast<Mesh>(weak_from_this().lock());
    if (!shared_this) {
        return;
    }
    Scene_host* scene_host = get_scene_host();
    if (scene_host == nullptr) {
        return;
    }
    scene_host->on_mesh_primitive_data_changed(shared_this);
}

auto Mesh::get_primitives() const -> const std::vector<Mesh_primitive>&
{
    return m_primitives;
}

Mesh::Mesh() = default;

Mesh::Mesh(Mesh&& other) noexcept
    : Item          {std::move(other)}
    , layer_id      {other.layer_id}
    , skin          {std::move(other.skin)}
    , point_size    {other.point_size}
    , line_width    {other.line_width}
    , m_primitives  {std::move(other.m_primitives)}
    , m_rt_scene    {other.m_rt_scene}
    , m_rt_primitives{std::move(other.m_rt_primitives)}
    , m_rt_primitives_dirty{other.m_rt_primitives_dirty}
{
    other.m_rt_scene = nullptr;
    stamp_primitive_owners();
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other) {
        Item::operator=(std::move(other));
        layer_id              = other.layer_id;
        skin                  = std::move(other.skin);
        point_size            = other.point_size;
        line_width            = other.line_width;
        m_primitives          = std::move(other.m_primitives);
        m_rt_scene            = other.m_rt_scene;
        m_rt_primitives       = std::move(other.m_rt_primitives);
        m_rt_primitives_dirty = other.m_rt_primitives_dirty;
        other.m_rt_scene      = nullptr;
        stamp_primitive_owners();
    }
    return *this;
}

Mesh::Mesh(const std::string_view name)
    : Item{name}
{
}

Mesh::Mesh(
    const std::string_view                             name,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive
)
    : Item{name}
{
    add_primitive(primitive, {});
}

Mesh::Mesh(const Mesh& src, erhe::for_clone)
    : Item      {src, for_clone{}}
    , layer_id  {src.layer_id}
    , skin      {src.skin}
    , point_size{src.point_size}
    , line_width{src.line_width}
{
    rederive_render_flag_bits();
    set_primitives(src.get_primitives());
}

Mesh::~Mesh() noexcept
{
    if (m_rt_scene != nullptr) {
        detach_rt_from_scene();
    }
}

auto Mesh::get_rt_scene() const -> erhe::raytrace::IScene*
{
    return m_rt_scene;
}

auto Mesh::get_rt_primitives() const -> const std::vector<std::unique_ptr<Raytrace_primitive>>&
{
    return m_rt_primitives;
}

void Mesh::set_rt_mask(const uint32_t mask)
{
    for (const auto& rt_primitive : m_rt_primitives) {
        rt_primitive->rt_instance->set_mask(mask);
    }
}

void Mesh::attach_rt_to_scene(erhe::raytrace::IScene* rt_scene)
{
    ERHE_VERIFY(rt_scene != nullptr);
    ERHE_VERIFY(m_rt_scene == nullptr);
    for (const auto& rt_primitive : m_rt_primitives) {
        rt_scene->attach(rt_primitive->rt_instance.get());
    }
    m_rt_scene = rt_scene;
}

void Mesh::detach_rt_from_scene() // erhe::raytrace::IScene* rt_scene)
{
    //ERHE_VERIFY((rt_scene == m_rt_scene) || (m_rt_scene == nullptr));
    if (m_rt_scene == nullptr) { // not attached
        return;
    }
    //ERHE_VERIFY(m_rt_scene != nullptr);
    for (const auto& rt_primitive : m_rt_primitives) {
        m_rt_scene->detach(rt_primitive->rt_instance.get());
    }
    m_rt_scene = nullptr;
}

void Mesh::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    const auto shared_this = std::static_pointer_cast<Mesh>(shared_from_this()); // keep alive

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_mesh(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_mesh(shared_this);
    }
}

void Mesh::handle_flag_bits_update(uint64_t old_flag_bits, uint64_t new_flag_bits)
{
    const uint64_t changed_bits = old_flag_bits ^ new_flag_bits;

    // Mirror every flag change to the scene host (draw list entry flags,
    // doc/draw_list_renderer_requirements.md R12a) before the raytrace-only
    // visibility gate below.
    {
        const std::shared_ptr<Mesh> shared_this = std::static_pointer_cast<Mesh>(weak_from_this().lock());
        if (shared_this) {
            Scene_host* scene_host = get_scene_host();
            if (scene_host != nullptr) {
                scene_host->on_mesh_flags_changed(shared_this, old_flag_bits, new_flag_bits);
            }
        }
    }

    const bool visibility_changed = erhe::utility::test_bit_set(changed_bits, erhe::Item_flags::visible);
    if (!visibility_changed) {
        return;
    }

    for (const auto& rt_primitive : m_rt_primitives) {
        const bool visible = erhe::utility::test_bit_set(new_flag_bits, erhe::Item_flags::visible);
        if (visible && !rt_primitive->rt_instance->is_enabled()) {
            rt_primitive->rt_instance->enable();
        } else if (!visible && rt_primitive->rt_instance->is_enabled()) {
            rt_primitive->rt_instance->disable();
        }
    }
}

void Mesh::handle_node_transform_update()
{
    const glm::mat4& world_from_node = (get_node() != nullptr) ? get_node()->world_from_node() : glm::mat4{1.0f};
    // Affine transform: det(mat4) == det(upper-left mat3); this runs for
    // every mesh under a moving subtree, every frame.
    const float determinant = glm::determinant(glm::mat3{world_from_node});
    if (determinant < 0.0f) {
        enable_flag_bits(Item_flags::negative_determinant);
    } else {
        disable_flag_bits(Item_flags::negative_determinant);
    }
    for (const auto& rt_primitive : m_rt_primitives) {
        rt_primitive->rt_instance->set_transform(world_from_node);
        rt_primitive->rt_instance->commit();
    }
    // The computed world bounds (D26) moved with the node; one null check
    // each when nothing depends on them.
    invalidate_dependents(world_bounds_min_property);
    invalidate_dependents(world_bounds_max_property);
    // Draw list primitive records (doc/draw_list_performance_improvements.md):
    // the host only enqueues; the records are rewritten once per frame.
    {
        const std::shared_ptr<Mesh> shared_this = std::static_pointer_cast<Mesh>(weak_from_this().lock());
        if (shared_this) {
            Scene_host* scene_host = get_scene_host();
            if (scene_host != nullptr) {
                scene_host->on_mesh_transform_changed(shared_this);
            }
        }
    }
}

auto Mesh::get_skinned_aabb_world() const -> erhe::math::Aabb
{
    erhe::math::Aabb aabb;
    for (const Mesh_primitive& mesh_primitive : m_primitives) {
        if (!mesh_primitive.primitive) {
            continue;
        }
        aabb.include(get_skinned_primitive_aabb_world(*mesh_primitive.primitive.get()));
    }
    return aabb;
}

auto Mesh::get_skinned_primitive_aabb_world(const erhe::primitive::Primitive& primitive) const -> erhe::math::Aabb
{
    erhe::math::Aabb aabb;
    if (!skin) {
        return aabb;
    }
    const Skin_data& skin_data = skin->skin_data;

    const erhe::primitive::Primitive_render_shape* shape = primitive.render_shape.get();
    if (shape == nullptr) {
        return aabb;
    }
    // Per-joint bounds are the same set of vertices either way, so the
    // always-present variant is the right one to read.
    const std::vector<erhe::math::Aabb>& joint_boxes =
        shape->get_renderable_mesh().joint_bounding_boxes;
    const std::size_t end = std::min(joint_boxes.size(), skin_data.joints.size());
    for (std::size_t i = 0; i < end; ++i) {
        const erhe::math::Aabb& joint_box = joint_boxes[i];
        if (!joint_box.is_valid()) {
            continue; // joint influences no vertex of this primitive
        }
        const std::optional<glm::mat4> world_from_bind = skin_data.get_world_from_bind(i);
        if (!world_from_bind.has_value()) {
            continue;
        }
        aabb.include(joint_box.transformed_by(world_from_bind.value()));
    }
    return aabb;
}

auto Mesh::get_aabb_world() const -> erhe::math::Aabb
{
    // A GPU-skinned mesh is posed entirely by its joints: glTF 2.0 requires the
    // skinned mesh node's transform to be ignored, and erhe honors that
    // (Joint_buffer + the skinning branch in standard.vert). Bounding it by the
    // rest-pose box under the node transform - as the unskinned path below does
    // - is therefore wrong twice over. Bound it from the joints instead.
    //
    // Recomputed on every call rather than cached: joints move every frame under
    // animation, and primitives can be rebuilt behind the Mesh's back (through
    // get_mutable_primitives() and Primitive::make_renderable_mesh()), so there
    // is no reliable invalidation signal. Cost is one box transform per joint
    // that actually influences the mesh.
    if (skin) {
        const erhe::math::Aabb skinned_aabb = get_skinned_aabb_world();
        if (skinned_aabb.is_valid()) {
            return skinned_aabb;
        }
        // Fall through when the primitives carry no per-joint bounds (geometry
        // built without joint attributes). The result is wrong in the ways
        // described above, but it is what the caller got before, and it beats
        // returning an empty box that silently culls the mesh.
    }

    const erhe::scene::Node* node = get_node();
    const glm::mat4 world_from_local = (node != nullptr) ? node->world_from_node() : glm::mat4{1.0f};
    erhe::math::Aabb aabb;
    for (const Mesh_primitive& mesh_primitive : m_primitives) {
        const erhe::math::Aabb primitive_aabb_local = mesh_primitive.primitive->get_bounding_box();
        const erhe::math::Aabb primitive_aabb_world = primitive_aabb_local.transformed_by(world_from_local);
        aabb.include(primitive_aabb_world);
    }
    return aabb;
}


auto operator<(const Mesh& lhs, const Mesh& rhs) -> bool
{
    return lhs.get_id() < rhs.get_id();
}

auto get_mesh(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<Mesh>
{
    std::shared_ptr<Mesh> scene_mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
    if (scene_mesh) {
        return scene_mesh;
    }

    // If we have node, get mesh from node
    std::shared_ptr<Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (node_shared) {
        return get_attachment<Mesh>(node_shared.get());
    }

    return {};
}


} // namespace erhe::scene

