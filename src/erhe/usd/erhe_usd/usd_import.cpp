#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"

// LightUSD headers. Together with usd.cpp this is the only place in erhe
// that includes them; everything the rest of erhe sees is in usd.hpp.
#include "lightusd.hh"
#include "core/composition-types.hh"
#include "core/prim.hh"
#include "core/prim-metas.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/scene-access.hh"
#include "value-pprint.hh"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace erhe::usd {

namespace {

using Tydra_scene     = lightusd::tydra::RenderScene;
using Tydra_node      = lightusd::tydra::Node;
using Tydra_mesh      = lightusd::tydra::RenderMesh;
using Tydra_material  = lightusd::tydra::RenderMaterial;
using Tydra_camera    = lightusd::tydra::RenderCamera;
using Tydra_light     = lightusd::tydra::RenderLight;
using Tydra_attribute = lightusd::tydra::VertexAttribute;
using Tydra_subset    = lightusd::tydra::MaterialSubset;

// Number of float components an attribute format carries. Tydra converts
// primvars to float formats; anything else is reported and skipped.
[[nodiscard]] auto attribute_component_count(const lightusd::tydra::VertexAttributeFormat format) -> std::size_t
{
    switch (format) {
        case lightusd::tydra::VertexAttributeFormat::Float: return 1;
        case lightusd::tydra::VertexAttributeFormat::Vec2:  return 2;
        case lightusd::tydra::VertexAttributeFormat::Vec3:  return 3;
        case lightusd::tydra::VertexAttributeFormat::Vec4:  return 4;
        default: return 0;
    }
}

// Which element of an attribute belongs to one polygon corner. This is the
// element table of doc/usd_compatibility.md read backwards: constant is one
// value for the mesh, uniform one per facet, vertex / varying one per
// vertex, faceVarying one per corner. An indexed primvar keeps its own
// index buffer, addressed by corner (the form Tydra produces for indexed
// faceVarying and vertex primvars alike).
[[nodiscard]] auto attribute_element_index(
    const Tydra_attribute& attribute,
    const std::size_t      vertex,
    const std::size_t      facet,
    const std::size_t      corner
) -> std::size_t
{
    switch (attribute.variability) {
        case lightusd::tydra::VertexVariability::Constant:    return 0;
        case lightusd::tydra::VertexVariability::Uniform:     return facet;
        case lightusd::tydra::VertexVariability::Varying:     return vertex;
        case lightusd::tydra::VertexVariability::Vertex:      return vertex;
        case lightusd::tydra::VertexVariability::FaceVarying: return corner;
        case lightusd::tydra::VertexVariability::Indexed:     return corner;
        default:                                             return 0;
    }
}

[[nodiscard]] auto read_attribute(const Tydra_attribute& attribute, const std::size_t element) -> glm::vec4
{
    glm::vec4         result{0.0f, 0.0f, 0.0f, 1.0f};
    const std::size_t components = attribute_component_count(attribute.format);
    if (components == 0) {
        return result;
    }
    std::size_t index = element;
    if (attribute.variability == lightusd::tydra::VertexVariability::Indexed) {
        if (element >= attribute.indices.size()) {
            return result;
        }
        index = attribute.indices[element];
    }
    const std::size_t stride = attribute.stride_bytes();
    const std::size_t offset = index * stride;
    if ((offset + (components * sizeof(float))) > attribute.data.size()) {
        return result;
    }
    const std::uint8_t* source = attribute.data.data() + offset;
    float               values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::memcpy(values, source, components * sizeof(float));
    for (std::size_t i = 0; i < components; ++i) {
        result[static_cast<glm::length_t>(i)] = values[i];
    }
    return result;
}

// A USD matrix4d is row-major with USD's row-vector convention; a glm mat4
// is column-major with the column-vector convention. The two store the same
// coefficients in the same order, so an element-for-element copy is the
// correct conversion (not a transpose).
[[nodiscard]] auto to_glm(const lightusd::value::matrix4d& matrix) -> glm::mat4
{
    glm::mat4 result{1.0f};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            result[j][i] = static_cast<float>(matrix.m[j][i]);
        }
    }
    return result;
}

[[nodiscard]] auto is_srgb_color_space(const lightusd::tydra::ColorSpace color_space) -> bool
{
    switch (color_space) {
        case lightusd::tydra::ColorSpace::sRGB:
        case lightusd::tydra::ColorSpace::sRGB_Texture:
        case lightusd::tydra::ColorSpace::sRGB_DisplayP3:
        case lightusd::tydra::ColorSpace::g22_Rec709:
        case lightusd::tydra::ColorSpace::g18_Rec709:
            return true;
        default:
            return false;
    }
}

// A USDA literal rewritten in erhe's property text form (D16): a tuple or
// array becomes space-separated components, a quoted token or string loses
// its quotes. `(1, 0.5, 0)` becomes `1 0.5 0`, `"guide"` becomes `guide`,
// `5000` stays `5000`. A string value that carries a comma or a bracket of
// its own is not representable this way and is left to fail parsing.
[[nodiscard]] auto usd_literal_to_property_text(const std::string& literal) -> std::string
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

// The USD `purpose` vocabulary, which erhe's Purpose enumeration mirrors
// token for token (doc/usd_compatibility.md, property system).
[[nodiscard]] auto to_erhe_purpose(const lightusd::Purpose purpose) -> erhe::Purpose
{
    switch (purpose) {
        case lightusd::Purpose::Default: return erhe::Purpose::default_;
        case lightusd::Purpose::Render:  return erhe::Purpose::render;
        case lightusd::Purpose::Proxy:   return erhe::Purpose::proxy;
        case lightusd::Purpose::Guide:   return erhe::Purpose::guide;
        default:                         return erhe::Purpose::default_;
    }
}

// One group of facets that shares a material: a GeomSubset with
// familyName = materialBind, or the facets no subset claims.
class Facet_group final
{
public:
    std::string               name;
    int                       material_id{-1};
    std::vector<std::uint32_t> facets;
};

class Importer final
{
public:
    Importer(const Usd_load_arguments& arguments, Usd_load_result& result)
        : m_arguments{arguments}
        , m_result   {result}
    {
    }

    void convert(const lightusd::Stage& stage)
    {
        ERHE_PROFILE_FUNCTION();

        report_skipped_physics(stage);

        lightusd::tydra::RenderSceneConverterEnv env{stage};
        env.usd_filename = m_arguments.path.generic_string();
        // Authored topology and authored primvar variability are what the
        // geometry-normative path needs, so neither triangulation nor
        // vertex-index building runs: facet counts and indices go straight
        // into geogram, and every attribute keeps the variability that
        // names its erhe element domain.
        env.mesh_config.triangulate           = false;
        env.mesh_config.build_vertex_indices  = false;
        env.mesh_config.extract_all_texcoords = true;
        // Time samples are read at the stage's default time code
        // (doc/usd-compatibility-plan.md I1); an animated attribute
        // contributes its default-time value and nothing else.
        env.timecode = lightusd::value::TimeCode::Default();
        // The search path for texture assets is the file's own directory.
        const std::string directory = m_arguments.path.parent_path().generic_string();
        if (!directory.empty()) {
            env.set_search_paths({directory});
        }

        lightusd::tydra::RenderSceneConverter converter;
        Tydra_scene                           scene;
        const bool converted = converter.ConvertToRenderScene(env, &scene);
        if (!converter.GetWarning().empty()) {
            m_result.warning = converter.GetWarning();
            log_usd->warn("USD '{}': {}", env.usd_filename, converter.GetWarning());
        }
        if (!converted) {
            m_result.error = converter.GetError().empty()
                ? std::string{"USD to render scene conversion failed"}
                : converter.GetError();
            log_usd->error("USD '{}' conversion failed: {}", env.usd_filename, m_result.error);
            return;
        }

        m_stage = &stage;
        m_scene = &scene;

        m_result.data.up_axis         = scene.meta.upAxis;
        m_result.data.meters_per_unit = scene.meta.metersPerUnit;
        read_custom_layer_data(stage);

        convert_images();
        convert_materials();
        convert_meshes();
        convert_cameras();
        convert_lights();
        convert_nodes();
        elide_default_local_values();

        log_usd->info(
            "USD '{}': {} nodes, {} meshes, {} materials, {} images, {} cameras, {} lights",
            env.usd_filename,
            m_result.data.nodes.size(),
            m_result.data.meshes.size(),
            m_result.data.materials.size(),
            m_result.data.images.size(),
            m_result.data.cameras.size(),
            m_result.data.lights.size()
        );
    }

private:
    // A local value is an authored value (doc/property-system.md D32,
    // doc/usd-compatibility-plan.md M4). The conversions above ask the
    // composed prim which attributes carry an authored opinion and write
    // only those, so this pass is the safety net for the few fields that
    // are still filled unconditionally: the light type, which comes from
    // the prim's schema type rather than from an attribute, and the erhe
    // properties an item's constructor seeds. It takes back a local value
    // that merely repeats the item's own default.
    void elide_default_local_values()
    {
        ERHE_PROFILE_FUNCTION();
        const auto elide = [](const std::shared_ptr<erhe::Item_base>& item) {
            if (item) {
                erhe::property::clear_default_valued_local_properties(*item);
            }
        };
        for (const std::shared_ptr<erhe::scene::Node>&         node     : m_result.data.nodes)     { elide(node);     }
        for (const std::shared_ptr<erhe::scene::Mesh>&         mesh     : m_result.data.meshes)    { elide(mesh);     }
        for (const std::shared_ptr<erhe::scene::Light>&        light    : m_result.data.lights)    { elide(light);    }
        for (const std::shared_ptr<erhe::scene::Camera>&       camera   : m_result.data.cameras)   { elide(camera);   }
        for (const std::shared_ptr<erhe::primitive::Material>& material : m_result.data.materials) { elide(material); }
    }

    // The root layer's `customLayerData`, string entries only: an erhe save
    // puts the editor's scene state there (doc/scene_serialization.md, USD-
    // backed scenes), and a value of any other type belongs to a writer this
    // conversion does not read.
    void read_custom_layer_data(const lightusd::Stage& stage)
    {
        for (const std::pair<const std::string, lightusd::MetaVariable>& entry : stage.metas().customLayerData) {
            const nonstd::optional<std::string> text = entry.second.get_value<std::string>();
            if (text.has_value()) {
                m_result.data.custom_layer_data.emplace(entry.first, text.value());
            }
        }
    }

    // UsdPhysics is future work (doc/usd-compatibility-plan.md section 5).
    // Say so once per file rather than silently dropping the schemas.
    void report_skipped_physics(const lightusd::Stage& stage)
    {
        std::size_t physics_prim_count = 0;
        for (const lightusd::Prim& prim : stage.root_prims()) {
            count_physics_prims(prim, physics_prim_count);
        }
        if (physics_prim_count > 0) {
            log_usd->info(
                "USD '{}': {} prim(s) carry UsdPhysics schemas - physics is not imported",
                m_arguments.path.generic_string(),
                physics_prim_count
            );
        }
    }

    void count_physics_prims(const lightusd::Prim& prim, std::size_t& count)
    {
        const std::string& type_name = prim.type_name();
        if (type_name.rfind("Physics", 0) == 0) {
            ++count;
        } else if (prim.metas().has_apiSchemas()) {
            const lightusd::APISchemas api_schemas = prim.metas().get_apiSchemas();
            for (const std::pair<lightusd::APISchemas::APIName, std::string>& entry : api_schemas.names) {
                if (is_physics_api_schema(entry.first)) {
                    ++count;
                    break;
                }
            }
        }
        for (const lightusd::Prim& child : prim.children()) {
            count_physics_prims(child, count);
        }
    }

    [[nodiscard]] static auto is_physics_api_schema(const lightusd::APISchemas::APIName name) -> bool
    {
        switch (name) {
            case lightusd::APISchemas::APIName::PhysicsRigidBodyAPI:
            case lightusd::APISchemas::APIName::PhysicsCollisionAPI:
            case lightusd::APISchemas::APIName::PhysicsMaterialAPI:
            case lightusd::APISchemas::APIName::PhysicsMeshCollisionAPI:
            case lightusd::APISchemas::APIName::PhysicsMassAPI:
            case lightusd::APISchemas::APIName::PhysicsFilteredPairsAPI:
            case lightusd::APISchemas::APIName::PhysicsArticulationRootAPI:
            case lightusd::APISchemas::APIName::PhysicsDriveAPI:
            case lightusd::APISchemas::APIName::PhysicsLimitAPI:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] auto find_prim(const std::string& absolute_path) const -> const lightusd::Prim*
    {
        if (m_stage == nullptr) {
            return nullptr;
        }
        const lightusd::Prim* prim = nullptr;
        std::string           error;
        if (!m_stage->find_prim_at_path(lightusd::Path{absolute_path, ""}, prim, &error)) {
            return nullptr;
        }
        return prim;
    }

    [[nodiscard]] auto has_api_schema(const std::string& absolute_path, const lightusd::APISchemas::APIName name) const -> bool
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if ((prim == nullptr) || !prim->metas().has_apiSchemas()) {
            return false;
        }
        const lightusd::APISchemas api_schemas = prim->metas().get_apiSchemas();
        for (const std::pair<lightusd::APISchemas::APIName, std::string>& entry : api_schemas.names) {
            if (entry.first == name) {
                return true;
            }
        }
        return false;
    }

    // The authored opinions of one prim (doc/usd-compatibility-plan.md I2).
    // Tydra's render scene reports a schema fallback exactly the way it
    // reports an authored opinion, so the composed prim is the only place
    // the two can be told apart: LightUSD's typed attribute wrappers answer
    // `authored()`, and Tydra's GetPropertyNames collects the names of the
    // builtin attributes that answer true plus every custom attribute the
    // prim carries. An opinion that arrives over a reference or a sublayer
    // is authored on the composed prim, so it is in this set. The set is
    // built once per prim: the conversion asks about several attributes of
    // the same prim.
    [[nodiscard]] auto authored_property_names(const std::string& absolute_path) -> const std::set<std::string>&
    {
        const std::map<std::string, std::set<std::string>>::iterator cached = m_authored_property_names.find(absolute_path);
        if (cached != m_authored_property_names.end()) {
            return cached->second;
        }
        std::set<std::string> names;
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim != nullptr) {
            std::vector<std::string> name_list;
            std::string              error;
            if (lightusd::tydra::GetPropertyNames(*prim, &name_list, &error)) {
                for (std::string& name : name_list) {
                    names.insert(std::move(name));
                }
            } else if (!error.empty()) {
                log_usd->warn("USD prim '{}': {}", absolute_path, error);
            }
        }
        return m_authored_property_names.emplace(absolute_path, std::move(names)).first->second;
    }

    [[nodiscard]] auto is_authored(const std::string& absolute_path, const std::string& name) -> bool
    {
        const std::set<std::string>& names = authored_property_names(absolute_path);
        return names.find(name) != names.end();
    }

    // `visibility` and `purpose` of the composed prim. Tydra's GetProperty
    // reaches a prim's own schema attributes and its custom ones, but not
    // the ones a prim inherits from GPrim or from LightAPI, so these two
    // are read from the concrete prim class. Every prim type the conversion
    // makes a node for is listed: the GPrim-derived geometry and camera
    // types, and the UsdLux types, which carry their own copies of the two
    // attributes rather than deriving from GPrim.
    template <typename T>
    [[nodiscard]] static auto read_visibility_and_purpose(
        const lightusd::Prim&  prim,
        lightusd::Visibility&  visibility,
        lightusd::Purpose&     purpose
    ) -> bool
    {
        const T* typed = prim.as<T>();
        if (typed == nullptr) {
            return false;
        }
        lightusd::Visibility           scalar_visibility = lightusd::Visibility::Inherited;
        const lightusd::Animatable<lightusd::Visibility>& animatable = typed->visibility.get_value();
        if (animatable.get_default(&scalar_visibility)) {
            visibility = scalar_visibility;
        }
        purpose = typed->purpose.get_value();
        return true;
    }

    [[nodiscard]] static auto get_visibility_and_purpose(
        const lightusd::Prim& prim,
        lightusd::Visibility& visibility,
        lightusd::Purpose&    purpose
    ) -> bool
    {
        return
            read_visibility_and_purpose<lightusd::Xform        >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomMesh     >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCamera   >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::SphereLight  >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::DistantLight >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::RectLight    >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::DiskLight    >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::CylinderLight>(prim, visibility, purpose);
    }

    // `visibility` and `purpose` land on the erhe item properties that carry
    // the same vocabulary (doc/usd-compatibility-plan.md M3). An unauthored
    // attribute writes nothing, so `visible` keeps its default and `purpose`
    // keeps the per-object default its flag bits derive (D31).
    void apply_visibility_and_purpose(const std::string& absolute_path, erhe::Item_base& item)
    {
        const bool visibility_authored = is_authored(absolute_path, "visibility");
        const bool purpose_authored    = is_authored(absolute_path, "purpose");
        if (!visibility_authored && !purpose_authored) {
            return;
        }
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return;
        }
        lightusd::Visibility visibility = lightusd::Visibility::Inherited;
        lightusd::Purpose    purpose    = lightusd::Purpose::Default;
        if (!get_visibility_and_purpose(*prim, visibility, purpose)) {
            log_usd->warn("USD prim '{}': visibility and purpose are not readable from a '{}' prim", absolute_path, prim->type_name());
            return;
        }
        if (visibility_authored) {
            item.set_value(erhe::Item_base::visible_property, visibility == lightusd::Visibility::Inherited);
        }
        if (purpose_authored) {
            item.set_value(erhe::Item_base::purpose_property, to_erhe_purpose(purpose));
        }
    }

    // A namespaced custom attribute `erhe:<Owner>:<name>` is an erhe property
    // value addressed by its qualified name (doc/property-system.md D30). USD
    // separates namespace components with `:` and reserves `.` for the
    // property separator of a path, so the erhe qualified name `Owner.name`
    // is spelled `erhe:Owner:name` on a prim (doc/usd_compatibility.md,
    // property system); `erhe:<name>` without a namespace component names a
    // property of the item's own class. The registry resolves the name
    // against each item the prim maps to - the attachment the prim's type
    // made first, then the node that carries it, which is what lets
    // `erhe:Light:color` name either the light itself or a node-held
    // attachment value. The USDA literal is converted to the property's text
    // form and parsed with the D16 `from_string` of the property's type. A
    // name that resolves to no property, and a value that fails to parse or
    // to validate, cost one warning each and are skipped.
    // The erhe property one `erhe:` attribute name addresses, and the object
    // that holds it. Each candidate object is asked in turn - for a scene
    // graph prim the attachment its type made, then the node that carries
    // it; for a Material prim the material alone - and for each,
    // `Owner.name` resolves either the way an editor holder addresses it (an
    // attached property, R7, or a secondary property the object holds for
    // another class, D30) or as `name` on an object of exactly the class
    // `Owner` names. Asking one object both ways before moving on is what
    // sends `Light.temperature` on a light prim to the light itself, and
    // `Light.color` on a plain Xform prim to the node that holds it.
    [[nodiscard]] static auto resolve_erhe_property(
        erhe::property::Dependency_object*  primary,
        erhe::property::Dependency_object*  secondary,
        const std::string&                  qualified_name,
        erhe::property::Dependency_object*& out_target
    ) -> const erhe::property::Dependency_property*
    {
        const erhe::property::Property_registry&        registry    = erhe::property::Property_registry::get();
        const std::size_t                               dot         = qualified_name.find('.');
        const std::string                               bare_name   = (dot == std::string::npos) ? qualified_name : qualified_name.substr(dot + 1);
        const std::optional<erhe::property::Owner_type> named_owner = (dot == std::string::npos)
            ? std::optional<erhe::property::Owner_type>{}
            : registry.find_owner_type(std::string_view{qualified_name}.substr(0, dot));
        if ((dot != std::string::npos) && !named_owner.has_value()) {
            return nullptr;
        }
        erhe::property::Dependency_object* const objects[2] = {primary, secondary};
        for (erhe::property::Dependency_object* const object : objects) {
            if (object == nullptr) {
                continue;
            }
            const erhe::property::Dependency_property* property = registry.find_for_object(*object, qualified_name);
            if (property == nullptr) {
                property = registry.find_for_object(*object, bare_name);
                if ((property != nullptr) && named_owner.has_value() && (property->get_owner_type() != named_owner.value())) {
                    property = nullptr;
                }
            }
            if (property != nullptr) {
                out_target = object;
                return property;
            }
        }
        return nullptr;
    }

    void apply_erhe_custom_attributes(
        const std::string&                 absolute_path,
        erhe::property::Dependency_object* primary,
        erhe::property::Dependency_object* secondary
    )
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return;
        }
        static constexpr std::string_view prefix{"erhe:"};
        const std::set<std::string>& names = authored_property_names(absolute_path);
        for (const std::string& name : names) {
            if (name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            // USD namespace form to erhe qualified form: the first `:`
            // after the `erhe` component separates owner from property.
            std::string       qualified_name = name.substr(prefix.size());
            const std::size_t separator      = qualified_name.find(':');
            if (separator != std::string::npos) {
                qualified_name[separator] = '.';
            }
            erhe::property::Dependency_object*         target   = nullptr;
            const erhe::property::Dependency_property* property = resolve_erhe_property(primary, secondary, qualified_name, target);
            if (property == nullptr) {
                log_usd->warn("USD prim '{}': custom attribute '{}' names no erhe property", absolute_path, name);
                continue;
            }
            if (property->is_read_only()) {
                log_usd->warn("USD prim '{}': erhe property '{}' is read-only", absolute_path, qualified_name);
                continue;
            }
            if (property->get_type() == erhe::property::Property_type::object) {
                log_usd->warn("USD prim '{}': erhe property '{}' is an object reference, which a custom attribute cannot name", absolute_path, qualified_name);
                continue;
            }
            lightusd::Attribute attribute;
            std::string         error;
            if (!lightusd::tydra::GetAttribute(*prim, name, &attribute, &error)) {
                log_usd->warn("USD prim '{}': custom attribute '{}' has no value: {}", absolute_path, name, error);
                continue;
            }
            const std::string literal = lightusd::value::pprint_value(attribute.get_var().value_raw());
            const std::string text    = usd_literal_to_property_text(literal);
            const std::optional<erhe::property::Property_value> value = erhe::property::parse_value(*property, text);
            if (!value.has_value()) {
                log_usd->warn("USD prim '{}': custom attribute '{}' value '{}' is not a valid {}", absolute_path, name, text, erhe::property::c_str(property->get_type()));
                continue;
            }
            std::string validation_error;
            if (!target->validate_value(*property, value.value(), validation_error)) {
                log_usd->warn("USD prim '{}': custom attribute '{}' value '{}' was rejected: {}", absolute_path, name, text, validation_error);
                continue;
            }
            target->set_value(*property, value.value());
        }
    }

    // A polygon mesh is geometry-normative when its subdivisionScheme is
    // `none`; USD's schema fallback is catmullClark, and a subdivision cage
    // is imported as a triangle soup the way glTF primitives are
    // (doc/usd-compatibility-plan.md I1).
    [[nodiscard]] auto is_geometry_normative(const std::string& absolute_path) const -> bool
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return false;
        }
        const lightusd::GeomMesh* geom_mesh = prim->as<lightusd::GeomMesh>();
        if (geom_mesh == nullptr) {
            return false;
        }
        return geom_mesh->subdivisionScheme.get_value() == lightusd::GeomMesh::SubdivisionScheme::SubdivisionSchemeNone;
    }

    void convert_images()
    {
        m_result.data.images.reserve(m_scene->images.size());
        const std::filesystem::path directory = m_arguments.path.parent_path();
        for (const lightusd::tydra::TextureImage& image : m_scene->images) {
            Usd_image usd_image{};
            // The asset identifier is the authored one: LightUSD resolves an
            // asset path only when it opens the asset, and its image loaders
            // are off. A relative path is resolved here, against the stage
            // file's own directory, so the caller receives a path it can open.
            std::filesystem::path image_path{image.asset_identifier};
            if (image_path.is_relative() && !directory.empty()) {
                image_path = (directory / image_path).lexically_normal();
            }
            usd_image.path = image_path;
            usd_image.name = usd_image.path.filename().generic_string();
            usd_image.srgb = is_srgb_color_space(image.usdColorSpace);
            m_result.data.images.push_back(usd_image);
        }
    }

    // The image behind one UsdPreviewSurface input, or no_image when the
    // input carries a plain value.
    static constexpr std::size_t no_image = ~std::size_t{0};

    [[nodiscard]] auto image_of(const std::int32_t texture_id) const -> std::size_t
    {
        if (texture_id < 0) {
            return no_image;
        }
        const std::size_t texture_index = static_cast<std::size_t>(texture_id);
        if (texture_index >= m_scene->textures.size()) {
            return no_image;
        }
        const std::int64_t image_id = m_scene->textures[texture_index].texture_image_id;
        if (image_id < 0) {
            return no_image;
        }
        const std::size_t image_index = static_cast<std::size_t>(image_id);
        if (image_index >= m_result.data.images.size()) {
            return no_image;
        }
        return image_index;
    }

    void bind_texture(const std::size_t material_index, const Usd_material_texture_slot slot, const std::int32_t texture_id)
    {
        const std::size_t image_index = image_of(texture_id);
        if (image_index == no_image) {
            return;
        }
        m_result.data.material_texture_bindings.push_back(
            Usd_material_texture_binding{material_index, slot, image_index}
        );
    }

    // The Shader prim the material's `outputs:surface` connects to: where
    // the UsdPreviewSurface inputs Tydra reports are authored (or not).
    [[nodiscard]] auto find_surface_shader_path(const std::string& material_absolute_path) -> std::string
    {
        const lightusd::Prim* prim = find_prim(material_absolute_path);
        if (prim == nullptr) {
            return {};
        }
        const lightusd::Material* usd_material = prim->as<lightusd::Material>();
        if (usd_material == nullptr) {
            return {};
        }
        const std::vector<lightusd::Path>& connections = usd_material->surface.get_connections();
        if (connections.empty()) {
            return {};
        }
        const lightusd::tstring_view prim_part = connections[0].prim_part();
        return std::string{prim_part.data(), prim_part.size()};
    }

    // Only an authored UsdPreviewSurface input becomes a local value
    // (doc/usd-compatibility-plan.md I2). An input the shader leaves at its
    // fallback writes nothing, so the erhe property keeps the ERHE default -
    // which is not the USD fallback for every input (`diffuseColor` 0.18 vs
    // erhe's white `base_color` is the one that differs most).
    void apply_preview_surface(
        const lightusd::tydra::PreviewSurfaceShader& shader,
        const std::string&                           shader_path,
        const std::size_t                            material_index,
        erhe::primitive::Material&                   material
    )
    {
        using erhe::primitive::Material;
        if (is_authored(shader_path, "inputs:diffuseColor")) {
            material.set_value(
                Material::base_color_property,
                glm::vec3{shader.diffuseColor.value[0], shader.diffuseColor.value[1], shader.diffuseColor.value[2]}
            );
        }
        if (is_authored(shader_path, "inputs:emissiveColor")) {
            material.set_value(
                Material::emissive_property,
                glm::vec3{shader.emissiveColor.value[0], shader.emissiveColor.value[1], shader.emissiveColor.value[2]}
            );
        }
        if (is_authored(shader_path, "inputs:metallic")) {
            material.set_value(Material::metallic_property, shader.metallic.value);
        }
        if (is_authored(shader_path, "inputs:roughness")) {
            // erhe's roughness is anisotropic; UsdPreviewSurface has one value.
            material.set_value(Material::roughness_property, glm::vec2{shader.roughness.value, shader.roughness.value});
        }
        if (is_authored(shader_path, "inputs:opacity")) {
            material.set_value(Material::opacity_property, shader.opacity.value);
        }
        if (is_authored(shader_path, "inputs:ior")) {
            material.set_value(Material::ior_property, shader.ior.value);
        }
        if (is_authored(shader_path, "inputs:occlusion")) {
            material.set_value(Material::occlusion_texture_strength_property, shader.occlusion.value);
        }
        // UsdPreviewSurface says opacityThreshold > 0 is a cutout and an
        // opacity below one without a threshold is blended. Both erhe
        // properties are derived, so they are written only when the input
        // they derive from is authored.
        if (is_authored(shader_path, "inputs:opacityThreshold") && (shader.opacityThreshold.value > 0.0f)) {
            material.set_value(Material::blending_mode_property, erhe::primitive::Material_blending_mode::alpha_test);
            material.set_value(Material::alpha_cutoff_property, shader.opacityThreshold.value);
        } else if (is_authored(shader_path, "inputs:opacity") && ((shader.opacity.value < 1.0f) || shader.opacity.is_texture())) {
            material.set_value(Material::blending_mode_property, erhe::primitive::Material_blending_mode::alpha_blend);
        }

        bind_texture(material_index, Usd_material_texture_slot::base_color, shader.diffuseColor.texture_id);
        bind_texture(material_index, Usd_material_texture_slot::emissive,   shader.emissiveColor.texture_id);
        bind_texture(material_index, Usd_material_texture_slot::normal,     shader.normal.texture_id);
        bind_texture(material_index, Usd_material_texture_slot::occlusion,  shader.occlusion.texture_id);
        // erhe has one metallic-roughness slot; UsdPreviewSurface reads the
        // two channels through separate texture inputs that a glTF-derived
        // file points at one image. The roughness input names it when both
        // are textured.
        const std::int32_t metallic_roughness_texture_id = (shader.roughness.texture_id >= 0)
            ? shader.roughness.texture_id
            : shader.metallic.texture_id;
        bind_texture(material_index, Usd_material_texture_slot::metallic_roughness, metallic_roughness_texture_id);
    }

    void convert_materials()
    {
        m_result.data.materials.reserve(m_scene->materials.size());
        for (std::size_t material_index = 0, end = m_scene->materials.size(); material_index < end; ++material_index) {
            const Tydra_material& usd_material = m_scene->materials[material_index];
            // The create info carries the name only: every value field of a
            // default Material_values equals the erhe default, so the new
            // material starts with no local value at all and the authored
            // inputs below are the complete local set.
            erhe::primitive::Material_create_info create_info{};
            create_info.name = usd_material.name.empty()
                ? fmt::format("material_{}", material_index)
                : usd_material.name;

            std::shared_ptr<erhe::primitive::Material> material = std::make_shared<erhe::primitive::Material>(create_info);
            material->set_source_path(m_arguments.path);
            material->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);

            if (usd_material.surfaceShader.has_value()) {
                apply_preview_surface(
                    usd_material.surfaceShader.value(),
                    find_surface_shader_path(usd_material.abs_path),
                    material_index,
                    *material.get()
                );
            } else {
                log_usd->warn(
                    "USD material '{}' has no UsdPreviewSurface shader - erhe material defaults are used",
                    create_info.name
                );
            }
            apply_erhe_custom_attributes(usd_material.abs_path, material.get(), nullptr);
            m_result.data.materials.push_back(material);
        }
    }

    [[nodiscard]] auto material_at(const int material_id) const -> std::shared_ptr<erhe::primitive::Material>
    {
        if (material_id < 0) {
            return {};
        }
        const std::size_t index = static_cast<std::size_t>(material_id);
        if (index >= m_result.data.materials.size()) {
            return {};
        }
        return m_result.data.materials[index];
    }

    // Split a mesh's facets into the groups that share a material: one per
    // materialBind GeomSubset, plus the facets no subset claims.
    [[nodiscard]] auto make_facet_groups(const Tydra_mesh& usd_mesh) const -> std::vector<Facet_group>
    {
        const std::size_t facet_count = usd_mesh.faceVertexCounts().size();
        std::vector<Facet_group> groups;
        std::vector<bool>        claimed(facet_count, false);

        for (const std::pair<const std::string, Tydra_subset>& entry : usd_mesh.material_subsetMap) {
            Facet_group group{};
            group.name        = entry.first;
            group.material_id = entry.second.material_id;
            for (const int facet : entry.second.indices()) {
                if ((facet < 0) || (static_cast<std::size_t>(facet) >= facet_count)) {
                    continue;
                }
                const std::size_t facet_index = static_cast<std::size_t>(facet);
                if (claimed[facet_index]) {
                    continue;
                }
                claimed[facet_index] = true;
                group.facets.push_back(static_cast<std::uint32_t>(facet_index));
            }
            if (!group.facets.empty()) {
                groups.push_back(std::move(group));
            }
        }

        Facet_group remainder{};
        remainder.name        = "";
        remainder.material_id = usd_mesh.material_id;
        for (std::size_t facet_index = 0; facet_index < facet_count; ++facet_index) {
            if (!claimed[facet_index]) {
                remainder.facets.push_back(static_cast<std::uint32_t>(facet_index));
            }
        }
        if (!remainder.facets.empty()) {
            groups.push_back(std::move(remainder));
        }
        return groups;
    }

    // First corner (face-vertex) index of every facet.
    [[nodiscard]] static auto make_facet_corner_offsets(const Tydra_mesh& usd_mesh) -> std::vector<std::uint32_t>
    {
        const std::vector<std::uint32_t>& counts = usd_mesh.faceVertexCounts();
        std::vector<std::uint32_t>        offsets;
        offsets.reserve(counts.size() + 1);
        std::uint32_t offset = 0;
        for (const std::uint32_t count : counts) {
            offsets.push_back(offset);
            offset += count;
        }
        offsets.push_back(offset);
        return offsets;
    }

    void set_corner_attributes(
        erhe::geometry::Geometry&  geometry,
        const Tydra_mesh&          usd_mesh,
        const GEO::index_t         corner,
        const std::size_t          usd_vertex,
        const std::size_t          usd_facet,
        const std::size_t          usd_corner
    ) const
    {
        erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
        if (!usd_mesh.normals.empty() && (attribute_component_count(usd_mesh.normals.format) >= 3)) {
            const glm::vec4 normal = read_attribute(
                usd_mesh.normals,
                attribute_element_index(usd_mesh.normals, usd_vertex, usd_facet, usd_corner)
            );
            attributes.corner_normal.set(corner, GEO::vec3f{normal.x, normal.y, normal.z});
        }
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto texcoord_i = usd_mesh.texcoords.find(static_cast<std::uint32_t>(slot));
            if (texcoord_i == usd_mesh.texcoords.end()) {
                continue;
            }
            const Tydra_attribute& texcoord = texcoord_i->second;
            if (texcoord.empty() || (attribute_component_count(texcoord.format) < 2)) {
                continue;
            }
            const glm::vec4 uv = read_attribute(
                texcoord,
                attribute_element_index(texcoord, usd_vertex, usd_facet, usd_corner)
            );
            attributes.corner_texcoord(slot).set(corner, GEO::vec2f{uv.x, uv.y});
        }
        if (!usd_mesh.vertex_colors.empty() && (attribute_component_count(usd_mesh.vertex_colors.format) >= 3)) {
            const glm::vec4 color = read_attribute(
                usd_mesh.vertex_colors,
                attribute_element_index(usd_mesh.vertex_colors, usd_vertex, usd_facet, usd_corner)
            );
            float alpha = 1.0f;
            if (!usd_mesh.vertex_opacities.empty() && (attribute_component_count(usd_mesh.vertex_opacities.format) >= 1)) {
                const glm::vec4 opacity = read_attribute(
                    usd_mesh.vertex_opacities,
                    attribute_element_index(usd_mesh.vertex_opacities, usd_vertex, usd_facet, usd_corner)
                );
                alpha = opacity.x;
            }
            attributes.corner_color_0.set(corner, GEO::vec4f{color.x, color.y, color.z, alpha});
        }
    }

    // Geometry-normative build: the authored facet counts and indices go
    // straight into geogram and the primvars become corner attributes per
    // the element table of doc/usd_compatibility.md.
    [[nodiscard]] auto build_geometry(
        const Tydra_mesh&                 usd_mesh,
        const Facet_group&                group,
        const std::vector<std::uint32_t>& facet_corner_offsets,
        const std::string&                name
    ) const -> std::shared_ptr<erhe::geometry::Geometry>
    {
        const std::vector<std::uint32_t>& counts  = usd_mesh.faceVertexCounts();
        const std::vector<std::uint32_t>& indices = usd_mesh.faceVertexIndices();

        std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(name);
        geometry->get_attributes().unbind();
        GEO::Mesh& mesh = geometry->get_mesh();
        mesh.clear(false, false);
        mesh.vertices.set_single_precision();

        // Only the vertices the group's facets use, in first-use order.
        std::unordered_map<std::uint32_t, GEO::index_t> vertex_map;
        std::vector<std::uint32_t>                      used_vertices;
        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            for (std::uint32_t local = 0; local < counts[facet]; ++local) {
                const std::uint32_t usd_vertex = indices[corner_offset + local];
                if (vertex_map.find(usd_vertex) == vertex_map.end()) {
                    vertex_map[usd_vertex] = static_cast<GEO::index_t>(used_vertices.size());
                    used_vertices.push_back(usd_vertex);
                }
            }
        }

        mesh.vertices.create_vertices(static_cast<GEO::index_t>(used_vertices.size()));
        for (std::size_t i = 0; i < used_vertices.size(); ++i) {
            const lightusd::tydra::vec3& point = usd_mesh.points[used_vertices[i]];
            float* destination = mesh.vertices.single_precision_point_ptr(static_cast<GEO::index_t>(i));
            destination[0] = point[0];
            destination[1] = point[1];
            destination[2] = point[2];
        }

        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_count  = counts[facet];
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            const GEO::index_t  new_facet     = mesh.facets.create_polygon(corner_count);
            for (std::uint32_t local = 0; local < corner_count; ++local) {
                const std::uint32_t usd_vertex = indices[corner_offset + local];
                mesh.facets.set_vertex(new_facet, local, vertex_map[usd_vertex]);
            }
        }

        geometry->get_attributes().bind();
        std::size_t new_facet_index = 0;
        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_count  = counts[facet];
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            const GEO::index_t  new_facet     = static_cast<GEO::index_t>(new_facet_index++);
            for (std::uint32_t local = 0; local < corner_count; ++local) {
                set_corner_attributes(
                    *geometry.get(),
                    usd_mesh,
                    mesh.facets.corner(new_facet, local),
                    indices[corner_offset + local],
                    facet,
                    corner_offset + local
                );
            }
        }

        // Facet adjacency, edges and the smooth vertex normals the wide-line
        // renderer needs. The same processing the editor's glTF finalize
        // pass runs for imported geometry that arrives without edges.
        geometry->process(
            {
                .flags =
                    erhe::geometry::Geometry::process_flag_connect                  |
                    erhe::geometry::Geometry::process_flag_build_edges              |
                    erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
            }
        );
        return geometry;
    }

    // Triangle-soup build for everything that is not a geometry-normative
    // polygon mesh: one vertex per polygon corner, polygons fanned into
    // triangles. Same carrier the glTF importer produces.
    [[nodiscard]] auto build_triangle_soup(
        const Tydra_mesh&                 usd_mesh,
        const Facet_group&                group,
        const std::vector<std::uint32_t>& facet_corner_offsets
    ) const -> std::shared_ptr<erhe::primitive::Triangle_soup>
    {
        using namespace erhe::dataformat;

        const std::vector<std::uint32_t>& counts  = usd_mesh.faceVertexCounts();
        const std::vector<std::uint32_t>& indices = usd_mesh.faceVertexIndices();

        std::shared_ptr<erhe::primitive::Triangle_soup> soup = std::make_shared<erhe::primitive::Triangle_soup>();
        soup->primitive_type = erhe::primitive::Primitive_type::triangles;
        soup->vertex_format.streams.emplace_back(0);
        soup->vertex_format.streams.front().emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
        const bool has_normal   = !usd_mesh.normals.empty() && (attribute_component_count(usd_mesh.normals.format) >= 3);
        const auto texcoord_i   = usd_mesh.texcoords.find(0u);
        const bool has_texcoord = (texcoord_i != usd_mesh.texcoords.end()) &&
                                  !texcoord_i->second.empty() &&
                                  (attribute_component_count(texcoord_i->second.format) >= 2);
        if (has_normal) {
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal, 0);
        }
        if (has_texcoord) {
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0);
        }
        soup->vertex_format.streams.front().emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::color, 0);
        soup->vertex_format.streams.front().finalize_stride();

        std::size_t corner_total = 0;
        for (const std::uint32_t facet : group.facets) {
            corner_total += counts[facet];
        }
        const std::size_t stride = soup->vertex_format.streams.front().stride;
        soup->vertex_data.resize(corner_total * stride);

        const bool has_color = !usd_mesh.vertex_colors.empty() && (attribute_component_count(usd_mesh.vertex_colors.format) >= 3);

        std::size_t vertex_index = 0;
        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_count  = counts[facet];
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            const std::size_t   facet_first   = vertex_index;
            for (std::uint32_t local = 0; local < corner_count; ++local) {
                const std::uint32_t usd_vertex = indices[corner_offset + local];
                const std::size_t   usd_corner = corner_offset + local;
                std::uint8_t*       destination = soup->vertex_data.data() + (vertex_index * stride);
                std::size_t         offset      = 0;

                const lightusd::tydra::vec3& point = usd_mesh.points[usd_vertex];
                const float position[3] = {point[0], point[1], point[2]};
                std::memcpy(destination + offset, position, sizeof(position));
                offset += sizeof(position);

                if (has_normal) {
                    const glm::vec4 normal = read_attribute(
                        usd_mesh.normals,
                        attribute_element_index(usd_mesh.normals, usd_vertex, facet, usd_corner)
                    );
                    const float normal_values[3] = {normal.x, normal.y, normal.z};
                    std::memcpy(destination + offset, normal_values, sizeof(normal_values));
                    offset += sizeof(normal_values);
                }
                if (has_texcoord) {
                    const glm::vec4 uv = read_attribute(
                        texcoord_i->second,
                        attribute_element_index(texcoord_i->second, usd_vertex, facet, usd_corner)
                    );
                    const float uv_values[2] = {uv.x, uv.y};
                    std::memcpy(destination + offset, uv_values, sizeof(uv_values));
                    offset += sizeof(uv_values);
                }
                glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
                if (has_color) {
                    color = read_attribute(
                        usd_mesh.vertex_colors,
                        attribute_element_index(usd_mesh.vertex_colors, usd_vertex, facet, usd_corner)
                    );
                    color.w = 1.0f;
                }
                const float color_values[4] = {color.x, color.y, color.z, color.w};
                std::memcpy(destination + offset, color_values, sizeof(color_values));

                ++vertex_index;
            }
            for (std::uint32_t local = 2; local < corner_count; ++local) {
                soup->index_data.push_back(static_cast<std::uint32_t>(facet_first));
                soup->index_data.push_back(static_cast<std::uint32_t>(facet_first + local - 1));
                soup->index_data.push_back(static_cast<std::uint32_t>(facet_first + local));
            }
        }
        return soup;
    }

    void convert_meshes()
    {
        m_result.data.meshes.reserve(m_scene->meshes.size());
        for (std::size_t mesh_index = 0, end = m_scene->meshes.size(); mesh_index < end; ++mesh_index) {
            const Tydra_mesh& usd_mesh = m_scene->meshes[mesh_index];
            const std::string mesh_name = usd_mesh.prim_name.empty()
                ? fmt::format("mesh_{}", mesh_index)
                : usd_mesh.prim_name;

            std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>(mesh_name);
            mesh->set_source_path(m_arguments.path);
            mesh->layer_id = m_arguments.mesh_layer_id;
            mesh->enable_flag_bits(
                erhe::Item_flags::content    |
                erhe::Item_flags::show_in_ui |
                erhe::Item_flags::id
            );
            mesh->set_value(erhe::scene::Mesh::shadow_cast_property, true);

            const bool                       geometry_normative   = is_geometry_normative(usd_mesh.abs_path);
            const std::vector<std::uint32_t> facet_corner_offsets = make_facet_corner_offsets(usd_mesh);
            const std::vector<Facet_group>   groups               = make_facet_groups(usd_mesh);
            for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
                const Facet_group& group = groups[group_index];
                const std::string  name  = group.name.empty()
                    ? fmt::format("{}[{}]", mesh_name, group_index)
                    : fmt::format("{}.{}", mesh_name, group.name);
                std::shared_ptr<erhe::primitive::Primitive> primitive;
                if (geometry_normative) {
                    primitive = std::make_shared<erhe::primitive::Primitive>(
                        build_geometry(usd_mesh, group, facet_corner_offsets, name)
                    );
                } else {
                    primitive = std::make_shared<erhe::primitive::Primitive>(
                        build_triangle_soup(usd_mesh, group, facet_corner_offsets)
                    );
                }
                mesh->add_primitive(primitive, material_at(group.material_id));
            }
            m_result.data.meshes.push_back(mesh);
        }
    }

    void convert_cameras()
    {
        m_result.data.cameras.reserve(m_scene->cameras.size());
        for (std::size_t camera_index = 0, end = m_scene->cameras.size(); camera_index < end; ++camera_index) {
            Tydra_camera      usd_camera = m_scene->cameras[camera_index];
            const std::string camera_name = usd_camera.name.empty()
                ? fmt::format("camera_{}", camera_index)
                : usd_camera.name;

            std::shared_ptr<erhe::scene::Camera> camera = std::make_shared<erhe::scene::Camera>(camera_name);
            camera->set_source_path(m_arguments.path);
            camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);

            // Only an authored camera attribute becomes a local value
            // (doc/usd-compatibility-plan.md I2); the field of view and the
            // orthographic extents are derived from focalLength and the
            // apertures, so they are written when any of those is authored.
            const std::string& path             = usd_camera.abs_path;
            const bool         lens_authored    =
                is_authored(path, "focalLength")        ||
                is_authored(path, "horizontalAperture") ||
                is_authored(path, "verticalAperture");
            const bool         range_authored   = is_authored(path, "clippingRange");
            const bool         ortho            = usd_camera.projection == lightusd::GeomCamera::Projection::Orthographic;

            if (range_authored) {
                camera->set_value(erhe::scene::Camera::z_near_property,         usd_camera.znear);
                camera->set_value(erhe::scene::Camera::z_far_property,          usd_camera.zfar);
                camera->set_value(erhe::scene::Camera::infinite_z_far_property, false);
            }
            if (ortho) {
                // A USD aperture is in tenths of a scene unit, so the
                // orthographic view spans aperture / 10 world units.
                camera->set_value(erhe::scene::Camera::projection_type_property, erhe::scene::Projection::Type::orthogonal);
                if (is_authored(path, "horizontalAperture")) {
                    const float width = usd_camera.horizontalAperture * 0.1f;
                    camera->set_value(erhe::scene::Camera::ortho_width_property, width);
                    camera->set_value(erhe::scene::Camera::ortho_left_property, -0.5f * width);
                }
                if (is_authored(path, "verticalAperture")) {
                    const float height = usd_camera.verticalAperture * 0.1f;
                    camera->set_value(erhe::scene::Camera::ortho_height_property, height);
                    camera->set_value(erhe::scene::Camera::ortho_bottom_property, -0.5f * height);
                }
            } else if (lens_authored) {
                camera->set_value(erhe::scene::Camera::projection_type_property, erhe::scene::Projection::Type::perspective_vertical);
                camera->set_value(erhe::scene::Camera::fov_y_property, usd_camera.yfov());
                camera->set_value(erhe::scene::Camera::fov_x_property, usd_camera.xfov());
            }
            if (is_authored(path, "exposure")) {
                camera->set_exposure(usd_camera.exposure);
            }
            m_result.data.cameras.push_back(camera);
        }
    }

    void convert_lights()
    {
        m_result.data.lights.resize(m_scene->lights.size());
        for (std::size_t light_index = 0, end = m_scene->lights.size(); light_index < end; ++light_index) {
            const Tydra_light& usd_light = m_scene->lights[light_index];
            const std::string  light_name = usd_light.name.empty()
                ? fmt::format("light_{}", light_index)
                : usd_light.name;

            erhe::scene::Light_type light_type = erhe::scene::Light_type::point;
            switch (usd_light.type) {
                case Tydra_light::Type::Distant: {
                    light_type = erhe::scene::Light_type::directional;
                    break;
                }
                case Tydra_light::Type::Point:
                case Tydra_light::Type::Sphere: {
                    // UsdLux puts a spot cone on a sphere light through
                    // ShapingAPI; without the schema it stays a point light.
                    light_type = has_api_schema(usd_light.abs_path, lightusd::APISchemas::APIName::ShapingAPI)
                        ? erhe::scene::Light_type::spot
                        : erhe::scene::Light_type::point;
                    break;
                }
                case Tydra_light::Type::Disk:
                case Tydra_light::Type::Rect:
                case Tydra_light::Type::Cylinder: {
                    log_usd->info(
                        "USD light '{}': area lights have no erhe counterpart - imported as a point light",
                        light_name
                    );
                    light_type = erhe::scene::Light_type::point;
                    break;
                }
                default: {
                    log_usd->info("USD light '{}': light type has no erhe counterpart - skipped", light_name);
                    continue;
                }
            }

            std::shared_ptr<erhe::scene::Light> light = std::make_shared<erhe::scene::Light>(light_name);
            light->set_source_path(m_arguments.path);
            // The prim's own type is always authored, so the light type is
            // always a local value; every other field is written only when
            // the UsdLux input it comes from carries an authored opinion
            // (doc/usd-compatibility-plan.md I2).
            light->set_light_type(light_type);
            const std::string& path = usd_light.abs_path;
            if (is_authored(path, "inputs:color")) {
                light->set_color(glm::vec3{usd_light.color[0], usd_light.color[1], usd_light.color[2]});
            }
            // UsdLux intensity and exposure are one photometric quantity;
            // erhe has no exposure on a light, so the two combine. No unit
            // conversion is applied (see doc/usd_compatibility.md, Lights).
            if (is_authored(path, "inputs:intensity") || is_authored(path, "inputs:exposure")) {
                light->set_intensity(usd_light.intensity * std::pow(2.0f, usd_light.exposure));
            }
            if (usd_light.enableColorTemperature && is_authored(path, "inputs:colorTemperature")) {
                light->set_temperature(usd_light.colorTemperature);
            }
            if ((light_type == erhe::scene::Light_type::spot) && is_authored(path, "inputs:shaping:cone:angle")) {
                const float outer = glm::radians(usd_light.shapingConeAngle);
                light->set_outer_spot_angle(outer);
                light->set_inner_spot_angle(outer * std::max(0.0f, 1.0f - usd_light.shapingConeSoftness));
            }
            if (is_authored(path, "inputs:shadow:enable")) {
                light->set_cast_shadow(usd_light.shadowEnable);
            }
            light->layer_id = 0;
            light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            m_result.data.lights[light_index] = light;
        }
    }

    // upAxis and metersPerUnit of the stage as one transform applied to the
    // top-level imported nodes (doc/usd_compatibility.md, stage-level
    // constants): erhe, like glTF, is Y-up and metres.
    [[nodiscard]] auto make_stage_transform() const -> glm::mat4
    {
        glm::mat4 transform{1.0f};
        if (m_result.data.up_axis == "Z") {
            transform = glm::rotate(transform, -glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        } else if (m_result.data.up_axis == "X") {
            log_usd->warn("USD stage up axis 'X' has no erhe counterpart - imported as Y-up");
        }
        const float scale = static_cast<float>(m_result.data.meters_per_unit);
        if (scale != 1.0f) {
            transform = glm::scale(transform, glm::vec3{scale, scale, scale});
        }
        return transform;
    }

    void convert_nodes()
    {
        const glm::mat4 stage_transform = make_stage_transform();
        for (const Tydra_node& usd_node : m_scene->nodes) {
            convert_node(usd_node, m_arguments.root_node, stage_transform);
        }
    }

    // Prim types that hold no place in the scene graph: none of them is
    // Xformable, and Tydra still lists each as a transform node. Scope,
    // Material, Shader and NodeGraph are the shading network; a GeomSubset's
    // facets are already carried by a primitive of its mesh.
    [[nodiscard]] static auto is_non_scene_prim_type(const std::string& type_name) -> bool
    {
        return
            (type_name == "Scope")     ||
            (type_name == "Material")  ||
            (type_name == "Shader")    ||
            (type_name == "NodeGraph") ||
            (type_name == "GeomSubset");
    }

    [[nodiscard]] auto subtree_has_scene_content(const Tydra_node& usd_node) const -> bool
    {
        if (usd_node.nodeType != lightusd::tydra::NodeType::Xform) {
            return true; // a mesh, camera, light, skeleton or volume
        }
        for (const Tydra_node& usd_child : usd_node.children) {
            if (subtree_has_scene_content(usd_child)) {
                return true;
            }
        }
        return false;
    }

    // Such a prim contributes no erhe node when its subtree carries no scene
    // content: a material scope is a namespace, not a place in the scene.
    [[nodiscard]] auto is_non_scene_node(const Tydra_node& usd_node) const -> bool
    {
        const lightusd::Prim* prim = find_prim(usd_node.abs_path);
        if (prim == nullptr) {
            return false;
        }
        if (!is_non_scene_prim_type(prim->type_name())) {
            return false;
        }
        return !subtree_has_scene_content(usd_node);
    }

    void convert_node(
        const Tydra_node&                         usd_node,
        const std::shared_ptr<erhe::scene::Node>& parent,
        const glm::mat4&                          extra_transform
    )
    {
        if (is_non_scene_node(usd_node)) {
            return;
        }
        const std::string node_name = usd_node.prim_name.empty()
            ? fmt::format("node_{}", m_result.data.nodes.size())
            : usd_node.prim_name;
        std::shared_ptr<erhe::scene::Node> node = std::make_shared<erhe::scene::Node>(node_name);
        node->set_source_path(m_arguments.path);
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        node->Hierarchy::set_parent(parent);
        node->node_data.transforms.parent_from_node.set(extra_transform * to_glm(usd_node.local_matrix));
        node->update_world_from_node();
        node->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
        m_result.data.nodes.push_back(node);

        const std::shared_ptr<erhe::Item_base> attachment = attach_node_content(usd_node, node);

        // The prim's own opinions: `visibility` and `purpose` on the node
        // that holds its place in the scene graph, and every `erhe:` custom
        // attribute on the item the name resolves against.
        apply_visibility_and_purpose(usd_node.abs_path, *node.get());
        apply_erhe_custom_attributes(usd_node.abs_path, attachment.get(), node.get());

        const glm::mat4 child_transform{1.0f};
        for (const Tydra_node& usd_child : usd_node.children) {
            convert_node(usd_child, node, child_transform);
        }
    }

    // The item the prim's own type contributes, attached to `node`; null for
    // a plain Xform prim and for content the conversion skipped.
    [[nodiscard]] auto attach_node_content(const Tydra_node& usd_node, const std::shared_ptr<erhe::scene::Node>& node) -> std::shared_ptr<erhe::Item_base>
    {
        if (usd_node.id < 0) {
            return {};
        }
        const std::size_t content_index = static_cast<std::size_t>(usd_node.id);
        switch (usd_node.nodeType) {
            case lightusd::tydra::NodeType::Mesh: {
                if (content_index >= m_result.data.meshes.size()) {
                    return {};
                }
                // A USD mesh several prims reference becomes one erhe mesh
                // per prim, the way a glTF mesh shared by several nodes does.
                const erhe::scene::Mesh& template_mesh = *m_result.data.meshes[content_index].get();
                std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh_attached[content_index]
                    ? std::make_shared<erhe::scene::Mesh>(template_mesh, erhe::for_clone{true})
                    : m_result.data.meshes[content_index];
                m_mesh_attached[content_index] = true;
                node->attach(mesh);
                return mesh;
            }
            case lightusd::tydra::NodeType::Camera: {
                if (content_index < m_result.data.cameras.size()) {
                    const std::shared_ptr<erhe::scene::Camera>& camera = m_result.data.cameras[content_index];
                    node->attach(camera);
                    return camera;
                }
                return {};
            }
            case lightusd::tydra::NodeType::PointLight:
            case lightusd::tydra::NodeType::DirectionalLight:
            case lightusd::tydra::NodeType::RectLight:
            case lightusd::tydra::NodeType::DiskLight:
            case lightusd::tydra::NodeType::CylinderLight: {
                if ((content_index < m_result.data.lights.size()) && m_result.data.lights[content_index]) {
                    const std::shared_ptr<erhe::scene::Light>& light = m_result.data.lights[content_index];
                    node->attach(light);
                    return light;
                }
                return {};
            }
            default: {
                return {};
            }
        }
    }

    const Usd_load_arguments&    m_arguments;
    Usd_load_result&             m_result;
    const lightusd::Stage*       m_stage{nullptr};
    const Tydra_scene*           m_scene{nullptr};
    std::map<std::size_t, bool>  m_mesh_attached;
    // Authored property names per prim path, see authored_property_names.
    std::map<std::string, std::set<std::string>> m_authored_property_names;
};

} // anonymous namespace

auto convert_stage(const Stage& stage, const Usd_load_arguments& arguments) -> Usd_load_result
{
    ERHE_PROFILE_FUNCTION();

    Usd_load_result result{};
    Importer        importer{arguments, result};
    importer.convert(stage.get_impl().stage);
    return result;
}

auto load_usd(const Usd_load_arguments& arguments) -> Usd_load_result
{
    ERHE_PROFILE_FUNCTION();

    Load_stage_result load_stage_result = load_stage(arguments.path);
    if (!load_stage_result.stage) {
        Usd_load_result result{};
        result.error   = load_stage_result.error;
        result.warning = load_stage_result.warning;
        return result;
    }

    Usd_load_result result = convert_stage(*load_stage_result.stage.get(), arguments);
    if (result.warning.empty()) {
        result.warning = load_stage_result.warning;
    }
    return result;
}

} // namespace erhe::usd
