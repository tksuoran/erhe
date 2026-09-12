// The UsdPhysics half of the USD reader (doc/usd_compatibility.md, "Physics",
// and src/erhe/usd/notes.md, "Physics"): the bodies, colliders, physics
// materials, collision groups, joints and the physics scene of a stage, read
// into the format-neutral `erhe::scene::Physics_description` the glTF reader
// fills too, plus the USD-side record that says where each of those sits on
// the stage.

#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

// LightUSD headers, as in usd_import.cpp: erhe::usd is the only erhe library
// that includes them.
#include "lightusd.hh"
#include "core/model-scope.hh"
#include "core/prim.hh"
#include "core/prim-metas.hh"
#include "core/property.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdPhysics.hh"
#include "usdShade.hh"
#include "value-pprint.hh"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::usd {

// A USDA literal rewritten in erhe's property text form (D16): a tuple or
// array becomes space-separated components, a quoted token or string loses
// its quotes. `(1, 0.5, 0)` becomes `1 0.5 0`, `"guide"` becomes `guide`,
// `5000` stays `5000`. A string value that carries a comma or a bracket of
// its own is not representable this way and is left to fail parsing.
auto usd_literal_to_property_text(const std::string& literal) -> std::string
{
    std::string text;
    text.reserve(literal.size());
    bool pending_space = false;
    for (const char character : literal) {
        switch (character) {
            case '(':
            case ')':
            case '[':
            case ']':
            case '"':
            case '\'': {
                break;
            }
            case ',':
            case ' ':
            case '\t':
            case '\n':
            case '\r': {
                pending_space = !text.empty();
                break;
            }
            default: {
                if (pending_space) {
                    text.push_back(' ');
                    pending_space = false;
                }
                text.push_back(character);
                break;
            }
        }
    }
    return text;
}

namespace {

using Property_map = std::map<std::string, lightusd::Property>;

constexpr std::size_t c_no_index = ~std::size_t{0};

// Degrees are the angular unit of every UsdPhysics attribute carrying an
// angle - a rotational limit, a cone angle, an angular drive target, an
// angular velocity - and radians are erhe's.
[[nodiscard]] auto to_radians(const float degrees) -> float
{
    return degrees * (glm::pi<float>() / 180.0f);
}

[[nodiscard]] auto to_axis_name(const lightusd::Axis axis) -> std::string
{
    switch (axis) {
        case lightusd::Axis::X: return std::string{"X"};
        case lightusd::Axis::Y: return std::string{"Y"};
        case lightusd::Axis::Z: return std::string{"Z"};
        default:                return std::string{};
    }
}

template <typename T>
[[nodiscard]] auto typed_props(const lightusd::Prim& prim) -> const Property_map*
{
    const T* typed = prim.as<T>();
    return (typed != nullptr) ? &typed->props : nullptr;
}

// The generic property map of one prim: where every attribute and
// relationship the prim's own schema does not name lives, which is where the
// `physics:` properties of an applied API schema and the `erhe:` custom
// attributes of the mapping are. Every prim class a physics record can sit on
// is listed.
[[nodiscard]] auto prim_props(const lightusd::Prim& prim) -> const Property_map*
{
    const Property_map* props = nullptr;
    if ((props = typed_props<lightusd::Model                >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::Xform                >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::Scope                >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::Material             >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomMesh             >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomCube             >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomSphere           >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomCone             >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomCylinder         >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomCylinder_1       >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomCapsule          >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::GeomCapsule_1        >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsScene         >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsJoint         >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsRevoluteJoint >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsPrismaticJoint>(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsSphericalJoint>(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsFixedJoint    >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsDistanceJoint >(prim)) != nullptr) { return props; }
    if ((props = typed_props<lightusd::PhysicsCollisionGroup>(prim)) != nullptr) { return props; }
    return nullptr;
}

[[nodiscard]] auto find_attribute(const Property_map* props, const std::string& name) -> const lightusd::Attribute*
{
    if (props == nullptr) {
        return nullptr;
    }
    const Property_map::const_iterator i = props->find(name);
    if (i == props->end()) {
        return nullptr;
    }
    return i->second.get_attribute_or_null();
}

[[nodiscard]] auto find_relationship(const Property_map* props, const std::string& name) -> const lightusd::Relationship*
{
    if (props == nullptr) {
        return nullptr;
    }
    const Property_map::const_iterator i = props->find(name);
    if (i == props->end()) {
        return nullptr;
    }
    return i->second.get_relationship_or_null();
}

[[nodiscard]] auto read_float(const Property_map* props, const std::string& name, float& out_value) -> bool
{
    const lightusd::Attribute* attribute = find_attribute(props, name);
    if (attribute == nullptr) {
        return false;
    }
    float float_value = 0.0f;
    if (attribute->get_value(&float_value)) {
        out_value = float_value;
        return true;
    }
    double double_value = 0.0;
    if (attribute->get_value(&double_value)) {
        out_value = static_cast<float>(double_value);
        return true;
    }
    return false;
}

[[nodiscard]] auto read_bool(const Property_map* props, const std::string& name, bool& out_value) -> bool
{
    const lightusd::Attribute* attribute = find_attribute(props, name);
    if (attribute == nullptr) {
        return false;
    }
    return attribute->get_value(&out_value);
}

[[nodiscard]] auto read_token(const Property_map* props, const std::string& name, std::string& out_value) -> bool
{
    const lightusd::Attribute* attribute = find_attribute(props, name);
    if (attribute == nullptr) {
        return false;
    }
    lightusd::value::token token_value;
    if (attribute->get_value(&token_value)) {
        out_value = token_value.str();
        return true;
    }
    return false;
}

[[nodiscard]] auto read_vec3(const Property_map* props, const std::string& name, glm::vec3& out_value) -> bool
{
    const lightusd::Attribute* attribute = find_attribute(props, name);
    if (attribute == nullptr) {
        return false;
    }
    lightusd::value::point3f point_value{0.0f, 0.0f, 0.0f};
    if (attribute->get_value(&point_value)) {
        out_value = glm::vec3{point_value[0], point_value[1], point_value[2]};
        return true;
    }
    lightusd::value::vector3f vector_value{0.0f, 0.0f, 0.0f};
    if (attribute->get_value(&vector_value)) {
        out_value = glm::vec3{vector_value[0], vector_value[1], vector_value[2]};
        return true;
    }
    lightusd::value::float3 float3_value{{0.0f, 0.0f, 0.0f}};
    if (attribute->get_value(&float3_value)) {
        out_value = glm::vec3{float3_value[0], float3_value[1], float3_value[2]};
        return true;
    }
    return false;
}

[[nodiscard]] auto read_quat(const Property_map* props, const std::string& name, glm::quat& out_value) -> bool
{
    const lightusd::Attribute* attribute = find_attribute(props, name);
    if (attribute == nullptr) {
        return false;
    }
    lightusd::value::quatf quat_value{{{0.0f, 0.0f, 0.0f}}, 1.0f};
    if (attribute->get_value(&quat_value)) {
        out_value = glm::quat{quat_value.real, quat_value.imag[0], quat_value.imag[1], quat_value.imag[2]};
        return true;
    }
    return false;
}

[[nodiscard]] auto read_string_array(const Property_map* props, const std::string& name, std::vector<std::string>& out_value) -> bool
{
    const lightusd::Attribute* attribute = find_attribute(props, name);
    if (attribute == nullptr) {
        return false;
    }
    std::vector<std::string> strings;
    if (attribute->get_value(&strings)) {
        out_value = std::move(strings);
        return true;
    }
    std::vector<lightusd::value::token> tokens;
    if (attribute->get_value(&tokens)) {
        out_value.clear();
        out_value.reserve(tokens.size());
        for (const lightusd::value::token& token : tokens) {
            out_value.push_back(token.str());
        }
        return true;
    }
    return false;
}

// The paths one relationship names, in the order it spells them.
void read_relationship_paths(const lightusd::Relationship* relationship, std::vector<std::string>& out_paths)
{
    if (relationship == nullptr) {
        return;
    }
    if (relationship->is_path()) {
        out_paths.push_back(relationship->targetPath.full_path_name());
        return;
    }
    if (relationship->is_pathvector()) {
        for (const lightusd::Path& path : relationship->targetPathVector) {
            out_paths.push_back(path.full_path_name());
        }
    }
}

// A schema attribute's value at the default time; an unauthored one answers
// with the schema fallback the prim struct carries.
[[nodiscard]] auto read_schema_double(
    const lightusd::TypedAttributeWithFallback<lightusd::Animatable<double>>& attribute,
    const double                                                              fallback
) -> double
{
    double value = fallback;
    if (attribute.get_value().get_default(&value)) {
        return value;
    }
    return fallback;
}

template <typename T>
[[nodiscard]] auto read_purpose(const lightusd::Prim& prim, lightusd::Purpose& purpose) -> bool
{
    const T* typed = prim.as<T>();
    if (typed == nullptr) {
        return false;
    }
    purpose = typed->purpose.get_value();
    return true;
}

// A primitive-schema prim whose `purpose` is `guide` carries a body's
// collision shape and nothing else: it is no scene content once the shape has
// folded into the body.
[[nodiscard]] auto is_guide_prim(const lightusd::Prim& prim) -> bool
{
    lightusd::Purpose purpose = lightusd::Purpose::Default;
    const bool read =
        read_purpose<lightusd::GeomCube      >(prim, purpose) ||
        read_purpose<lightusd::GeomSphere    >(prim, purpose) ||
        read_purpose<lightusd::GeomCone      >(prim, purpose) ||
        read_purpose<lightusd::GeomCylinder  >(prim, purpose) ||
        read_purpose<lightusd::GeomCylinder_1>(prim, purpose) ||
        read_purpose<lightusd::GeomCapsule   >(prim, purpose) ||
        read_purpose<lightusd::GeomCapsule_1 >(prim, purpose);
    return read && (purpose == lightusd::Purpose::Guide);
}

[[nodiscard]] auto is_primitive_schema_prim_type(const std::string& type_name) -> bool
{
    return
        (type_name == "Cube")       ||
        (type_name == "Sphere")     ||
        (type_name == "Cone")       ||
        (type_name == "Cylinder")   ||
        (type_name == "Cylinder_1") ||
        (type_name == "Capsule")    ||
        (type_name == "Capsule_1");
}

[[nodiscard]] auto is_joint_prim_type(const std::string& type_name) -> bool
{
    return
        (type_name == c_physics_joint_prim_type_name)           ||
        (type_name == c_physics_revolute_joint_prim_type_name)  ||
        (type_name == c_physics_prismatic_joint_prim_type_name) ||
        (type_name == c_physics_spherical_joint_prim_type_name) ||
        (type_name == c_physics_fixed_joint_prim_type_name)     ||
        (type_name == c_physics_distance_joint_prim_type_name);
}

// The axis index of a `physics:axis` token or of a limit / drive instance
// name: `X`, `transX` and `rotX` are 0, `Y` is 1, `Z` is 2.
[[nodiscard]] auto axis_index_of(const std::string& text) -> int
{
    if (text.empty()) {
        return 0;
    }
    switch (text.back()) {
        case 'X': return 0;
        case 'Y': return 1;
        case 'Z': return 2;
        default:  return 0;
    }
}

// One limit or drive instance of the multi-apply schemas, as the reader needs
// it: which axis it names, and whether that axis is a translation.
class Dof final
{
public:
    std::string name;
    int         axis       {0};
    bool        is_linear  {true};
    bool        is_distance{false};
};

[[nodiscard]] auto to_dof(const std::string& instance_name) -> Dof
{
    Dof dof{};
    dof.name        = instance_name;
    dof.axis        = axis_index_of(instance_name);
    dof.is_distance = (instance_name == "distance");
    dof.is_linear   = (instance_name.rfind("rot", 0) != 0) && (instance_name != "angular");
    return dof;
}

// One limit the reader has read, before the instances that say the same thing
// are joined into one `Physics_joint_limit`.
class Limit_entry final
{
public:
    Dof                  dof;
    std::optional<float> min;
    std::optional<float> max;
    std::optional<float> stiffness;
    float                damping{0.0f};
};

[[nodiscard]] auto same_limit_values(const Limit_entry& lhs, const Limit_entry& rhs) -> bool
{
    return
        (lhs.min       == rhs.min      ) &&
        (lhs.max       == rhs.max      ) &&
        (lhs.stiffness == rhs.stiffness) &&
        (lhs.damping   == rhs.damping  );
}

// The limits the reader read, with the instances of identical value joined
// into one erhe limit - the inverse of the writer's one instance per axis.
[[nodiscard]] auto join_limits(const std::vector<Limit_entry>& entries) -> std::vector<erhe::scene::Physics_joint_limit>
{
    std::vector<erhe::scene::Physics_joint_limit> limits;
    std::vector<Limit_entry>                      joined;
    for (const Limit_entry& entry : entries) {
        std::size_t index = 0;
        bool        found = false;
        for (std::size_t i = 0, end = joined.size(); i < end; ++i) {
            if (same_limit_values(joined[i], entry)) {
                index = i;
                found = true;
                break;
            }
        }
        if (!found) {
            joined.push_back(entry);
            limits.push_back(
                erhe::scene::Physics_joint_limit{
                    .min       = entry.min,
                    .max       = entry.max,
                    .stiffness = entry.stiffness,
                    .damping   = entry.damping
                }
            );
            index = limits.size() - 1;
        }
        erhe::scene::Physics_joint_limit& limit = limits[index];
        if (entry.dof.is_distance) {
            limit.linear_axes = std::vector<int>{0, 1, 2};
            continue;
        }
        std::vector<int>& axes = entry.dof.is_linear ? limit.linear_axes : limit.angular_axes;
        if (std::find(axes.begin(), axes.end(), entry.dof.axis) == axes.end()) {
            axes.push_back(entry.dof.axis);
        }
    }
    return limits;
}

// Which of the six degrees of freedom a subclass joint's own limit attributes
// name.
enum class Limit_axis_kind : unsigned int {
    linear  = 0,
    angular = 1
};

// The common base of the six joint classes: LightUSD reconstructs into it the
// attributes UsdPhysics gives every joint.
[[nodiscard]] auto joint_base_of(const lightusd::Prim& prim) -> const lightusd::PhysicsJointBase*
{
    if (const lightusd::PhysicsJoint*          joint = prim.as<lightusd::PhysicsJoint         >(); joint != nullptr) { return joint; }
    if (const lightusd::PhysicsRevoluteJoint*  joint = prim.as<lightusd::PhysicsRevoluteJoint >(); joint != nullptr) { return joint; }
    if (const lightusd::PhysicsPrismaticJoint* joint = prim.as<lightusd::PhysicsPrismaticJoint>(); joint != nullptr) { return joint; }
    if (const lightusd::PhysicsSphericalJoint* joint = prim.as<lightusd::PhysicsSphericalJoint>(); joint != nullptr) { return joint; }
    if (const lightusd::PhysicsFixedJoint*     joint = prim.as<lightusd::PhysicsFixedJoint    >(); joint != nullptr) { return joint; }
    if (const lightusd::PhysicsDistanceJoint*  joint = prim.as<lightusd::PhysicsDistanceJoint >(); joint != nullptr) { return joint; }
    return nullptr;
}

[[nodiscard]] auto to_glm(const lightusd::value::point3f& value) -> glm::vec3
{
    return glm::vec3{value[0], value[1], value[2]};
}

[[nodiscard]] auto to_glm(const lightusd::value::quatf& value) -> glm::quat
{
    return glm::quat{value.real, value.imag[0], value.imag[1], value.imag[2]};
}

[[nodiscard]] auto to_strings(const std::vector<lightusd::Path>& paths) -> std::vector<std::string>
{
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const lightusd::Path& path : paths) {
        out.push_back(path.full_path_name());
    }
    return out;
}

// One prim of the stage, with the absolute path it sits at and the index of
// the prim holding it.
class Prim_entry final
{
public:
    std::string           path;
    const lightusd::Prim* prim  {nullptr};
    std::size_t           parent{c_no_index};
    std::string           type_name;
};

class Physics_reader final
{
public:
    Physics_reader(const Usd_physics_read_arguments& arguments, Usd_data& data, std::vector<std::string>& warnings)
        : m_arguments{arguments}
        , m_data     {data}
        , m_warnings {warnings}
    {
    }

    void read()
    {
        ERHE_PROFILE_FUNCTION();

        collect_prims();
        read_scene();
        read_materials();
        read_collision_groups();
        read_bodies();
        read_joint_settings_prims();
        read_joints();
        report();
    }

private:
    void collect_prims()
    {
        for (const lightusd::Prim& prim : m_arguments.stage.root_prims()) {
            collect_prim(prim, std::string{}, c_no_index);
        }
    }

    void collect_prim(const lightusd::Prim& prim, const std::string& parent_path, const std::size_t parent)
    {
        const std::string path  = parent_path + "/" + std::string{prim.element_name()};
        const std::size_t index = m_prims.size();
        m_prims.push_back(
            Prim_entry{
                .path      = path,
                .prim      = &prim,
                .parent    = parent,
                .type_name = (prim.type_name() == "Model") ? prim.prim_type_name() : prim.type_name()
            }
        );
        for (const lightusd::Prim& child : prim.children()) {
            collect_prim(child, path, index);
        }
    }

    [[nodiscard]] auto has_api_schema(const lightusd::Prim& prim, const lightusd::APISchemas::APIName name) const -> bool
    {
        if (!prim.metas().has_apiSchemas()) {
            return false;
        }
        const lightusd::APISchemas api_schemas = prim.metas().get_apiSchemas();
        for (const std::pair<lightusd::APISchemas::APIName, std::string>& entry : api_schemas.names) {
            if (entry.first == name) {
                return true;
            }
        }
        return false;
    }

    // The instance names of one multi-apply API schema, in the order the prim
    // lists them.
    [[nodiscard]] auto api_schema_instances(
        const lightusd::Prim&               prim,
        const lightusd::APISchemas::APIName name
    ) const -> std::vector<std::string>
    {
        std::vector<std::string> instances;
        if (!prim.metas().has_apiSchemas()) {
            return instances;
        }
        const lightusd::APISchemas api_schemas = prim.metas().get_apiSchemas();
        for (const std::pair<lightusd::APISchemas::APIName, std::string>& entry : api_schemas.names) {
            if ((entry.first == name) && !entry.second.empty()) {
                instances.push_back(entry.second);
            }
        }
        return instances;
    }

    void add_warning(const std::string& text)
    {
        log_usd->warn("{}", text);
        m_warnings.push_back(text);
    }

    // Every `erhe:<Owner>:<name>` custom attribute of one prim as a property
    // assignment of the record, minus the names the neutral record carries in
    // fields of its own.
    void read_erhe_properties(
        const Prim_entry&                  entry,
        const std::set<std::string>&       own_fields,
        std::vector<Usd_physics_property>& out_properties
    ) const
    {
        const Property_map* props = prim_props(*entry.prim);
        if (props == nullptr) {
            return;
        }
        static constexpr std::string_view prefix{"erhe:"};
        for (const std::pair<const std::string, lightusd::Property>& property : *props) {
            const std::string& name = property.first;
            if (name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            if (own_fields.count(name) != 0) {
                continue;
            }
            const lightusd::Attribute* attribute = property.second.get_attribute_or_null();
            if (attribute == nullptr) {
                continue;
            }
            // USD namespace form to erhe qualified form: the first `:` after
            // the `erhe` component separates owner from property. A name with
            // one component is one of the joint-limit attributes, which the
            // limit read takes and this one leaves; one with three is not an
            // erhe property name at all.
            std::string       qualified_name = name.substr(prefix.size());
            const std::size_t separator      = qualified_name.find(':');
            if (separator == std::string::npos) {
                continue;
            }
            qualified_name[separator] = '.';
            if (qualified_name.find(':') != std::string::npos) {
                continue;
            }
            out_properties.push_back(
                Usd_physics_property{
                    .name     = qualified_name,
                    .usd_type = attribute->type_name(),
                    .value    = usd_literal_to_property_text(lightusd::value::pprint_value(attribute->get_var().value_raw()))
                }
            );
        }
    }

    void read_scene()
    {
        for (const Prim_entry& entry : m_prims) {
            if (entry.type_name != c_physics_scene_prim_type_name) {
                continue;
            }
            Usd_physics_scene& scene = m_data.physics_prims.scene;
            if (scene.present) {
                add_warning(
                    fmt::format(
                        "USD '{}': prim '{}' is a second PhysicsScene - the gravity of the first one, '{}', is the scene's",
                        m_arguments.file_name, entry.path, scene.stage_path
                    )
                );
                continue;
            }
            scene.present    = true;
            scene.stage_path = entry.path;
            const lightusd::PhysicsScene* usd_scene = entry.prim->as<lightusd::PhysicsScene>();
            if (usd_scene == nullptr) {
                continue;
            }
            lightusd::value::vector3f direction{0.0f, -1.0f, 0.0f};
            if (usd_scene->gravityDirection.get_value(&direction)) {
                scene.gravity_direction = glm::vec3{direction[0], direction[1], direction[2]};
            }
            float magnitude = 0.0f;
            if (usd_scene->gravityMagnitude.get_value(&magnitude)) {
                scene.gravity_magnitude = magnitude;
            }
        }
    }

    void read_materials()
    {
        for (const Prim_entry& entry : m_prims) {
            if (!has_api_schema(*entry.prim, lightusd::APISchemas::APIName::PhysicsMaterialAPI)) {
                continue;
            }
            const Property_map*                       props = prim_props(*entry.prim);
            erhe::scene::Physics_material_description material{};
            material.name = std::string{entry.prim->element_name()};
            float value = 0.0f;
            if (read_float(props, "physics:staticFriction",  value)) { material.static_friction  = value; }
            if (read_float(props, "physics:dynamicFriction", value)) { material.dynamic_friction = value; }
            if (read_float(props, "physics:restitution",     value)) { material.restitution      = value; }

            Usd_physics_record record{};
            record.stage_path = entry.path;
            if (read_float(props, "physics:density", value)) {
                record.properties.push_back(
                    Usd_physics_property{
                        .name     = std::string{"Physics_material.density"},
                        .usd_type = std::string{"float"},
                        .value    = fmt::format("{}", value)
                    }
                );
            }
            read_erhe_properties(entry, m_material_own_fields, record.properties);
            apply_combine_modes(record, material);

            m_material_index_by_path.emplace(entry.path, m_data.physics.materials.size());
            m_data.physics.materials.push_back(std::move(material));
            m_data.physics_prims.materials.push_back(std::move(record));
        }
    }

    // The two combine modes the neutral record carries: they arrive as erhe
    // custom attributes, so they leave the property list for their fields.
    static void apply_combine_modes(Usd_physics_record& record, erhe::scene::Physics_material_description& material)
    {
        std::vector<Usd_physics_property> kept;
        for (Usd_physics_property& property : record.properties) {
            if (property.name == "Physics_material.friction_combine") {
                material.friction_combine = to_combine_mode(property.value);
                continue;
            }
            if (property.name == "Physics_material.restitution_combine") {
                material.restitution_combine = to_combine_mode(property.value);
                continue;
            }
            kept.push_back(std::move(property));
        }
        record.properties = std::move(kept);
    }

    [[nodiscard]] static auto to_combine_mode(const std::string& text) -> erhe::scene::Physics_combine_mode
    {
        if (text == "minimum")  { return erhe::scene::Physics_combine_mode::e_minimum;  }
        if (text == "maximum")  { return erhe::scene::Physics_combine_mode::e_maximum;  }
        if (text == "multiply") { return erhe::scene::Physics_combine_mode::e_multiply; }
        return erhe::scene::Physics_combine_mode::e_average;
    }

    void read_collision_groups()
    {
        for (const Prim_entry& entry : m_prims) {
            if (entry.type_name != c_physics_collision_group_prim_type_name) {
                continue;
            }
            const Property_map*                              props = prim_props(*entry.prim);
            erhe::scene::Physics_collision_filter_description filter{};
            filter.name = std::string{entry.prim->element_name()};

            const bool has_systems     = read_string_array(props, std::string{c_collision_filter_systems_attribute},     filter.collision_systems);
            const bool has_collide     = read_string_array(props, std::string{c_collision_filter_collide_attribute},     filter.collide_with_systems);
            const bool has_not_collide = read_string_array(props, std::string{c_collision_filter_not_collide_attribute}, filter.not_collide_with_systems);
            if (!has_systems && !has_collide && !has_not_collide) {
                // A group another application wrote: the group is the one
                // system its members are in, and `filteredGroups` names the
                // systems they do not collide with - or the only ones they do,
                // when the group inverts the list.
                filter.collision_systems.push_back(filter.name);
                const lightusd::PhysicsCollisionGroup* group = entry.prim->as<lightusd::PhysicsCollisionGroup>();
                const std::vector<std::string> filtered_group_paths = (group != nullptr)
                    ? to_strings(group->filteredGroups.get_targetPaths())
                    : read_paths(entry, "physics:filteredGroups");
                const bool invert = (group != nullptr) && group->invertFilteredGroups.get_value();
                std::vector<std::string>& targets = invert ? filter.collide_with_systems : filter.not_collide_with_systems;
                for (const std::string& path : filtered_group_paths) {
                    targets.push_back(leaf_name(path));
                }
            }

            const std::size_t              filter_index = m_data.physics.collision_filters.size();
            const std::vector<std::string> includes     = read_paths(entry, std::string{c_physics_colliders_includes});
            for (const std::string& included : includes) {
                m_filter_index_by_path.emplace(included, filter_index);
            }

            Usd_physics_record record{};
            record.stage_path = entry.path;
            read_erhe_properties(entry, m_filter_own_fields, record.properties);

            m_data.physics.collision_filters.push_back(std::move(filter));
            m_data.physics_prims.collision_filters.push_back(std::move(record));
        }
    }

    [[nodiscard]] static auto leaf_name(const std::string& path) -> std::string
    {
        const std::size_t separator = path.rfind('/');
        return (separator == std::string::npos) ? path : path.substr(separator + 1);
    }

    void read_bodies()
    {
        for (const Prim_entry& entry : m_prims) {
            const bool is_body     = has_api_schema(*entry.prim, lightusd::APISchemas::APIName::PhysicsRigidBodyAPI);
            const bool is_collider = has_api_schema(*entry.prim, lightusd::APISchemas::APIName::PhysicsCollisionAPI);
            if (!is_body && !is_collider) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Node> node = find_node(entry.path);
            if (!node) {
                add_warning(
                    fmt::format(
                        "USD '{}': physics prim '{}' has no prim in the scene tree - its body is not imported",
                        m_arguments.file_name, entry.path
                    )
                );
                continue;
            }
            const std::size_t description_index = ensure_node_entry(entry.path, node);
            if (is_body) {
                read_motion(entry, m_data.physics.node_physics[description_index], m_data.physics_prims.bodies[description_index]);
            }
            if (is_collider) {
                read_collider(entry, m_data.physics.node_physics[description_index]);
            }
            apply_trigger(entry, m_data.physics.node_physics[description_index]);
        }
    }

    [[nodiscard]] auto ensure_node_entry(const std::string& path, const std::shared_ptr<erhe::scene::Node>& node) -> std::size_t
    {
        const std::map<std::string, std::size_t>::const_iterator i = m_node_entry_index_by_path.find(path);
        if (i != m_node_entry_index_by_path.end()) {
            return i->second;
        }
        const std::size_t                     index = m_data.physics.node_physics.size();
        erhe::scene::Physics_node_description description{};
        description.node = node;
        m_data.physics.node_physics.push_back(std::move(description));
        m_data.physics_prims.bodies.push_back(Usd_physics_record{.stage_path = path});
        m_node_entry_index_by_path.emplace(path, index);
        return index;
    }

    void read_motion(
        const Prim_entry&                      entry,
        erhe::scene::Physics_node_description& description,
        Usd_physics_record&                    record
    )
    {
        const Property_map* props   = prim_props(*entry.prim);
        bool                enabled = true;
        static_cast<void>(read_bool(props, "physics:rigidBodyEnabled", enabled));
        read_erhe_properties(entry, m_body_own_fields, record.properties);
        if (!enabled) {
            // A disabled rigid body is a static body: the prim keeps its
            // colliders and has no motion.
            return;
        }
        erhe::scene::Physics_node_motion motion{};
        static_cast<void>(read_bool(props, "physics:kinematicEnabled", motion.is_kinematic));
        float mass = 0.0f;
        if (read_float(props, "physics:mass", mass) && (mass != 0.0f)) {
            // The schema default 0 says the body computes its mass, which is
            // erhe's density-derived mass: the record leaves it unset.
            motion.mass = mass;
        }
        glm::vec3 center_of_mass{0.0f};
        if (read_vec3(props, "physics:centerOfMass", center_of_mass) && std::isfinite(center_of_mass.x)) {
            // The schema default is -inf on every component, which says the
            // body computes its own center of mass.
            motion.center_of_mass = center_of_mass;
        }
        glm::vec3 inertia{0.0f};
        if (read_vec3(props, "physics:diagonalInertia", inertia) && (inertia != glm::vec3{0.0f})) {
            motion.inertia_diagonal = inertia;
            glm::quat principal_axes{1.0f, 0.0f, 0.0f, 0.0f};
            if (read_quat(props, "physics:principalAxes", principal_axes)) {
                motion.inertia_orientation = principal_axes;
            }
        }
        glm::vec3 velocity{0.0f};
        if (read_vec3(props, "physics:velocity", velocity)) {
            motion.linear_velocity = velocity;
        }
        glm::vec3 angular_velocity{0.0f};
        if (read_vec3(props, "physics:angularVelocity", angular_velocity)) {
            motion.angular_velocity = glm::vec3{
                to_radians(angular_velocity.x),
                to_radians(angular_velocity.y),
                to_radians(angular_velocity.z)
            };
        }
        float gravity_factor = 1.0f;
        if (read_float(props, std::string{c_node_physics_gravity_factor_attribute}, gravity_factor)) {
            motion.gravity_factor = gravity_factor;
        }
        description.motion = motion;
    }

    // A body the file marks as a trigger detects overlaps rather than
    // colliding, which the neutral description states as a trigger rather
    // than as a collider: the shapes are the same and the body is not
    // simulated against. The colliders below such a body fold onto it the way
    // a plain body's do.
    void apply_trigger(const Prim_entry& entry, erhe::scene::Physics_node_description& description)
    {
        bool is_trigger = false;
        if (!read_bool(prim_props(*entry.prim), std::string{c_node_physics_is_trigger_attribute}, is_trigger) || !is_trigger) {
            return;
        }
        erhe::scene::Physics_node_trigger trigger{};
        if (description.collider.has_value()) {
            trigger.geometry     = description.collider.value().geometry;
            trigger.filter_index = description.collider.value().filter_index;
            description.collider.reset();
        }
        description.trigger = std::move(trigger);
    }

    void read_collider(const Prim_entry& entry, erhe::scene::Physics_node_description& description)
    {
        erhe::scene::Physics_node_collider collider{};
        if (entry.type_name == "Mesh") {
            const std::map<std::string, std::shared_ptr<erhe::scene::Mesh>>::const_iterator i =
                m_arguments.meshes_by_path.find(entry.path);
            if (i == m_arguments.meshes_by_path.end()) {
                add_warning(
                    fmt::format(
                        "USD '{}': collider prim '{}' has no converted mesh - it becomes no collider",
                        m_arguments.file_name, entry.path
                    )
                );
                return;
            }
            collider.geometry.mesh        = i->second;
            collider.geometry.convex_hull = read_mesh_approximation(entry);
        } else if (is_primitive_schema_prim_type(entry.type_name)) {
            const std::optional<std::size_t> shape_index = read_shape(entry);
            if (!shape_index.has_value()) {
                return;
            }
            collider.geometry.shape_index = shape_index;
            if (is_guide_prim(*entry.prim)) {
                m_data.physics_prims.guide_collider_prims.push_back(find_node(entry.path));
            }
        } else {
            add_warning(
                fmt::format(
                    "USD '{}': collider prim '{}' of type '{}' names no collision shape - it becomes no collider",
                    m_arguments.file_name, entry.path, entry.type_name
                )
            );
            return;
        }
        collider.material_index = find_material_index(entry);
        collider.filter_index   = find_filter_index(entry);
        description.collider    = std::move(collider);
    }

    [[nodiscard]] auto read_mesh_approximation(const Prim_entry& entry) -> bool
    {
        std::string approximation;
        if (!read_token(prim_props(*entry.prim), "physics:approximation", approximation) || (approximation == "none")) {
            return false;
        }
        if (approximation != "convexHull") {
            add_warning(
                fmt::format(
                    "USD '{}': collider prim '{}' asks for approximation '{}' - it becomes a convex hull",
                    m_arguments.file_name, entry.path, approximation
                )
            );
        }
        return true;
    }

    // The implicit shape one primitive-schema collider prim describes, as an
    // index into `Physics_description::shapes`. The shapes are Y-aligned and
    // centered on the origin on both sides, so the dimensions and the prim's
    // own scale are what travel: USD states a box of unequal extents as a
    // unit `Cube` scaled per axis, which is the form its own physics tooling
    // authors, and the erhe shape is the scaled size.
    [[nodiscard]] auto read_shape(const Prim_entry& entry) -> std::optional<std::size_t>
    {
        const lightusd::Prim&      prim = *entry.prim;
        erhe::scene::Physics_shape shape{};
        std::string                axis_name{"Y"};
        if (entry.type_name == "Cube") {
            const lightusd::GeomCube* cube = prim.as<lightusd::GeomCube>();
            if (cube == nullptr) {
                return {};
            }
            const float size = static_cast<float>(read_schema_double(cube->size, 2.0));
            shape.type = erhe::scene::Physics_shape_type::e_box;
            shape.size = glm::vec3{size, size, size};
        } else if (entry.type_name == "Sphere") {
            const lightusd::GeomSphere* sphere = prim.as<lightusd::GeomSphere>();
            if (sphere == nullptr) {
                return {};
            }
            shape.type   = erhe::scene::Physics_shape_type::e_sphere;
            shape.radius = static_cast<float>(read_schema_double(sphere->radius, 1.0));
        } else if (entry.type_name == "Capsule") {
            const lightusd::GeomCapsule* capsule = prim.as<lightusd::GeomCapsule>();
            if (capsule == nullptr) {
                return {};
            }
            // The schema fallbacks of `Capsule` are radius 0.5 and height 1,
            // and USD's height is the distance between the centers of the
            // capping spheres, which is the erhe capsule height.
            const float radius = static_cast<float>(read_schema_double(capsule->radius, 0.5));
            shape.type          = erhe::scene::Physics_shape_type::e_capsule;
            shape.height        = static_cast<float>(read_schema_double(capsule->height, 1.0));
            shape.radius_bottom = radius;
            shape.radius_top    = radius;
            axis_name           = to_axis_name(capsule->axis.get_value());
        } else if (entry.type_name == "Capsule_1") {
            const lightusd::GeomCapsule_1* capsule = prim.as<lightusd::GeomCapsule_1>();
            if (capsule == nullptr) {
                return {};
            }
            shape.type          = erhe::scene::Physics_shape_type::e_capsule;
            shape.height        = static_cast<float>(read_schema_double(capsule->height,       1.0));
            shape.radius_bottom = static_cast<float>(read_schema_double(capsule->radiusBottom, 0.5));
            shape.radius_top    = static_cast<float>(read_schema_double(capsule->radiusTop,    0.5));
            axis_name           = to_axis_name(capsule->axis.get_value());
        } else if (entry.type_name == "Cylinder") {
            const lightusd::GeomCylinder* cylinder = prim.as<lightusd::GeomCylinder>();
            if (cylinder == nullptr) {
                return {};
            }
            const float radius = static_cast<float>(read_schema_double(cylinder->radius, 1.0));
            shape.type          = erhe::scene::Physics_shape_type::e_cylinder;
            shape.height        = static_cast<float>(read_schema_double(cylinder->height, 2.0));
            shape.radius_bottom = radius;
            shape.radius_top    = radius;
            axis_name           = to_axis_name(cylinder->axis.get_value());
        } else if (entry.type_name == "Cylinder_1") {
            const lightusd::GeomCylinder_1* cylinder = prim.as<lightusd::GeomCylinder_1>();
            if (cylinder == nullptr) {
                return {};
            }
            shape.type          = erhe::scene::Physics_shape_type::e_cylinder;
            shape.height        = static_cast<float>(read_schema_double(cylinder->height,       2.0));
            shape.radius_bottom = static_cast<float>(read_schema_double(cylinder->radiusBottom, 1.0));
            shape.radius_top    = static_cast<float>(read_schema_double(cylinder->radiusTop,    1.0));
            axis_name           = to_axis_name(cylinder->axis.get_value());
        } else {
            add_warning(
                fmt::format(
                    "USD '{}': collider prim '{}' of type '{}' is outside the shapes erhe simulates - it becomes no collider",
                    m_arguments.file_name, entry.path, entry.type_name
                )
            );
            return {};
        }
        if (!axis_name.empty() && (axis_name != "Y")) {
            add_warning(
                fmt::format(
                    "USD '{}': collider prim '{}' is aligned along {} - its collision shape is aligned along Y",
                    m_arguments.file_name, entry.path, axis_name
                )
            );
        }
        const Property_map* props  = prim_props(prim);
        float               radius = 0.0f;
        if (read_float(props, std::string{c_physics_shape_radius_bottom_attribute}, radius)) { shape.radius_bottom = radius; }
        if (read_float(props, std::string{c_physics_shape_radius_top_attribute   }, radius)) { shape.radius_top    = radius; }
        m_data.physics.shapes.push_back(shape);
        return m_data.physics.shapes.size() - 1;
    }

    // The physics material bound to one collider prim: its own
    // `material:binding:physics`, else the one an ancestor up to and
    // including its body prim binds.
    [[nodiscard]] auto find_material_index(const Prim_entry& entry) const -> std::optional<std::size_t>
    {
        const Prim_entry* current = &entry;
        while (current != nullptr) {
            const std::vector<std::string> paths = read_paths(*current, std::string{c_physics_material_binding});
            if (!paths.empty()) {
                const std::map<std::string, std::size_t>::const_iterator i = m_material_index_by_path.find(paths.front());
                if (i != m_material_index_by_path.end()) {
                    return i->second;
                }
            }
            if ((current != &entry) && has_api_schema(*current->prim, lightusd::APISchemas::APIName::PhysicsRigidBodyAPI)) {
                break;
            }
            current = parent_of(*current);
        }
        return {};
    }

    // The collision filter of one collider prim: the group whose
    // `collection:colliders:includes` names it, or names the body prim it
    // belongs to.
    [[nodiscard]] auto find_filter_index(const Prim_entry& entry) const -> std::optional<std::size_t>
    {
        const Prim_entry* current = &entry;
        while (current != nullptr) {
            const std::map<std::string, std::size_t>::const_iterator i = m_filter_index_by_path.find(current->path);
            if (i != m_filter_index_by_path.end()) {
                return i->second;
            }
            if ((current != &entry) && has_api_schema(*current->prim, lightusd::APISchemas::APIName::PhysicsRigidBodyAPI)) {
                break;
            }
            current = parent_of(*current);
        }
        return {};
    }

    [[nodiscard]] auto parent_of(const Prim_entry& entry) const -> const Prim_entry*
    {
        return (entry.parent == c_no_index) ? nullptr : &m_prims[entry.parent];
    }

    // The paths one relationship of a prim names. LightUSD's prim
    // reconstruction keeps an applied API schema's attributes on the prim and
    // drops its relationships, so the composed layer's prim spec - which
    // carries what the file spells - answers first, and the prim itself for a
    // stage that kept no layer.
    [[nodiscard]] auto read_paths(const Prim_entry& entry, const std::string& name) const -> std::vector<std::string>
    {
        std::vector<std::string> paths;
        if (m_arguments.layer != nullptr) {
            const lightusd::PrimSpec* spec  = nullptr;
            std::string               error;
            if (m_arguments.layer->find_primspec_at(lightusd::Path{entry.path, ""}, &spec, &error) && (spec != nullptr)) {
                const Property_map::const_iterator i = spec->props().find(name);
                if (i != spec->props().end()) {
                    read_relationship_paths(i->second.get_relationship_or_null(), paths);
                    if (!paths.empty()) {
                        return paths;
                    }
                }
            }
        }
        read_relationship_paths(find_relationship(prim_props(*entry.prim), name), paths);
        return paths;
    }

    [[nodiscard]] auto find_node(const std::string& path) const -> std::shared_ptr<erhe::scene::Node>
    {
        const std::map<std::string, std::shared_ptr<erhe::scene::Node>>::const_iterator i = m_arguments.nodes_by_path.find(path);
        return (i == m_arguments.nodes_by_path.end()) ? std::shared_ptr<erhe::scene::Node>{} : i->second;
    }

    // The limits and drives one prim applies through the multi-apply
    // `PhysicsLimitAPI:<axis>` and `PhysicsDriveAPI:<axis>` schemas.
    void read_limits_and_drives(const Prim_entry& entry, erhe::scene::Physics_joint_description& joint) const
    {
        const Property_map*      props = prim_props(*entry.prim);
        std::vector<Limit_entry> limits;
        for (const std::string& instance : api_schema_instances(*entry.prim, lightusd::APISchemas::APIName::PhysicsLimitAPI)) {
            const std::string prefix = std::string{c_physics_limit_prefix} + instance + ":";
            Limit_entry       limit{};
            limit.dof = to_dof(instance);
            float value = 0.0f;
            if (read_float(props, prefix + "low", value) && std::isfinite(value)) {
                limit.min = limit.dof.is_linear ? value : to_radians(value);
            }
            if (read_float(props, prefix + "high", value) && std::isfinite(value)) {
                limit.max = limit.dof.is_linear ? value : to_radians(value);
            }
            const std::string erhe_prefix = std::string{c_physics_limit_erhe_prefix} + instance;
            if (read_float(props, erhe_prefix + std::string{c_physics_limit_stiffness_suffix}, value)) {
                limit.stiffness = value;
            }
            if (read_float(props, erhe_prefix + std::string{c_physics_limit_damping_suffix}, value)) {
                limit.damping = value;
            }
            limits.push_back(limit);
        }
        joint.limits = join_limits(limits);

        for (const std::string& instance : api_schema_instances(*entry.prim, lightusd::APISchemas::APIName::PhysicsDriveAPI)) {
            const std::string                prefix = std::string{c_physics_drive_prefix} + instance + ":";
            const Dof                        dof    = to_dof(instance);
            erhe::scene::Physics_joint_drive drive{};
            drive.type = dof.is_linear ? erhe::scene::Physics_drive_type::e_linear : erhe::scene::Physics_drive_type::e_angular;
            drive.axis = dof.axis;
            std::string type_token;
            if (read_token(props, prefix + "type", type_token) && (type_token == "acceleration")) {
                drive.mode = erhe::scene::Physics_drive_mode::e_acceleration;
            }
            float value = 0.0f;
            if (read_float(props, prefix + "maxForce",       value)) { drive.max_force       = value; }
            if (read_float(props, prefix + "targetPosition", value)) { drive.position_target = dof.is_linear ? value : to_radians(value); }
            if (read_float(props, prefix + "targetVelocity", value)) { drive.velocity_target = dof.is_linear ? value : to_radians(value); }
            // A drive's spring constants are per USD unit of the axis, which
            // for an angular axis is a degree, and erhe applies them per
            // radian: the numbers travel as they are
            // (src/erhe/usd/notes.md, "Physics").
            if (read_float(props, prefix + "stiffness", value)) { drive.stiffness = value; }
            if (read_float(props, prefix + "damping",   value)) { drive.damping   = value; }
            joint.drives.push_back(drive);
        }
    }

    // A prim that applies limit or drive instances and is no joint itself is
    // a joint-settings item the joints naming it share.
    void read_joint_settings_prims()
    {
        for (const Prim_entry& entry : m_prims) {
            if (is_joint_prim_type(entry.type_name)) {
                continue;
            }
            const bool has_limits = !api_schema_instances(*entry.prim, lightusd::APISchemas::APIName::PhysicsLimitAPI).empty();
            const bool has_drives = !api_schema_instances(*entry.prim, lightusd::APISchemas::APIName::PhysicsDriveAPI).empty();
            if (!has_limits && !has_drives) {
                continue;
            }
            erhe::scene::Physics_joint_description joint{};
            joint.name = std::string{entry.prim->element_name()};
            read_limits_and_drives(entry, joint);

            Usd_physics_record record{};
            record.stage_path = entry.path;
            read_erhe_properties(entry, m_joint_settings_own_fields, record.properties);

            m_joint_settings_index_by_path.emplace(entry.path, m_data.physics.joints.size());
            m_data.physics.joints.push_back(std::move(joint));
            m_data.physics_prims.joint_settings.push_back(std::move(record));
        }
    }

    void read_joints()
    {
        for (const Prim_entry& entry : m_prims) {
            if (!is_joint_prim_type(entry.type_name)) {
                continue;
            }
            const Property_map*                props = prim_props(*entry.prim);
            const lightusd::PhysicsJointBase*  base  = joint_base_of(*entry.prim);
            std::vector<std::string> body0_paths = (base != nullptr) ? to_strings(base->body0.get_targetPaths()) : std::vector<std::string>{};
            std::vector<std::string> body1_paths = (base != nullptr) ? to_strings(base->body1.get_targetPaths()) : std::vector<std::string>{};
            if (body0_paths.empty()) { body0_paths = read_paths(entry, "physics:body0"); }
            if (body1_paths.empty()) { body1_paths = read_paths(entry, "physics:body1"); }
            const Prim_entry* parent     = parent_of(entry);
            const std::string body0_path = body0_paths.empty()
                ? ((parent != nullptr) ? parent->path : std::string{})
                : body0_paths.front();
            const std::shared_ptr<erhe::scene::Node> node0 = find_node(body0_path);
            if (!node0) {
                add_warning(
                    fmt::format(
                        "USD '{}': joint prim '{}' names no prim of the scene tree as its first body - it becomes no joint",
                        m_arguments.file_name, entry.path
                    )
                );
                continue;
            }
            const std::shared_ptr<erhe::scene::Node> node1 = body1_paths.empty()
                ? std::shared_ptr<erhe::scene::Node>{}
                : find_node(body1_paths.front());
            if (!body1_paths.empty() && !node1) {
                add_warning(
                    fmt::format(
                        "USD '{}': joint prim '{}' names '{}' as its second body, which is no prim of the scene tree",
                        m_arguments.file_name, entry.path, body1_paths.front()
                    )
                );
            }
            // The two frames the joint prim authors are two nodes: erhe takes
            // a joint's frames from the transforms of the node it sits on and
            // of the node it names, so a frame of its own is a node of its
            // own below the body (doc/usd_compatibility.md, "Physics").
            std::string                             joint_node_path = body0_path;
            const std::shared_ptr<erhe::scene::Node> joint_node =
                resolve_joint_frame(entry, base, node0, body0_path, Joint_frame::first, joint_node_path);
            const std::string                       body1_path     = body1_paths.empty() ? std::string{} : body1_paths.front();
            std::string                             connected_path = body1_path;
            const std::shared_ptr<erhe::scene::Node> connected_node =
                resolve_joint_frame(entry, base, node1, body1_path, Joint_frame::second, connected_path);

            const std::size_t               joint_index = resolve_joint_settings(entry, props);
            const std::size_t               body_index  = ensure_node_entry(joint_node_path, joint_node);
            erhe::scene::Physics_node_joint node_joint{};
            node_joint.connected_node   = connected_node;
            node_joint.joint_index      = joint_index;
            node_joint.enable_collision = (base != nullptr) && base->collisionEnabled.get_value();
            m_data.physics.node_physics[body_index].joint = std::move(node_joint);
        }
    }

    // Which of a joint prim's two frames is being read: `localPos0` /
    // `localRot0`, the frame of the joint in the first body's space, or
    // `localPos1` / `localRot1`, the frame of the joint in the second body's
    // space.
    enum class Joint_frame
    {
        first,
        second
    };

    // The node one authored joint frame becomes (doc/usd_compatibility.md,
    // "Physics"): the body prim itself for an identity frame, else a frame
    // node below the body prim carrying the authored frame - the node erhe's
    // constraint then reads the frame off. A file erhe wrote already holds
    // that node as an `Xform` prim of the name the write gave it, so a
    // reload finds it rather than making a second one.
    [[nodiscard]] auto resolve_joint_frame(
        const Prim_entry&                         entry,
        const lightusd::PhysicsJointBase*         base,
        const std::shared_ptr<erhe::scene::Node>& body_node,
        const std::string&                        body_path,
        const Joint_frame                         side,
        std::string&                              out_path
    ) -> std::shared_ptr<erhe::scene::Node>
    {
        out_path = body_path;
        if ((base == nullptr) || !body_node) {
            return body_node;
        }
        const glm::vec3 translation = (side == Joint_frame::first)
            ? to_glm(base->localPos0.get_value())
            : to_glm(base->localPos1.get_value());
        const glm::quat rotation = (side == Joint_frame::first)
            ? to_glm(base->localRot0.get_value())
            : to_glm(base->localRot1.get_value());
        if (is_identity_frame(translation, rotation)) {
            return body_node;
        }
        const std::string name = std::string{entry.prim->element_name()} +
            ((side == Joint_frame::first) ? std::string{"_frame0"} : std::string{"_frame1"});
        const std::string path = body_path + "/" + name;
        const std::shared_ptr<erhe::scene::Node> existing = find_node(path);
        if (existing) {
            if (
                (existing->get_parent_node().get() == body_node.get()) &&
                frame_matches(*existing.get(), translation, rotation)
            ) {
                out_path = path;
                return existing;
            }
            add_warning(
                fmt::format(
                    "USD '{}': joint prim '{}' authors a frame below '{}', which already holds a prim named '{}' of another transform - the frame is placed on a prim of its own",
                    m_arguments.file_name, entry.path, body_path, name
                )
            );
        }
        std::shared_ptr<erhe::scene::Xform> frame = std::make_shared<erhe::scene::Xform>(name);
        frame->set_source_path(std::filesystem::path{m_arguments.file_name});
        frame->enable_flag_bits(
            erhe::Item_flags::show_in_ui |
            (body_node->get_flag_bits() & erhe::Item_flags::content)
        );
        frame->erhe::Hierarchy::set_parent(body_node);
        frame->set_parent_from_node(erhe::scene::Trs_transform{translation, rotation});
        frame->update_world_from_node();
        frame->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
        m_data.nodes.push_back(frame);
        // The name a sibling of that name already took is not the one the
        // node got (M2), so the path is read back off the node.
        out_path = body_path + "/" + frame->get_name();
        return frame;
    }

    [[nodiscard]] static auto is_identity_frame(const glm::vec3& translation, const glm::quat& rotation) -> bool
    {
        return
            (translation == glm::vec3{0.0f}) &&
            (rotation    == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    }

    // Whether a prim already sitting where a frame node would go is that
    // frame node: its local transform is the frame, up to the precision a
    // `float`-valued `localPos` / `localRot` states it in.
    [[nodiscard]] static auto frame_matches(
        const erhe::scene::Node& node,
        const glm::vec3&         translation,
        const glm::quat&         rotation
    ) -> bool
    {
        constexpr float tolerance = 1e-4f;
        const erhe::scene::Trs_transform& transform = node.parent_from_node_transform();
        if (glm::distance(transform.get_translation(), translation) > tolerance) {
            return false;
        }
        if (glm::distance(glm::abs(transform.get_scale()), glm::vec3{1.0f}) > tolerance) {
            return false;
        }
        const glm::quat node_rotation = transform.get_rotation();
        // A quaternion and its negation are one rotation.
        const float     dot           = glm::dot(node_rotation, rotation);
        return std::abs(std::abs(dot) - 1.0f) <= tolerance;
    }

    // The joint-settings item one joint prim uses: the one its
    // `erhe:Node_joint:joint_settings` relationship names, else one made from
    // the limits and drives the joint prim carries itself and from what its
    // own class states.
    [[nodiscard]] auto resolve_joint_settings(const Prim_entry& entry, const Property_map* props) -> std::size_t
    {
        const std::vector<std::string> settings_paths = read_paths(entry, std::string{c_node_joint_settings_relationship});
        if (!settings_paths.empty()) {
            const std::map<std::string, std::size_t>::const_iterator i = m_joint_settings_index_by_path.find(settings_paths.front());
            if (i != m_joint_settings_index_by_path.end()) {
                return i->second;
            }
            add_warning(
                fmt::format(
                    "USD '{}': joint prim '{}' names joint settings '{}', which carries no limits or drives - the joint's own are used",
                    m_arguments.file_name, entry.path, settings_paths.front()
                )
            );
        }
        erhe::scene::Physics_joint_description joint{};
        joint.name = std::string{entry.prim->element_name()};
        read_limits_and_drives(entry, joint);
        read_subclass_limits(entry, props, joint);

        const std::size_t index = m_data.physics.joints.size();
        m_data.physics.joints.push_back(std::move(joint));
        m_data.physics_prims.joint_settings.push_back(Usd_physics_record{.stage_path = entry.path});
        return index;
    }

    // The limits a joint subclass states through its own attributes rather
    // than through limit instances.
    void read_subclass_limits(
        const Prim_entry&                       entry,
        const Property_map*                     props,
        erhe::scene::Physics_joint_description& joint
    ) const
    {
        static_cast<void>(props);
        if (const lightusd::PhysicsRevoluteJoint* revolute = entry.prim->as<lightusd::PhysicsRevoluteJoint>(); revolute != nullptr) {
            joint.limits.push_back(
                make_axis_limit(
                    axis_index_of(token_value(revolute->axis)),
                    Limit_axis_kind::angular,
                    revolute->lowerLimit,
                    revolute->upperLimit
                )
            );
            return;
        }
        if (const lightusd::PhysicsPrismaticJoint* prismatic = entry.prim->as<lightusd::PhysicsPrismaticJoint>(); prismatic != nullptr) {
            joint.limits.push_back(
                make_axis_limit(
                    axis_index_of(token_value(prismatic->axis)),
                    Limit_axis_kind::linear,
                    prismatic->lowerLimit,
                    prismatic->upperLimit
                )
            );
            return;
        }
        if (entry.prim->as<lightusd::PhysicsFixedJoint>() != nullptr) {
            // Every degree of freedom is locked: one limit over all six axes,
            // at zero.
            joint.limits.push_back(
                erhe::scene::Physics_joint_limit{
                    .linear_axes  = std::vector<int>{0, 1, 2},
                    .angular_axes = std::vector<int>{0, 1, 2},
                    .min          = 0.0f,
                    .max          = 0.0f
                }
            );
            return;
        }
        if (const lightusd::PhysicsSphericalJoint* spherical = entry.prim->as<lightusd::PhysicsSphericalJoint>(); spherical != nullptr) {
            // The two cone angles limit the two axes beside the joint axis,
            // symmetrically around it.
            const int axis   = axis_index_of(token_value(spherical->axis));
            const int axes[2]{(axis + 1) % 3, (axis + 2) % 3};
            const lightusd::TypedAttribute<float>* cones[2]{&spherical->coneAngle0Limit, &spherical->coneAngle1Limit};
            for (int i = 0; i < 2; ++i) {
                float cone = 0.0f;
                if (cones[i]->get_value(&cone) && std::isfinite(cone) && (cone >= 0.0f)) {
                    joint.limits.push_back(
                        erhe::scene::Physics_joint_limit{
                            .angular_axes = std::vector<int>{axes[i]},
                            .min          = -to_radians(cone),
                            .max          =  to_radians(cone)
                        }
                    );
                }
            }
            return;
        }
        if (const lightusd::PhysicsDistanceJoint* distance_joint = entry.prim->as<lightusd::PhysicsDistanceJoint>(); distance_joint != nullptr) {
            erhe::scene::Physics_joint_limit limit{};
            limit.linear_axes = std::vector<int>{0, 1, 2};
            float distance = 0.0f;
            // A negative distance is USD's "not limited on this side".
            if (distance_joint->minDistance.get_value(&distance) && (distance >= 0.0f)) { limit.min = distance; }
            if (distance_joint->maxDistance.get_value(&distance) && (distance >= 0.0f)) { limit.max = distance; }
            joint.limits.push_back(limit);
        }
    }

    [[nodiscard]] static auto token_value(const lightusd::TypedAttribute<lightusd::value::token>& attribute) -> std::string
    {
        lightusd::value::token token;
        return attribute.get_value(&token) ? token.str() : std::string{"X"};
    }

    [[nodiscard]] static auto make_axis_limit(
        const int                                    axis,
        const Limit_axis_kind                        kind,
        const lightusd::TypedAttribute<float>&       lower,
        const lightusd::TypedAttribute<float>&       upper
    ) -> erhe::scene::Physics_joint_limit
    {
        erhe::scene::Physics_joint_limit limit{};
        if (kind == Limit_axis_kind::linear) {
            limit.linear_axes = std::vector<int>{axis};
        } else {
            limit.angular_axes = std::vector<int>{axis};
        }
        float value = 0.0f;
        if (lower.get_value(&value) && std::isfinite(value)) {
            limit.min = (kind == Limit_axis_kind::linear) ? value : to_radians(value);
        }
        if (upper.get_value(&value) && std::isfinite(value)) {
            limit.max = (kind == Limit_axis_kind::linear) ? value : to_radians(value);
        }
        return limit;
    }

    void report() const
    {
        const erhe::scene::Physics_description& physics = m_data.physics;
        if (
            physics.node_physics.empty()      &&
            physics.shapes.empty()            &&
            physics.materials.empty()         &&
            physics.collision_filters.empty() &&
            physics.joints.empty()            &&
            !m_data.physics_prims.scene.present
        ) {
            return;
        }
        log_usd->info(
            "USD '{}' physics: {} shapes, {} materials, {} collision filters, {} joints, {} body prims, physics scene = {}",
            m_arguments.file_name,
            physics.shapes.size(),
            physics.materials.size(),
            physics.collision_filters.size(),
            physics.joints.size(),
            physics.node_physics.size(),
            m_data.physics_prims.scene.present
        );
    }

    const Usd_physics_read_arguments& m_arguments;
    Usd_data&                         m_data;
    std::vector<std::string>&         m_warnings;
    std::vector<Prim_entry>           m_prims;
    std::map<std::string, std::size_t> m_material_index_by_path;
    std::map<std::string, std::size_t> m_filter_index_by_path;
    std::map<std::string, std::size_t> m_node_entry_index_by_path;
    std::map<std::string, std::size_t> m_joint_settings_index_by_path;
    // The custom attributes each record reads into a field of its own, and
    // which are then not property assignments of the record.
    const std::set<std::string> m_body_own_fields{
        std::string{c_node_physics_gravity_factor_attribute},
        std::string{c_node_physics_is_trigger_attribute}
    };
    const std::set<std::string> m_material_own_fields{};
    const std::set<std::string> m_filter_own_fields{
        std::string{c_collision_filter_systems_attribute},
        std::string{c_collision_filter_collide_attribute},
        std::string{c_collision_filter_not_collide_attribute}
    };
    const std::set<std::string> m_joint_settings_own_fields{};
};

} // anonymous namespace

void read_usd_physics(
    const Usd_physics_read_arguments& arguments,
    Usd_data&                         data,
    std::vector<std::string>&         warnings
)
{
    ERHE_PROFILE_FUNCTION();

    Physics_reader reader{arguments, data, warnings};
    reader.read();
}

} // namespace erhe::usd
