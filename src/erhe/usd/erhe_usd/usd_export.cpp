#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/trs_transform.hpp"

// LightUSD headers. Together with usd.cpp and usd_import.cpp this is the
// only place in erhe that includes them; everything the rest of erhe sees is
// declared in usd.hpp.
#include "lightusd.hh"
#include "core/prim.hh"
#include "core/model-scope.hh"
#include "core/prim-metas.hh"
#include "stage.hh"
#include "usda-writer.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace erhe::usd {

namespace {

// The scope holding every exported material, and the name of the wrapper
// prim created when the scene has more than one top-level prim: a stage
// names one `defaultPrim` and an erhe scene has one root
// (doc/usd_compatibility.md, stage-level constants).
constexpr const char* c_materials_scope_name = "Materials";
constexpr const char* c_world_prim_name      = "World";

// The focal length every exported perspective camera gets. USD is
// physical-camera-first and erhe stores the two field-of-view angles, so one
// of the three is free; fixing the focal length puts the angles in the
// apertures, which `2 * atan(0.5 * aperture / focalLength)` reads back
// exactly (doc/usd_compatibility.md, cameras).
constexpr float c_export_focal_length = 50.0f;

// The USD `purpose` vocabulary, which erhe's Purpose enumeration mirrors
// token for token; the inverse of the importer's to_erhe_purpose.
[[nodiscard]] auto to_usd_purpose(const erhe::Purpose purpose) -> lightusd::Purpose
{
    switch (purpose) {
        case erhe::Purpose::default_: return lightusd::Purpose::Default;
        case erhe::Purpose::render:   return lightusd::Purpose::Render;
        case erhe::Purpose::proxy:    return lightusd::Purpose::Proxy;
        case erhe::Purpose::guide:    return lightusd::Purpose::Guide;
        default:                      return lightusd::Purpose::Default;
    }
}

// A glm mat4 is column-major with the column-vector convention; a USD
// matrix4d is row-major with the row-vector convention. The two store the
// same coefficients in the same order, so an element-for-element copy is the
// correct conversion - the importer's to_glm read the same way.
[[nodiscard]] auto to_usd(const glm::mat4& matrix) -> lightusd::value::matrix4d
{
    lightusd::value::matrix4d result{};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            result.m[j][i] = static_cast<double>(matrix[j][i]);
        }
    }
    return result;
}

[[nodiscard]] auto is_identity(const glm::mat4& matrix) -> bool
{
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const float expected = (i == j) ? 1.0f : 0.0f;
            if (matrix[j][i] != expected) {
                return false;
            }
        }
    }
    return true;
}

// Sibling-unique prim names: the names a scope has already handed out plus
// the suffix rule of erhe::Hierarchy::make_sibling_unique_name (M2) - the
// base is the wanted name without a trailing `_<digits>`, and the first free
// `<base>_<n>` from 1 wins. Sanitizing can map two distinct item names onto
// one spelling, so the writer needs the rule even though erhe names are
// already sibling-unique.
class Name_scope final
{
public:
    [[nodiscard]] auto make_unique(const std::string_view wanted_name) -> std::string
    {
        const std::string sanitized = sanitize_usd_identifier(wanted_name);
        if (m_taken.find(sanitized) == m_taken.end()) {
            m_taken.insert(sanitized);
            return sanitized;
        }
        std::string_view base         = sanitized;
        std::size_t      digits_begin = base.size();
        while ((digits_begin > 0) && (base[digits_begin - 1] >= '0') && (base[digits_begin - 1] <= '9')) {
            --digits_begin;
        }
        if ((digits_begin < base.size()) && (digits_begin >= 2) && (base[digits_begin - 1] == '_')) {
            base = base.substr(0, digits_begin - 1);
        }
        for (std::size_t number = 1; ; ++number) {
            std::string candidate = std::string{base} + "_" + std::to_string(number);
            if (m_taken.find(candidate) == m_taken.end()) {
                m_taken.insert(candidate);
                return candidate;
            }
        }
    }

private:
    std::set<std::string> m_taken;
};

// The erhe properties this writer carries in a USD attribute of the schema -
// the exact inverse of what the importer reads back
// (doc/usd-compatibility-plan.md I2, and the per-domain tables of
// doc/usd_compatibility.md). Every other local value travels as an `erhe:`
// custom attribute, so a property is listed here exactly once: writing both
// forms would author one value twice.
[[nodiscard]] auto is_native_usd_property(const std::string_view owner, const std::string_view name) -> bool
{
    if (owner == "Item_base") {
        return (name == "visible") || (name == "purpose");
    }
    if (owner == "Material") {
        return
            (name == "base_color")                 || (name == "emissive")          ||
            (name == "metallic")                   || (name == "roughness")         ||
            (name == "opacity")                    || (name == "ior")               ||
            (name == "occlusion_texture_strength") || (name == "blending_mode")     ||
            (name == "alpha_cutoff")               || (name == "base_color_texture") ||
            (name == "metallic_roughness_texture") || (name == "normal_texture")    ||
            (name == "occlusion_texture")          || (name == "emissive_texture");
    }
    if (owner == "Camera") {
        return
            (name == "projection_type") || (name == "fov_x")       || (name == "fov_y")        ||
            (name == "ortho_left")      || (name == "ortho_width") || (name == "ortho_bottom") ||
            (name == "ortho_height")    || (name == "z_near")      || (name == "z_far")        ||
            (name == "infinite_z_far")  || (name == "exposure");
    }
    if (owner == "Light") {
        return
            (name == "light_type")  || (name == "color")            || (name == "intensity")        ||
            (name == "temperature") || (name == "inner_spot_angle") || (name == "outer_spot_angle") ||
            (name == "cast_shadow");
    }
    return false;
}

// One `erhe:` custom attribute value. The USD type follows the erhe property
// type and the value is the one the importer parses back with the D16
// `from_string` after stripping brackets, commas and quotes. A quaternion
// travels as a `float4` in erhe's `x y z w` component order, which is what
// that parse expects - a USD `quatf` prints its real part first.
[[nodiscard]] auto make_custom_attribute(
    const erhe::property::Dependency_property& property,
    const erhe::property::Property_value&      value
) -> lightusd::Attribute
{
    lightusd::Attribute        attribute;
    lightusd::primvar::PrimVar var;
    switch (property.get_type()) {
        case erhe::property::Property_type::boolean: {
            var.set_value(std::get<bool>(value));
            break;
        }
        case erhe::property::Property_type::integer: {
            var.set_value(static_cast<int32_t>(std::get<int>(value)));
            break;
        }
        case erhe::property::Property_type::floating: {
            var.set_value(std::get<float>(value));
            break;
        }
        case erhe::property::Property_type::vec2: {
            const glm::vec2 v = std::get<glm::vec2>(value);
            var.set_value(lightusd::value::float2{v.x, v.y});
            break;
        }
        case erhe::property::Property_type::vec3: {
            const glm::vec3 v = std::get<glm::vec3>(value);
            var.set_value(lightusd::value::float3{v.x, v.y, v.z});
            break;
        }
        case erhe::property::Property_type::vec4: {
            const glm::vec4 v = std::get<glm::vec4>(value);
            var.set_value(lightusd::value::float4{v.x, v.y, v.z, v.w});
            break;
        }
        case erhe::property::Property_type::quat: {
            const glm::quat q = std::get<glm::quat>(value);
            var.set_value(lightusd::value::float4{q.x, q.y, q.z, q.w});
            break;
        }
        case erhe::property::Property_type::ivec2: {
            const glm::ivec2 v = std::get<glm::ivec2>(value);
            var.set_value(lightusd::value::int2{v.x, v.y});
            break;
        }
        case erhe::property::Property_type::ivec3: {
            const glm::ivec3 v = std::get<glm::ivec3>(value);
            var.set_value(lightusd::value::int3{v.x, v.y, v.z});
            break;
        }
        case erhe::property::Property_type::ivec4: {
            const glm::ivec4 v = std::get<glm::ivec4>(value);
            var.set_value(lightusd::value::int4{v.x, v.y, v.z, v.w});
            break;
        }
        case erhe::property::Property_type::enumeration: {
            var.set_value(lightusd::value::token{erhe::property::to_string(property, value)});
            break;
        }
        default: {
            // string, and object: the D16 text form, which for an object
            // reference is the pointee's get_reference_path().
            var.set_value(erhe::property::to_string(property, value));
            break;
        }
    }
    attribute.set_var(std::move(var));
    return attribute;
}

class Exporter final
{
public:
    Exporter(const Usd_save_arguments& arguments, Usd_save_result& result)
        : m_arguments{arguments}
        , m_result   {result}
    {
    }

    void write()
    {
        ERHE_PROFILE_FUNCTION();

        if (!m_arguments.root_node) {
            m_result.error = "no root node to write";
            return;
        }

        lightusd::Stage stage;
        write_materials();

        Name_scope                  top_level_names;
        std::vector<lightusd::Prim> content_prims;
        write_child_nodes(*m_arguments.root_node.get(), glm::mat4{1.0f}, top_level_names, content_prims);

        std::string default_prim_name;
        if (content_prims.size() == 1) {
            lightusd::Prim& prim = content_prims.front();
            default_prim_name = std::string{prim.element_name()};
            add_collections(prim);
            add_root_prim(stage, std::move(prim));
        } else {
            // Several top-level prims are gathered under one Xform that plays
            // the erhe root's part, so the stage still names one defaultPrim.
            lightusd::Xform world;
            world.name = c_world_prim_name;
            lightusd::Prim world_prim{world};
            for (lightusd::Prim& prim : content_prims) {
                std::string error;
                if (!world_prim.add_child(std::move(prim), false, &error)) {
                    add_warning(fmt::format("a top-level prim could not be added under '{}': {}", c_world_prim_name, error));
                }
            }
            prefix_tag_member_paths(fmt::format("/{}", c_world_prim_name));
            default_prim_name = c_world_prim_name;
            add_collections(world_prim);
            add_root_prim(stage, std::move(world_prim));
        }
        if (m_materials_prim.has_value()) {
            add_root_prim(stage, std::move(m_materials_prim.value()));
        }

        stage.metas().defaultPrim = lightusd::value::token{default_prim_name};
        stage.metas().upAxis.set_value(
            (m_arguments.up_axis == "Z") ? lightusd::Axis::Z :
            (m_arguments.up_axis == "X") ? lightusd::Axis::X : lightusd::Axis::Y
        );
        stage.metas().metersPerUnit.set_value(m_arguments.meters_per_unit);
        for (const std::pair<const std::string, std::string>& entry : m_arguments.custom_layer_data) {
            stage.metas().customLayerData[entry.first] = lightusd::MetaVariable{entry.second};
        }
        if (!m_arguments.custom_layer_data.empty()) {
            stage.metas().customLayerDataAuthored = true;
        }
        stage.commit();

        const std::string filename = m_arguments.path.generic_string();
        std::string       warning;
        std::string       error;
        if (!lightusd::usda::SaveAsUSDA(filename, stage, &warning, &error)) {
            m_result.error = error.empty() ? std::string{"USDA write failed"} : error;
            log_usd->error("Writing USD stage '{}' failed: {}", filename, m_result.error);
            return;
        }
        if (!warning.empty()) {
            add_warning(warning);
        }
        log_usd->info(
            "USD '{}': {} node(s), {} mesh prim(s), {} material(s)",
            filename, m_node_count, m_mesh_count, m_arguments.materials.size()
        );
    }

private:
    void add_warning(const std::string& text)
    {
        if (!m_result.warning.empty()) {
            m_result.warning += "; ";
        }
        m_result.warning += text;
        log_usd->warn("USD '{}': {}", m_arguments.path.generic_string(), text);
    }

    void add_root_prim(lightusd::Stage& stage, lightusd::Prim&& prim)
    {
        if (!stage.add_root_prim(std::move(prim), false)) {
            add_warning("a root prim could not be added to the stage");
        }
    }

    // -------------------------------------------------------------------
    // Property values
    // -------------------------------------------------------------------

    [[nodiscard]] static auto is_local(
        const erhe::property::Dependency_object&   object,
        const erhe::property::Dependency_property& property
    ) -> bool
    {
        return object.get_value_source(property) == erhe::property::Value_source::local;
    }

    // Every local value of `object` that no schema attribute of the prim
    // already carries, as an `erhe:Owner:name` custom attribute
    // (doc/usd_compatibility.md, property system). A bridged property is the
    // object's own engineered representation - the node transform, the item
    // name and tags - and travels in the USD form that owns it; a property
    // without the serialize flag is session state, and so is an expression
    // (D14).
    template <typename T>
    void write_erhe_properties(const erhe::property::Dependency_object& object, T& typed_prim)
    {
        const erhe::property::Owner_type object_owner_type = object.get_property_owner_type();
        object.for_each_local_value(
            [this, &object, object_owner_type, &typed_prim](
                const erhe::property::Dependency_property& property,
                const erhe::property::Property_value&      value
            ) {
                const erhe::property::Property_metadata& metadata = property.get_metadata(object_owner_type);
                if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                    return;
                }
                if (metadata.bridge.is_bound()) {
                    return;
                }
                const std::string_view owner_name = erhe::property::get_owner_type_name(property.get_owner_type());
                if (is_native_usd_property(owner_name, property.get_name())) {
                    return;
                }
                if (object.get_expression(property).has_value()) {
                    return;
                }
                const std::string attribute_name = fmt::format("erhe:{}:{}", owner_name, property.get_name());
                if (typed_prim.props.find(attribute_name) != typed_prim.props.end()) {
                    add_warning(
                        fmt::format("'{}' is authored by more than one item of one prim - the later value is dropped", attribute_name)
                    );
                    return;
                }
                typed_prim.props.emplace(attribute_name, lightusd::Property{make_custom_attribute(property, value), true});
            }
        );
    }

    // `visible` and `purpose` of the item that holds the prim's place in the
    // scene graph. The importer reads both from the prim onto the node, so
    // the node is what the writer reads them from.
    template <typename T>
    void write_visibility_and_purpose(const erhe::Item_base& item, T& typed_prim)
    {
        if (is_local(item, erhe::Item_base::visible_property.get())) {
            typed_prim.visibility.set_value(
                item.get_value(erhe::Item_base::visible_property)
                    ? lightusd::Visibility::Inherited
                    : lightusd::Visibility::Invisible
            );
        }
        if (is_local(item, erhe::Item_base::purpose_property.get())) {
            typed_prim.purpose.set_value(to_usd_purpose(item.get_value(erhe::Item_base::purpose_property)));
        }
    }

    // -------------------------------------------------------------------
    // Materials
    // -------------------------------------------------------------------

    [[nodiscard]] auto texture_of(const std::size_t material_index, const Usd_material_texture_slot slot) const -> const Usd_save_texture*
    {
        for (const Usd_save_texture& texture : m_arguments.textures) {
            if ((texture.material_index == material_index) && (texture.slot == slot) && !texture.path.empty()) {
                return &texture;
            }
        }
        return nullptr;
    }

    // The image as the USDA asset identifier: relative to the written file's
    // own directory, so a scene and its images keep working when the pair is
    // moved together.
    [[nodiscard]] auto asset_path_of(const std::filesystem::path& image_path) const -> std::string
    {
        const std::filesystem::path directory = m_arguments.path.parent_path();
        if (!directory.empty()) {
            std::error_code             error_code{};
            const std::filesystem::path relative = std::filesystem::relative(image_path, directory, error_code);
            if (!error_code && !relative.empty()) {
                return relative.generic_string();
            }
        }
        return image_path.generic_string();
    }

    [[nodiscard]] static auto texture_shader_name(const Usd_material_texture_slot slot) -> const char*
    {
        switch (slot) {
            case Usd_material_texture_slot::base_color:         return "base_color_texture";
            case Usd_material_texture_slot::metallic_roughness: return "metallic_roughness_texture";
            case Usd_material_texture_slot::normal:             return "normal_texture";
            case Usd_material_texture_slot::occlusion:          return "occlusion_texture";
            case Usd_material_texture_slot::emissive:           return "emissive_texture";
            default:                                            return "texture";
        }
    }

    // One UsdUVTexture shader prim under the material prim, created once per
    // slot; the surface inputs connect to its outputs.
    [[nodiscard]] auto get_texture_shader(
        lightusd::Prim&                 material_prim,
        const std::string&              material_path,
        const std::size_t               material_index,
        const Usd_material_texture_slot slot
    ) -> std::string
    {
        const std::pair<std::size_t, Usd_material_texture_slot>  key{material_index, slot};
        const std::map<std::pair<std::size_t, Usd_material_texture_slot>, std::string>::const_iterator i =
            m_texture_shader_names.find(key);
        if (i != m_texture_shader_names.end()) {
            return i->second;
        }
        const Usd_save_texture* texture = texture_of(material_index, slot);
        if (texture == nullptr) {
            return {};
        }
        // The primvar reader every texture of the material reads its UVs
        // from; a material without a texture gets no shading network beyond
        // its surface.
        if (m_uv_reader_materials.insert(material_index).second) {
            add_uv_reader(material_prim);
        }

        lightusd::UsdUVTexture uv_texture;
        uv_texture.file.set_value(lightusd::value::AssetPath{asset_path_of(texture->path)});
        uv_texture.sourceColorSpace.set_value(
            texture->srgb
                ? lightusd::UsdUVTexture::SourceColorSpace::SRGB
                : lightusd::UsdUVTexture::SourceColorSpace::Raw
        );
        uv_texture.st.set_connection(lightusd::Path{material_path + "/uv_reader", "outputs:result"});
        uv_texture.outputsR.set_authored(true);
        uv_texture.outputsG.set_authored(true);
        uv_texture.outputsB.set_authored(true);
        uv_texture.outputsRGB.set_authored(true);

        lightusd::Shader shader;
        shader.name    = texture_shader_name(slot);
        shader.info_id = "UsdUVTexture";
        shader.value   = std::move(uv_texture);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("texture shader '{}' could not be added: {}", shader.name, error));
            return {};
        }
        m_texture_shader_names[key] = shader.name;
        return shader.name;
    }

    void add_uv_reader(lightusd::Prim& material_prim)
    {
        lightusd::UsdPrimvarReader_float2 reader;
        reader.varname.set_value(std::string{"st"});
        reader.result.set_authored(true);

        lightusd::Shader shader;
        shader.name    = "uv_reader";
        shader.info_id = "UsdPrimvarReader_float2";
        shader.value   = std::move(reader);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("uv reader shader could not be added: {}", error));
        }
    }

    // A texture slot with a local value whose image the caller could not
    // resolve to a file: a generated texture has no bytes on disk, so the
    // slot stays out of the shading network (commit 1, see
    // src/erhe/usd/notes.md).
    void warn_about_unresolved_textures(const erhe::primitive::Material& material, const std::size_t material_index)
    {
        using Slot_property = erhe::property::Property<erhe::property::Object_reference>;
        const std::pair<const Slot_property*, Usd_material_texture_slot> slots[] = {
            {&erhe::primitive::Material::base_color_texture_property,         Usd_material_texture_slot::base_color},
            {&erhe::primitive::Material::metallic_roughness_texture_property, Usd_material_texture_slot::metallic_roughness},
            {&erhe::primitive::Material::normal_texture_property,             Usd_material_texture_slot::normal},
            {&erhe::primitive::Material::occlusion_texture_property,          Usd_material_texture_slot::occlusion},
            {&erhe::primitive::Material::emissive_texture_property,           Usd_material_texture_slot::emissive}
        };
        for (const std::pair<const Slot_property*, Usd_material_texture_slot>& slot : slots) {
            if (!is_local(material, slot.first->get())) {
                continue;
            }
            if (material.get_value(*slot.first).object == nullptr) {
                continue;
            }
            if (texture_of(material_index, slot.second) == nullptr) {
                add_warning(
                    fmt::format(
                        "material '{}' texture slot '{}' has no source file - the slot is not written",
                        material.get_name(), texture_shader_name(slot.second)
                    )
                );
            }
        }
    }

    void write_materials()
    {
        if (m_arguments.materials.empty()) {
            return;
        }
        lightusd::Scope scope;
        scope.name = c_materials_scope_name;
        lightusd::Prim scope_prim{scope};

        Name_scope material_names;
        for (std::size_t material_index = 0, end = m_arguments.materials.size(); material_index < end; ++material_index) {
            const std::shared_ptr<erhe::primitive::Material>& material = m_arguments.materials[material_index];
            if (!material) {
                continue;
            }
            const std::string material_name = material_names.make_unique(material->get_name());
            const std::string material_path = fmt::format("/{}/{}", c_materials_scope_name, material_name);

            lightusd::Material usd_material;
            usd_material.name = material_name;
            usd_material.surface.set(lightusd::Path{material_path + "/surface", "outputs:surface"});
            write_erhe_properties(*material.get(), usd_material);

            lightusd::Prim material_prim{usd_material};
            warn_about_unresolved_textures(*material.get(), material_index);
            write_surface_shader(material_prim, material_path, *material.get(), material_index);
            m_material_paths[material.get()] = material_path;

            std::string error;
            if (!scope_prim.add_child(std::move(material_prim), false, &error)) {
                add_warning(fmt::format("material '{}' could not be added: {}", material_name, error));
            }
        }
        m_materials_prim = std::move(scope_prim);
    }

    void write_surface_shader(
        lightusd::Prim&                  material_prim,
        const std::string&               material_path,
        const erhe::primitive::Material& material,
        const std::size_t                material_index
    )
    {
        using erhe::primitive::Material;

        lightusd::UsdPreviewSurface surface;
        surface.outputsSurface.set_authored(true);

        if (is_local(material, Material::base_color_property.get())) {
            const glm::vec3 base_color = material.get_value(Material::base_color_property);
            surface.diffuseColor.set_value(lightusd::value::color3f{base_color.x, base_color.y, base_color.z});
        }
        if (is_local(material, Material::emissive_property.get())) {
            const glm::vec3 emissive = material.get_value(Material::emissive_property);
            surface.emissiveColor.set_value(lightusd::value::color3f{emissive.x, emissive.y, emissive.z});
        }
        if (is_local(material, Material::metallic_property.get())) {
            surface.metallic.set_value(material.get_value(Material::metallic_property));
        }
        if (is_local(material, Material::roughness_property.get())) {
            // UsdPreviewSurface has one roughness; erhe's is anisotropic and
            // its x component is the isotropic one.
            surface.roughness.set_value(material.get_value(Material::roughness_property).x);
        }
        if (is_local(material, Material::opacity_property.get())) {
            surface.opacity.set_value(material.get_value(Material::opacity_property));
        }
        if (is_local(material, Material::ior_property.get())) {
            surface.ior.set_value(material.get_value(Material::ior_property));
        }
        if (is_local(material, Material::occlusion_texture_strength_property.get())) {
            surface.occlusion.set_value(material.get_value(Material::occlusion_texture_strength_property));
        }
        // UsdPreviewSurface says a positive opacityThreshold is a cutout;
        // that threshold is the only form the erhe blending mode has, so it
        // is written for an alpha-test material and for nothing else.
        if (material.get_value(Material::blending_mode_property) == erhe::primitive::Material_blending_mode::alpha_test) {
            surface.opacityThreshold.set_value(material.get_value(Material::alpha_cutoff_property));
        }

        connect_texture(material_prim, material_path, material_index, Usd_material_texture_slot::base_color, "outputs:rgb", surface.diffuseColor);
        connect_texture(material_prim, material_path, material_index, Usd_material_texture_slot::emissive,   "outputs:rgb", surface.emissiveColor);
        connect_texture(material_prim, material_path, material_index, Usd_material_texture_slot::normal,     "outputs:rgb", surface.normal);
        connect_texture(material_prim, material_path, material_index, Usd_material_texture_slot::occlusion,  "outputs:r",   surface.occlusion);
        // erhe has one metallic-roughness slot; UsdPreviewSurface reads the
        // two channels through separate inputs of the one texture, in the
        // glTF channel layout the importer expects back.
        connect_texture(material_prim, material_path, material_index, Usd_material_texture_slot::metallic_roughness, "outputs:g", surface.roughness);
        connect_texture(material_prim, material_path, material_index, Usd_material_texture_slot::metallic_roughness, "outputs:b", surface.metallic);

        lightusd::Shader shader;
        shader.name    = "surface";
        shader.info_id = "UsdPreviewSurface";
        shader.value   = std::move(surface);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("surface shader of '{}' could not be added: {}", material_path, error));
        }
    }

    template <typename T>
    void connect_texture(
        lightusd::Prim&                 material_prim,
        const std::string&              material_path,
        const std::size_t               material_index,
        const Usd_material_texture_slot slot,
        const char*                     output_name,
        T&                              input
    )
    {
        const std::string shader_name = get_texture_shader(material_prim, material_path, material_index, slot);
        if (shader_name.empty()) {
            return;
        }
        input.set_connection(lightusd::Path{material_path + "/" + shader_name, output_name});
        input.set_value_empty();
    }

    void bind_material(lightusd::MaterialBinding& binding, const erhe::primitive::Material* material)
    {
        if (material == nullptr) {
            return;
        }
        const std::map<const erhe::primitive::Material*, std::string>::const_iterator i = m_material_paths.find(material);
        if (i == m_material_paths.end()) {
            add_warning(
                fmt::format("material '{}' is used but was not offered to the writer - the binding is dropped", material->get_name())
            );
            return;
        }
        lightusd::Relationship relationship;
        relationship.set(lightusd::Path{i->second, ""});
        binding.set_materialBinding(relationship);
    }

    // -------------------------------------------------------------------
    // Nodes
    // -------------------------------------------------------------------

    // The child nodes of `node` as prims. The filter is the glTF exporter's:
    // an import_root container is unwrapped with its transform composed in,
    // a render proxy is derived data rebuilt by its owner, and anything
    // without Item_flags::content is transient editor furniture - tool
    // visuals, controllers, rendertarget UI quads - recreated every session.
    void write_child_nodes(
        const erhe::scene::Node&     node,
        const glm::mat4&             pre_transform,
        Name_scope&                  names,
        std::vector<lightusd::Prim>& out_prims
    )
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
            const erhe::scene::Node* child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
            if (child_node == nullptr) {
                continue;
            }
            const uint64_t flags = child_node->get_flag_bits();
            if ((flags & erhe::Item_flags::import_root) != 0) {
                write_child_nodes(
                    *child_node,
                    pre_transform * child_node->parent_from_node_transform().get_matrix(),
                    names,
                    out_prims
                );
                continue;
            }
            if ((flags & erhe::Item_flags::render_proxy) != 0) {
                continue;
            }
            if ((flags & erhe::Item_flags::content) == 0) {
                continue;
            }
            out_prims.push_back(write_node(*child_node, pre_transform, names));
        }
    }

    // One erhe node as one prim. The node's attachment types the prim that
    // holds the node's transform (doc/usd_compatibility.md, object model),
    // which is what the importer inverts: it makes one node per prim and
    // attaches the content the prim type names, so the pair round-trips
    // without gaining a level.
    [[nodiscard]] auto write_node(const erhe::scene::Node& node, const glm::mat4& pre_transform, Name_scope& names) -> lightusd::Prim
    {
        ++m_node_count;
        const std::string prim_name = names.make_unique(node.get_name());
        const glm::mat4   matrix    = pre_transform * node.parent_from_node_transform().get_matrix();

        m_prim_path_stack.push_back(prim_name);
        record_tags(node);

        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(&node);
        if (mesh && ((mesh->get_flag_bits() & erhe::Item_flags::rendertarget) != 0)) {
            // A rendertarget mesh is a UI quad rendered into every frame; it
            // has no serializable source image, so the node exports without it.
            mesh.reset();
        }
        const std::shared_ptr<erhe::scene::Camera> camera = erhe::scene::get_attachment<erhe::scene::Camera>(&node);
        const std::shared_ptr<erhe::scene::Light>  light  = erhe::scene::get_attachment<erhe::scene::Light>(&node);

        const int attachment_count = (mesh ? 1 : 0) + (camera ? 1 : 0) + (light ? 1 : 0);
        if (attachment_count > 1) {
            add_warning(fmt::format("node '{}' has several attachments - only the mesh, camera or light in that order is written", node.get_name()));
        }

        lightusd::Prim prim =
            mesh   ? write_mesh_prim  (node, *mesh.get(),   prim_name, matrix) :
            camera ? write_camera_prim(node, *camera.get(), prim_name, matrix) :
            light  ? write_light_prim (node, *light.get(),  prim_name, matrix) :
                     write_xform_prim (node,                prim_name, matrix);

        Name_scope                  child_names;
        std::vector<lightusd::Prim> child_prims;
        write_child_nodes(node, glm::mat4{1.0f}, child_names, child_prims);
        for (lightusd::Prim& child_prim : child_prims) {
            std::string error;
            if (!prim.add_child(std::move(child_prim), false, &error)) {
                add_warning(fmt::format("a child of '{}' could not be added: {}", prim_name, error));
            }
        }
        m_prim_path_stack.pop_back();
        return prim;
    }

    template <typename T>
    static void set_transform(T& typed_prim, const glm::mat4& matrix)
    {
        if (is_identity(matrix)) {
            return;
        }
        lightusd::XformOp op;
        op.op_type = lightusd::XformOp::OpType::Transform;
        op.set_value(to_usd(matrix));
        typed_prim.xformOps.push_back(op);
    }

    [[nodiscard]] auto write_xform_prim(const erhe::scene::Node& node, const std::string& prim_name, const glm::mat4& matrix) -> lightusd::Prim
    {
        lightusd::Xform xform;
        xform.name = prim_name;
        set_transform(xform, matrix);
        write_visibility_and_purpose(node, xform);
        write_erhe_properties(node, xform);
        return lightusd::Prim{xform};
    }

    // -------------------------------------------------------------------
    // Meshes
    // -------------------------------------------------------------------

    // The polygons of every primitive of one erhe mesh, concatenated into
    // the arrays of one USD Mesh prim. The primvars are faceVarying: one
    // value per polygon corner is the domain erhe's corner attributes live
    // in (doc/usd_compatibility.md, element table).
    class Mesh_accumulator final
    {
    public:
        std::vector<lightusd::value::point3f>    points;
        std::vector<int32_t>                     face_vertex_counts;
        std::vector<int32_t>                     face_vertex_indices;
        std::vector<lightusd::value::normal3f>   normals;
        std::vector<lightusd::value::texcoord2f> texcoords;
        std::vector<lightusd::value::color3f>    colors;
        std::vector<float>                       opacities;
        bool                                     has_normals  {false};
        bool                                     has_texcoords{false};
        bool                                     has_colors   {false};
    };

    static void append_geometry(Mesh_accumulator& out, const erhe::geometry::Geometry& geometry)
    {
        const GEO::Mesh&                       mesh       = geometry.get_mesh();
        const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
        const std::size_t                      base       = out.points.size();

        for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
            const GEO::vec3f point = erhe::geometry::get_pointf(mesh.vertices, vertex);
            out.points.push_back(lightusd::value::point3f{point.x, point.y, point.z});
        }
        for (GEO::index_t facet = 0; facet < mesh.facets.nb(); ++facet) {
            const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
            out.face_vertex_counts.push_back(static_cast<int32_t>(corner_count));
            for (GEO::index_t local = 0; local < corner_count; ++local) {
                const GEO::index_t corner = mesh.facets.corner(facet, local);
                out.face_vertex_indices.push_back(static_cast<int32_t>(base + mesh.facets.vertex(facet, local)));

                const std::optional<GEO::vec3f> normal = attributes.corner_normal.try_get(corner);
                out.normals.push_back(
                    normal.has_value()
                        ? lightusd::value::normal3f{normal.value().x, normal.value().y, normal.value().z}
                        : lightusd::value::normal3f{0.0f, 0.0f, 0.0f}
                );
                out.has_normals = out.has_normals || normal.has_value();

                const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
                out.texcoords.push_back(
                    texcoord.has_value()
                        ? lightusd::value::texcoord2f{texcoord.value().x, texcoord.value().y}
                        : lightusd::value::texcoord2f{0.0f, 0.0f}
                );
                out.has_texcoords = out.has_texcoords || texcoord.has_value();

                const std::optional<GEO::vec4f> color = attributes.corner_color_0.try_get(corner);
                out.colors.push_back(
                    color.has_value()
                        ? lightusd::value::color3f{color.value().x, color.value().y, color.value().z}
                        : lightusd::value::color3f{1.0f, 1.0f, 1.0f}
                );
                out.opacities.push_back(color.has_value() ? color.value().w : 1.0f);
                out.has_colors = out.has_colors || color.has_value();
            }
        }
    }

    // One vertex attribute of a triangle soup, read through the vertex
    // format and converted to four floats whatever its storage encoding is.
    [[nodiscard]] static auto read_soup_attribute(
        const erhe::primitive::Triangle_soup&     soup,
        const erhe::dataformat::Vertex_attribute* attribute,
        const std::size_t                         stream_stride,
        const std::size_t                         vertex_index,
        glm::vec4&                                out_value
    ) -> bool
    {
        if (attribute == nullptr) {
            return false;
        }
        const std::size_t offset = (vertex_index * stream_stride) + attribute->offset;
        const std::size_t size   = erhe::dataformat::get_format_size_bytes(attribute->format);
        if ((offset + size) > soup.vertex_data.size()) {
            return false;
        }
        out_value = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
        erhe::dataformat::convert(
            soup.vertex_data.data() + offset, attribute->format,
            &out_value.x, erhe::dataformat::Format::format_32_vec4_float,
            1.0f
        );
        return true;
    }

    void append_triangle_soup(Mesh_accumulator& out, const erhe::primitive::Triangle_soup& soup)
    {
        using erhe::dataformat::Vertex_attribute;
        using erhe::dataformat::Vertex_attribute_usage;

        if (soup.vertex_format.streams.empty()) {
            add_warning("a triangle soup has no vertex stream - the primitive is not written");
            return;
        }
        const erhe::dataformat::Vertex_stream& stream   = soup.vertex_format.streams.front();
        const Vertex_attribute*                position = stream.find_attribute(Vertex_attribute_usage::position);
        if (position == nullptr) {
            add_warning("a triangle soup has no position attribute - the primitive is not written");
            return;
        }
        const Vertex_attribute* normal   = stream.find_attribute(Vertex_attribute_usage::normal);
        const Vertex_attribute* texcoord = stream.find_attribute(Vertex_attribute_usage::tex_coord);
        const Vertex_attribute* color    = stream.find_attribute(Vertex_attribute_usage::color);

        const std::size_t base         = out.points.size();
        const std::size_t stride       = stream.stride;
        const std::size_t vertex_count = (stride > 0) ? (soup.vertex_data.size() / stride) : 0;
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            glm::vec4 value{0.0f, 0.0f, 0.0f, 1.0f};
            const bool has_position = read_soup_attribute(soup, position, stride, vertex_index, value);
            out.points.push_back(
                has_position
                    ? lightusd::value::point3f{value.x, value.y, value.z}
                    : lightusd::value::point3f{0.0f, 0.0f, 0.0f}
            );
        }
        for (std::size_t triangle = 0; ((triangle * 3) + 2) < soup.index_data.size(); ++triangle) {
            out.face_vertex_counts.push_back(3);
            for (std::size_t local = 0; local < 3; ++local) {
                const std::size_t vertex_index = soup.index_data[(triangle * 3) + local];
                out.face_vertex_indices.push_back(static_cast<int32_t>(base + vertex_index));

                glm::vec4  value{0.0f, 0.0f, 0.0f, 1.0f};
                const bool has_normal = read_soup_attribute(soup, normal, stride, vertex_index, value);
                out.normals.push_back(
                    has_normal
                        ? lightusd::value::normal3f{value.x, value.y, value.z}
                        : lightusd::value::normal3f{0.0f, 0.0f, 0.0f}
                );
                out.has_normals = out.has_normals || has_normal;

                value = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
                const bool has_texcoord = read_soup_attribute(soup, texcoord, stride, vertex_index, value);
                out.texcoords.push_back(
                    has_texcoord
                        ? lightusd::value::texcoord2f{value.x, value.y}
                        : lightusd::value::texcoord2f{0.0f, 0.0f}
                );
                out.has_texcoords = out.has_texcoords || has_texcoord;

                value = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
                const bool has_color = read_soup_attribute(soup, color, stride, vertex_index, value);
                out.colors.push_back(lightusd::value::color3f{value.x, value.y, value.z});
                out.opacities.push_back(value.w);
                out.has_colors = out.has_colors || has_color;
            }
        }
    }

    // One primitive of an erhe mesh: the facets it contributed to the shared
    // arrays, and the material bound to them.
    class Facet_group final
    {
    public:
        std::string                      name;
        int32_t                          first_facet{0};
        int32_t                          facet_count{0};
        const erhe::primitive::Material* material   {nullptr};
    };

    [[nodiscard]] auto write_mesh_prim(
        const erhe::scene::Node& node,
        const erhe::scene::Mesh& mesh,
        const std::string&       prim_name,
        const glm::mat4&         matrix
    ) -> lightusd::Prim
    {
        ++m_mesh_count;
        lightusd::GeomMesh geom_mesh;
        geom_mesh.name = prim_name;
        set_transform(geom_mesh, matrix);
        // The authored polygons are the mesh, not a subdivision cage: this is
        // what makes the importer build erhe geometry rather than a triangle
        // soup (doc/usd_compatibility.md, geometry attributes).
        geom_mesh.subdivisionScheme.set_value(lightusd::GeomMesh::SubdivisionScheme::SubdivisionSchemeNone);

        Mesh_accumulator         accumulator;
        std::vector<Facet_group> groups;
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
            const erhe::primitive::Primitive_render_shape* render_shape =
                mesh_primitive.primitive ? mesh_primitive.primitive->render_shape.get() : nullptr;
            if (render_shape == nullptr) {
                continue;
            }
            const int32_t                                    first_facet = static_cast<int32_t>(accumulator.face_vertex_counts.size());
            const std::shared_ptr<erhe::geometry::Geometry>& geometry    = render_shape->get_geometry_const();
            if (geometry) {
                append_geometry(accumulator, *geometry.get());
            } else {
                const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = render_shape->get_triangle_soup();
                if (!soup) {
                    continue;
                }
                append_triangle_soup(accumulator, *soup.get());
            }
            const int32_t facet_count = static_cast<int32_t>(accumulator.face_vertex_counts.size()) - first_facet;
            if (facet_count == 0) {
                continue;
            }
            Facet_group group{};
            group.name        = geometry ? geometry->get_name() : fmt::format("{}_{}", prim_name, groups.size());
            group.first_facet = first_facet;
            group.facet_count = facet_count;
            group.material    = mesh_primitive.material.get();
            groups.push_back(std::move(group));
        }

        geom_mesh.points.set_value(accumulator.points);
        geom_mesh.faceVertexCounts.set_value(accumulator.face_vertex_counts);
        geom_mesh.faceVertexIndices.set_value(accumulator.face_vertex_indices);
        if (accumulator.has_normals) {
            geom_mesh.normals.set_value(accumulator.normals);
            geom_mesh.normals.metas().set_interpolation_enum(lightusd::Interpolation::FaceVarying);
        }
        if (accumulator.has_texcoords) {
            add_primvar(geom_mesh, "primvars:st", accumulator.texcoords);
        }
        if (accumulator.has_colors) {
            add_primvar(geom_mesh, "primvars:displayColor", accumulator.colors);
            add_primvar(geom_mesh, "primvars:displayOpacity", accumulator.opacities);
        }

        write_visibility_and_purpose(node, geom_mesh);
        write_erhe_properties(node, geom_mesh);
        write_erhe_properties(mesh, geom_mesh);

        // One primitive needs no subset: the importer's remainder group is
        // the whole mesh and reproduces it. Several primitives become one
        // materialBind GeomSubset each.
        if (groups.size() == 1) {
            bind_material(geom_mesh, groups.front().material);
        }
        lightusd::Prim prim{geom_mesh};
        if (groups.size() > 1) {
            Name_scope subset_names;
            for (const Facet_group& group : groups) {
                lightusd::GeomSubset subset;
                subset.name = subset_names.make_unique(group.name);
                subset.elementType.set_value(lightusd::GeomSubset::ElementType::Face);
                subset.familyName.set_value(lightusd::value::token{"materialBind"});
                std::vector<int32_t> indices;
                indices.reserve(static_cast<std::size_t>(group.facet_count));
                for (int32_t facet = 0; facet < group.facet_count; ++facet) {
                    indices.push_back(group.first_facet + facet);
                }
                subset.indices.set_value(std::move(indices));
                bind_material(subset, group.material);
                std::string error;
                if (!prim.add_child(lightusd::Prim{subset}, false, &error)) {
                    add_warning(fmt::format("subset '{}' of '{}' could not be added: {}", subset.name, prim_name, error));
                }
            }
        }
        return prim;
    }

    template <typename T, typename V>
    static void add_primvar(T& typed_prim, const std::string& name, const std::vector<V>& values)
    {
        lightusd::Attribute attribute;
        attribute.set_value(values);
        attribute.metas().set_interpolation_enum(lightusd::Interpolation::FaceVarying);
        typed_prim.props.emplace(name, lightusd::Property{attribute, false});
    }

    // -------------------------------------------------------------------
    // Cameras and lights
    // -------------------------------------------------------------------

    [[nodiscard]] auto write_camera_prim(
        const erhe::scene::Node&   node,
        const erhe::scene::Camera& camera,
        const std::string&         prim_name,
        const glm::mat4&           matrix
    ) -> lightusd::Prim
    {
        using erhe::scene::Camera;

        lightusd::GeomCamera geom_camera;
        geom_camera.name = prim_name;
        set_transform(geom_camera, matrix);

        if (is_local(camera, Camera::z_near_property.get()) || is_local(camera, Camera::z_far_property.get())) {
            geom_camera.clippingRange.set_value(
                lightusd::value::float2{
                    camera.get_value(Camera::z_near_property),
                    camera.get_value(Camera::z_far_property)
                }
            );
        }
        const erhe::scene::Projection::Type projection_type = camera.get_value(Camera::projection_type_property);
        if (projection_type == erhe::scene::Projection::Type::orthogonal) {
            geom_camera.projection.set_value(lightusd::GeomCamera::Projection::Orthographic);
            // A USD aperture is in tenths of a scene unit.
            if (is_local(camera, Camera::ortho_width_property.get())) {
                geom_camera.horizontalAperture.set_value(camera.get_value(Camera::ortho_width_property) * 10.0f);
            }
            if (is_local(camera, Camera::ortho_height_property.get())) {
                geom_camera.verticalAperture.set_value(camera.get_value(Camera::ortho_height_property) * 10.0f);
            }
        } else if (is_local(camera, Camera::fov_x_property.get()) || is_local(camera, Camera::fov_y_property.get())) {
            geom_camera.focalLength.set_value(c_export_focal_length);
            geom_camera.horizontalAperture.set_value(
                2.0f * c_export_focal_length * std::tan(0.5f * camera.get_value(Camera::fov_x_property))
            );
            geom_camera.verticalAperture.set_value(
                2.0f * c_export_focal_length * std::tan(0.5f * camera.get_value(Camera::fov_y_property))
            );
        }
        if (is_local(camera, Camera::exposure_property.get())) {
            geom_camera.exposure.set_value(camera.get_value(Camera::exposure_property));
        }
        if (is_local(camera, Camera::infinite_z_far_property.get()) && camera.get_value(Camera::infinite_z_far_property)) {
            add_warning(
                fmt::format(
                    "camera '{}' has an infinite far plane, which USD cannot express - clippingRange carries the finite range",
                    camera.get_name()
                )
            );
        }

        write_visibility_and_purpose(node, geom_camera);
        write_erhe_properties(node, geom_camera);
        write_erhe_properties(camera, geom_camera);
        return lightusd::Prim{geom_camera};
    }

    // The UsdLux inputs erhe fills, shared by every light type.
    template <typename T>
    static void write_light_api(const erhe::scene::Light& light, T& usd_light)
    {
        using erhe::scene::Light;

        if (is_local(light, Light::color_property.get())) {
            const glm::vec3 color = light.get_value(Light::color_property);
            usd_light.color.set_value(lightusd::value::color3f{color.x, color.y, color.z});
        }
        if (is_local(light, Light::intensity_property.get())) {
            // erhe has no exposure on a light, so the whole quantity is the
            // intensity and `inputs:exposure` stays at its zero fallback -
            // which is what the importer folds back in.
            usd_light.intensity.set_value(light.get_value(Light::intensity_property));
        }
        if (is_local(light, Light::temperature_property.get())) {
            usd_light.enableColorTemperature.set_value(true);
            usd_light.colorTemperature.set_value(light.get_value(Light::temperature_property));
        }
        if (is_local(light, Light::cast_shadow_property.get())) {
            usd_light.shadowEnable.set_value(light.get_value(Light::cast_shadow_property));
        }
    }

    [[nodiscard]] auto write_light_prim(
        const erhe::scene::Node&  node,
        const erhe::scene::Light& light,
        const std::string&        prim_name,
        const glm::mat4&          matrix
    ) -> lightusd::Prim
    {
        using erhe::scene::Light;
        using erhe::scene::Light_type;

        const Light_type light_type = light.get_value(Light::light_type_property);
        if (light_type == Light_type::directional) {
            lightusd::DistantLight distant_light;
            distant_light.name = prim_name;
            set_transform(distant_light, matrix);
            write_light_api(light, distant_light);
            write_visibility_and_purpose(node, distant_light);
            write_erhe_properties(node, distant_light);
            write_erhe_properties(light, distant_light);
            return lightusd::Prim{distant_light};
        }

        lightusd::SphereLight sphere_light;
        sphere_light.name = prim_name;
        set_transform(sphere_light, matrix);
        // A point light is a sphere light of no extent.
        sphere_light.radius.set_value(0.0f);
        write_light_api(light, sphere_light);
        if (light_type == Light_type::spot) {
            const float outer = light.get_value(Light::outer_spot_angle_property);
            const float inner = light.get_value(Light::inner_spot_angle_property);
            sphere_light.shapingConeAngle.set_value(glm::degrees(outer));
            sphere_light.shapingConeSoftness.set_value((outer > 0.0f) ? std::max(0.0f, 1.0f - (inner / outer)) : 0.0f);
        }
        write_visibility_and_purpose(node, sphere_light);
        write_erhe_properties(node, sphere_light);
        write_erhe_properties(light, sphere_light);

        lightusd::Prim prim{sphere_light};
        if (light_type == Light_type::spot) {
            // The applied ShapingAPI, not the cone attributes, is what makes
            // an imported sphere light a spot light (doc/usd_compatibility.md,
            // lights).
            apply_api_schema(prim, lightusd::APISchemas::APIName::ShapingAPI, "ShapingAPI");
        }
        return prim;
    }

    static void apply_api_schema(lightusd::Prim& prim, const lightusd::APISchemas::APIName name, const std::string& instance_name)
    {
        lightusd::APISchemas& schemas = prim.metas().get_apiSchemas_mutable();
        schemas.names.emplace_back(name, instance_name);
    }

    // -------------------------------------------------------------------
    // Tags
    // -------------------------------------------------------------------

    // Item tags become UsdCollectionAPI collections on the default prim
    // (doc/usd_compatibility.md, object model): one collection per tag whose
    // `includes` names every prim carrying it. The paths are collected while
    // the prims are written, because a prim path is only known once the
    // sanitized, sibling-unique names of its ancestors are.
    void record_tags(const erhe::scene::Node& node)
    {
        const std::set<std::string>& tags = node.get_tags();
        if (tags.empty()) {
            return;
        }
        const std::string path = current_prim_path();
        for (const std::string& tag : tags) {
            m_tag_members[sanitize_usd_identifier(tag)].push_back(path);
        }
    }

    [[nodiscard]] auto current_prim_path() const -> std::string
    {
        std::string path;
        for (const std::string& component : m_prim_path_stack) {
            path += "/";
            path += component;
        }
        return path;
    }

    void prefix_tag_member_paths(const std::string& prefix)
    {
        for (std::pair<const std::string, std::vector<std::string>>& entry : m_tag_members) {
            for (std::string& path : entry.second) {
                path.insert(0, prefix);
            }
        }
    }

    // The collections go on the typed value the prim holds, which is where
    // UsdCollectionAPI's instances live; every prim type the writer can put
    // at the top level is asked in turn.
    void add_collections(lightusd::Prim& prim)
    {
        if (m_tag_members.empty()) {
            return;
        }
        const bool added =
            add_collections_to<lightusd::Xform       >(prim) ||
            add_collections_to<lightusd::GeomMesh    >(prim) ||
            add_collections_to<lightusd::GeomCamera  >(prim) ||
            add_collections_to<lightusd::SphereLight >(prim) ||
            add_collections_to<lightusd::DistantLight>(prim);
        if (!added) {
            add_warning("the default prim cannot hold collections - the item tags are not written");
            return;
        }
        for (const std::pair<const std::string, std::vector<std::string>>& entry : m_tag_members) {
            apply_api_schema(prim, lightusd::APISchemas::APIName::CollectionAPI, entry.first);
        }
    }

    template <typename T>
    [[nodiscard]] auto add_collections_to(lightusd::Prim& prim) -> bool
    {
        T* typed = prim.get_data().as<T>();
        if (typed == nullptr) {
            return false;
        }
        for (const std::pair<const std::string, std::vector<std::string>>& entry : m_tag_members) {
            std::vector<lightusd::Path> paths;
            paths.reserve(entry.second.size());
            for (const std::string& path : entry.second) {
                paths.emplace_back(path, "");
            }
            lightusd::Relationship relationship;
            relationship.set(std::move(paths));
            typed->get_or_add_instance(entry.first).includes = relationship;
        }
        return true;
    }

    const Usd_save_arguments& m_arguments;
    Usd_save_result&          m_result;

    std::optional<lightusd::Prim>                                           m_materials_prim;
    std::map<const erhe::primitive::Material*, std::string>                 m_material_paths;
    std::map<std::pair<std::size_t, Usd_material_texture_slot>, std::string> m_texture_shader_names;
    std::set<std::size_t>                                                   m_uv_reader_materials;
    std::map<std::string, std::vector<std::string>>                         m_tag_members;
    std::vector<std::string>                                                m_prim_path_stack;
    std::size_t                                                             m_node_count{0};
    std::size_t                                                             m_mesh_count{0};
};

} // anonymous namespace

auto sanitize_usd_identifier(const std::string_view name) -> std::string
{
    std::string result;
    result.reserve(name.size() + 1);
    for (const char character : name) {
        const bool is_identifier_character =
            ((character >= 'A') && (character <= 'Z')) ||
            ((character >= 'a') && (character <= 'z')) ||
            ((character >= '0') && (character <= '9')) ||
            (character == '_');
        result.push_back(is_identifier_character ? character : '_');
    }
    if (result.empty()) {
        return std::string{"_"};
    }
    if ((result.front() >= '0') && (result.front() <= '9')) {
        result.insert(result.begin(), '_');
    }
    return result;
}

auto save_usda(const Usd_save_arguments& arguments) -> Usd_save_result
{
    ERHE_PROFILE_FUNCTION();

    Usd_save_result result{};
    Exporter        exporter{arguments, result};
    exporter.write();
    return result;
}

} // namespace erhe::usd
