// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "parsers/gltf.hpp"
#include "editor_log.hpp"

#include "scene/content_library.hpp"
#include "scene/material_library.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/primitive/primitive_builder.hpp"

#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/profile.hpp"

extern "C" {
    #include "cgltf.h"
}

#include <glm/glm.hpp>
#include <gsl/gsl>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>

namespace editor {

namespace {

const char* c_str(const glm::vec3::length_type axis)
{
    switch (axis)
    {
        case glm::vec3::length_type{0}: return "X";
        case glm::vec3::length_type{1}: return "Y";
        case glm::vec3::length_type{2}: return "Z";
        default: return "?";
    }
}

auto c_str(const cgltf_result value) -> const char*
{
    switch (value)
    {
        case cgltf_result::cgltf_result_success:         return "success";
        case cgltf_result::cgltf_result_data_too_short:  return "data too short";
        case cgltf_result::cgltf_result_unknown_format:  return "unknown format";
        case cgltf_result::cgltf_result_invalid_json:    return "invalid json";
        case cgltf_result::cgltf_result_invalid_gltf:    return "invalid gltf";
        case cgltf_result::cgltf_result_invalid_options: return "invalid options";
        case cgltf_result::cgltf_result_file_not_found:  return "file not found";
        case cgltf_result::cgltf_result_io_error:        return "io error";
        case cgltf_result::cgltf_result_out_of_memory:   return "out of memory";
        case cgltf_result::cgltf_result_legacy_gltf:     return "legacy gltf";
        default:                                         return "?";
    }
}

auto c_str(const cgltf_attribute_type value) -> const char*
{
    switch (value)
    {
        case cgltf_attribute_type::cgltf_attribute_type_invalid:  return "invalid";
        case cgltf_attribute_type::cgltf_attribute_type_position: return "position";
        case cgltf_attribute_type::cgltf_attribute_type_normal:   return "normal";
        case cgltf_attribute_type::cgltf_attribute_type_tangent:  return "tangent";
        case cgltf_attribute_type::cgltf_attribute_type_texcoord: return "texcoord";
        case cgltf_attribute_type::cgltf_attribute_type_color:    return "color";
        case cgltf_attribute_type::cgltf_attribute_type_joints:   return "joints";
        case cgltf_attribute_type::cgltf_attribute_type_weights:  return "weights";
        default:                                                  return "?";
    }
}

auto c_str(const cgltf_buffer_view_type value) -> const char*
{
    switch (value)
    {
        case cgltf_buffer_view_type::cgltf_buffer_view_type_invalid:  return "invalid";
        case cgltf_buffer_view_type::cgltf_buffer_view_type_vertices: return "vertices";
        case cgltf_buffer_view_type::cgltf_buffer_view_type_indices:  return "indices";
        default:                                                      return "?";
    }
};

// auto c_str(const cgltf_component_type value) -> const char*
// {
//     switch (value)
//     {
//         case cgltf_component_type::cgltf_component_type_invalid : return "invalid";
//         case cgltf_component_type::cgltf_component_type_r_8     : return "r_8";
//         case cgltf_component_type::cgltf_component_type_r_8u    : return "r_8u";
//         case cgltf_component_type::cgltf_component_type_r_16    : return "r_16";
//         case cgltf_component_type::cgltf_component_type_r_16u   : return "r_16u";
//         case cgltf_component_type::cgltf_component_type_r_32u   : return "r_32u";
//         case cgltf_component_type::cgltf_component_type_r_32f   : return "r_32f";
//         default:                                                  return "?";
//     }
// };

auto c_str(const cgltf_type value) -> const char*
{
    switch (value)
    {
        case cgltf_type::cgltf_type_invalid: return "invalid";
        case cgltf_type::cgltf_type_scalar:  return "scalar";
        case cgltf_type::cgltf_type_vec2:    return "vec2";
        case cgltf_type::cgltf_type_vec3:    return "vec3";
        case cgltf_type::cgltf_type_vec4:    return "vec4";
        case cgltf_type::cgltf_type_mat2:    return "mat2";
        case cgltf_type::cgltf_type_mat3:    return "mat3";
        case cgltf_type::cgltf_type_mat4:    return "mat4";
        default:                             return "?";
    }
};

auto c_str(const cgltf_primitive_type value) -> const char*
{
    switch (value)
    {
        case cgltf_primitive_type::cgltf_primitive_type_points:         return "points";
        case cgltf_primitive_type::cgltf_primitive_type_lines:          return "lines";
        case cgltf_primitive_type::cgltf_primitive_type_line_loop:      return "line_loop";
        case cgltf_primitive_type::cgltf_primitive_type_line_strip:     return "line_strip";
        case cgltf_primitive_type::cgltf_primitive_type_triangles:      return "triangles";
        case cgltf_primitive_type::cgltf_primitive_type_triangle_strip: return "triangle_strip";
        case cgltf_primitive_type::cgltf_primitive_type_triangle_fan:   return "triangle_fan";
        default:                                                        return "?";
    }
};

// auto c_str(const cgltf_alpha_mode value) -> const char*
// {
//     switch (value)
//     {
//         case cgltf_alpha_mode::cgltf_alpha_mode_opaque: return "opaque";
//         case cgltf_alpha_mode::cgltf_alpha_mode_mask:   return "blend";
//         case cgltf_alpha_mode::cgltf_alpha_mode_blend:  return "mask";
//         default:                                        return "?";
//     }
// };

// auto c_str(const cgltf_animation_path_type value) -> const char*
// {
//     switch (value)
//     {
//         case cgltf_animation_path_type::cgltf_animation_path_type_invalid:     return "invalid";
//         case cgltf_animation_path_type::cgltf_animation_path_type_translation: return "translation";
//         case cgltf_animation_path_type::cgltf_animation_path_type_rotation:    return "rotation";
//         case cgltf_animation_path_type::cgltf_animation_path_type_scale:       return "scale";
//         case cgltf_animation_path_type::cgltf_animation_path_type_weights:     return "weights";
//         default:                 return "?";
//     }
// };

// auto c_str(const cgltf_interpolation_type value) -> const char*
// {
//     switch (value)
//     {
//         case cgltf_interpolation_type::cgltf_interpolation_type_linear:       return "linear";
//         case cgltf_interpolation_type::cgltf_interpolation_type_step:         return "step";
//         case cgltf_interpolation_type::cgltf_interpolation_type_cubic_spline: return "cubicspline";
//         default:                                                              return "?";
//     }
// };

// auto c_str(const cgltf_camera_type value) -> const char*
// {
//     switch (value)
//     {
//         case cgltf_camera_type::cgltf_camera_type_invalid:      return "invalid";
//         case cgltf_camera_type::cgltf_camera_type_perspective:  return "perspective";
//         case cgltf_camera_type::cgltf_camera_type_orthographic: return "orthographic";
//         default:                                                return "?";
//     }
// };

auto safe_str(const char* text)
{
    return (text != nullptr) ? text : "(null)";
}

} // anonymous namespace

using namespace glm;
using namespace erhe::geometry;


auto to_erhe(const cgltf_light_type gltf_light_type) -> erhe::scene::Light_type
{
    switch (gltf_light_type)
    {
        case cgltf_light_type::cgltf_light_type_invalid:     return erhe::scene::Light_type::directional;
        case cgltf_light_type::cgltf_light_type_directional: return erhe::scene::Light_type::directional;
        case cgltf_light_type::cgltf_light_type_point:       return erhe::scene::Light_type::point;
        case cgltf_light_type::cgltf_light_type_spot:        return erhe::scene::Light_type::spot;
        default:                                             return erhe::scene::Light_type::directional;
    }
}

auto to_erhe(
    const cgltf_attribute_type gltf_attribute_type
) -> std::optional<erhe::geometry::Property_map_descriptor>
{
    switch (gltf_attribute_type)
    {
        case cgltf_attribute_type::cgltf_attribute_type_invalid:  return {};
        case cgltf_attribute_type::cgltf_attribute_type_position: return erhe::geometry::c_point_locations;
        case cgltf_attribute_type::cgltf_attribute_type_normal:   return erhe::geometry::c_corner_normals;
        case cgltf_attribute_type::cgltf_attribute_type_tangent:  return erhe::geometry::c_corner_tangents;
        case cgltf_attribute_type::cgltf_attribute_type_texcoord: return erhe::geometry::c_corner_texcoords;
        case cgltf_attribute_type::cgltf_attribute_type_color:    return erhe::geometry::c_corner_colors;
        case cgltf_attribute_type::cgltf_attribute_type_joints:   return {}; // TODO
        case cgltf_attribute_type::cgltf_attribute_type_weights:  return {}; // TODO
        default:                                                  return {};
    }
}

class Geometry_entry
{
public:
    cgltf_size                                index_accessor;
    std::vector<cgltf_size>                   attribute_accessors;
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    erhe::primitive::Primitive_geometry       gl_primitive_geometry;
    std::shared_ptr<Node_raytrace>            node_raytrace;
    Raytrace_primitive*                       raytrace_primitive;
};

using Item_flags = erhe::scene::Item_flags;

class Gltf_parser
{
public:
    const cgltf_size null_index{std::numeric_limits<cgltf_size>::max()};

    Gltf_parser(
        const std::shared_ptr<Scene_root>& scene_root,
        erhe::primitive::Build_info&       build_info,
        const std::filesystem::path&       path
    )
        : m_scene_root{scene_root}
        , m_build_info{build_info}
    {
        if (!open(path))
        {
            return;
        }

        trace_info();
    }

    void parse_and_build()
    {
        m_materials.reserve(m_data->materials_count);
        for (cgltf_size i = 0; i < m_data->materials_count; ++i)
        {
            parse_material(&m_data->materials[i]);
        }

        // TODO:
        //  - images
        //  - textures
        //  - samplers
        //  - animations
        //  - skins

        for (cgltf_size i = 0; i < m_data->scenes_count; ++i)
        {
            parse_scene(&m_data->scenes[i]);
        }
    }

private:
    auto open(const std::filesystem::path& path) -> bool
    {
        const cgltf_options parse_options
        {
            .type             = cgltf_file_type_invalid, // auto
            .json_token_count = 0, // 0 == auto
            .memory = {
                .alloc_func   = nullptr,
                .free_func    = nullptr,
                .user_data    = nullptr
            },
            .file = {
                .read         = nullptr,
                .release      = nullptr,
                .user_data    = nullptr
            }
        };

        const cgltf_result parse_result = cgltf_parse_file(
            &parse_options,
            path.string().c_str(),
            &m_data
        );

        if (parse_result != cgltf_result::cgltf_result_success)
        {
            log_parsers->error("glTF parse error: {}", c_str(parse_result));
            return false;
        }

        const cgltf_result load_buffers_result = cgltf_load_buffers(&parse_options, m_data, path.string().c_str());
        if (load_buffers_result != cgltf_result::cgltf_result_success)
        {
            log_parsers->error("glTF load buffers error: {}", c_str(load_buffers_result));
            return false;
        }

        return true;
    }
    void trace_info() const
    {
        if (m_data->asset.version != nullptr)
        {
            log_parsers->trace("Asset Version:    {}", m_data->asset.version);
        }
        if (m_data->asset.min_version != nullptr)
        {
            log_parsers->trace("Asset MinVersion: {}", m_data->asset.min_version);
        }
        if (m_data->asset.generator != nullptr)
        {
            log_parsers->trace("Asset Generator:  {}", m_data->asset.generator);
        }
        if (m_data->asset.copyright != nullptr)
        {
            log_parsers->trace("Asset Copyright:  {}", m_data->asset.copyright);
        }
        log_parsers->trace("Node Count:       {}", m_data->nodes_count);
        log_parsers->trace("Camera Count:     {}", m_data->cameras_count);
        log_parsers->trace("Light Count:      {}", m_data->lights_count);
        log_parsers->trace("Material Count:   {}", m_data->materials_count);
        log_parsers->trace("Mesh Count:       {}", m_data->meshes_count);
        log_parsers->trace("Skin Count:       {}", m_data->skins_count);
        log_parsers->trace("Image Count:      {}", m_data->images_count);
        log_parsers->trace("Texture Count:    {}", m_data->textures_count);
        log_parsers->trace("Sampler Count:    {}", m_data->samplers_count);
        log_parsers->trace("Buffer Count:     {}", m_data->buffers_count);
        log_parsers->trace("BufferView Count: {}", m_data->buffer_views_count);
        log_parsers->trace("Accessor Count:   {}", m_data->accessors_count);
        log_parsers->trace("Animation Count:  {}", m_data->animations_count);
        log_parsers->trace("Scene Count:      {}", m_data->scenes_count);

        for (cgltf_size i = 0; i < m_data->extensions_used_count; ++i)
        {
            log_parsers->trace("Extension Used: {}", m_data->extensions_used[i]);
        }

        for (cgltf_size i = 0; i < m_data->extensions_required_count; ++i)
        {
            log_parsers->trace("Extension Required: {}", m_data->extensions_required[i]);
        }
    }

    void parse_material(cgltf_material* material)
    {
        const cgltf_size material_index = material - m_data->materials;
        log_parsers->trace(
            "Primitive material: id = {}, name = {}",
            material_index,
            safe_str(material->name)
        );

        auto new_material = m_scene_root->content_library()->materials.make(material->name);
        m_materials.push_back(new_material);
        if (material->has_pbr_metallic_roughness)
        {
            const cgltf_pbr_metallic_roughness& pbr_metallic_roughness = material->pbr_metallic_roughness;
            new_material->base_color = glm::vec4{
                pbr_metallic_roughness.base_color_factor[0],
                pbr_metallic_roughness.base_color_factor[1],
                pbr_metallic_roughness.base_color_factor[2],
                pbr_metallic_roughness.base_color_factor[3]
            };
            new_material->metallic      = pbr_metallic_roughness.metallic_factor;
            new_material->roughness.x   = pbr_metallic_roughness.roughness_factor;
            new_material->roughness.y   = pbr_metallic_roughness.roughness_factor;
            new_material->emissive      = glm::vec4{0.0f, 0.0f, 0.0f, 0.0f};
            new_material->m_shown_in_ui = true;
            log_parsers->trace(
                "Material PBR metallic roughness base color factor = {}, {}, {}, {}",
                pbr_metallic_roughness.base_color_factor[0],
                pbr_metallic_roughness.base_color_factor[1],
                pbr_metallic_roughness.base_color_factor[2],
                pbr_metallic_roughness.base_color_factor[3]
            );
            log_parsers->trace(
                "Material PBR metallic roughness metallic factor = {}",
                pbr_metallic_roughness.metallic_factor
            );
            log_parsers->trace(
                "Material PBR metallic roughness roughness factor = {}",
                pbr_metallic_roughness.roughness_factor
            );
        }
        if (material->has_pbr_specular_glossiness)
        {
            const cgltf_pbr_specular_glossiness& pbr_specular_glossiness = material->pbr_specular_glossiness;
            log_parsers->trace(
                "Material PBR specular glossiness diffuse factor = {}, {}, {}, {}",
                pbr_specular_glossiness.diffuse_factor[0],
                pbr_specular_glossiness.diffuse_factor[1],
                pbr_specular_glossiness.diffuse_factor[2],
                pbr_specular_glossiness.diffuse_factor[3]
            );
            log_parsers->trace(
                "Material PBR specular glossiness specular factor = {}, {}, {}",
                pbr_specular_glossiness.specular_factor[0],
                pbr_specular_glossiness.specular_factor[1],
                pbr_specular_glossiness.specular_factor[2]
            );
            log_parsers->trace(
                "Material PBR specular glossiness glossiness factor = {}",
                pbr_specular_glossiness.glossiness_factor
            );
            log_parsers->warn("Material PBR specular glossiness is not yet implemented");
        }
    }
    void parse_node_transform(
        cgltf_node*                               node,
        const std::shared_ptr<erhe::scene::Node>& erhe_node
    )
    {
        cgltf_float m[16];
        cgltf_node_transform_local(node, &m[0]);
        const glm::mat4 matrix{
            m[ 0], m[ 1], m[ 2], m[ 3],
            m[ 4], m[ 5], m[ 6], m[ 7],
            m[ 8], m[ 9], m[10], m[11],
            m[12], m[13], m[14], m[15]
        };
        erhe_node->set_parent_from_node(matrix);
    }
    void parse_camera(cgltf_node* node)
    {
        cgltf_camera* camera = node->camera;
        const cgltf_size node_index   = node   - m_data->nodes;
        const cgltf_size camera_index = camera - m_data->cameras;
        log_parsers->trace(
            "Camera: node_index = {}, camera index = {}, name = {}",
            node_index, camera_index, safe_str(camera->name)
        );

        auto  erhe_node  = m_nodes.at(node_index);
        auto  new_camera = m_scene_root->content_library()->cameras.make(camera->name);
        new_camera->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        auto* projection = new_camera->projection();
        switch (camera->type)
        {
            case cgltf_camera_type::cgltf_camera_type_perspective:
            {
                const cgltf_camera_perspective& perspective = camera->data.perspective;
                log_parsers->trace("Camera.has_aspect_ration: {}", perspective.has_aspect_ratio);
                log_parsers->trace("Camera.aspect_ratio:      {}", perspective.aspect_ratio);
                log_parsers->trace("Camera.yfov:              {}", perspective.yfov);
                log_parsers->trace("Camera.has_zfar:          {}", perspective.has_zfar);
                log_parsers->trace("Camera.zfar:              {}", perspective.zfar);
                log_parsers->trace("Camera.znear:             {}", perspective.znear);
                projection->projection_type = erhe::scene::Projection::Type::perspective_vertical;
                projection->fov_y           = perspective.yfov;
                projection->z_far           = (perspective.has_zfar != 0)
                    ? perspective.zfar
                    : 0.0f;
                break;
            }

            case cgltf_camera_type::cgltf_camera_type_orthographic:
            {
                const cgltf_camera_orthographic& orthographic = camera->data.orthographic;
                log_parsers->trace("Camera.xmag:              {}", orthographic.xmag);
                log_parsers->trace("Camera.ymag:              {}", orthographic.ymag);
                log_parsers->trace("Camera.zfar:              {}", orthographic.zfar);
                log_parsers->trace("Camera.znear:             {}", orthographic.znear);
                projection->projection_type = erhe::scene::Projection::Type::orthogonal;
                projection->ortho_width     = orthographic.xmag;
                projection->ortho_height    = orthographic.ymag;
                projection->z_far           = orthographic.zfar;
                projection->z_near          = orthographic.znear;
                break;
            }

            default:
            {
                log_parsers->warn("Camera.Projection: unknown projection type {}");
                break;
            }
        }

        erhe_node->attach(new_camera);
    }
    void parse_light(cgltf_node* node)
    {
        cgltf_light* light = node->light;
        const cgltf_size node_index  = node  - m_data->nodes;
        const cgltf_size light_index = light - m_data->lights;
        log_parsers->trace(
            "Light: node_index = {}, camera index = {}, name = {}",
            node_index, light_index, safe_str(light->name)
        );

        auto erhe_node = m_nodes.at(node_index);
        auto new_light = m_scene_root->content_library()->lights.make(light->name);
        new_light->color = glm::vec3{
            light->color[0],
            light->color[1],
            light->color[2]
        };
        new_light->intensity        = light->intensity;
        new_light->type             = to_erhe(light->type);
        new_light->range            = light->range;
        new_light->inner_spot_angle = light->spot_inner_cone_angle;
        new_light->outer_spot_angle = light->spot_outer_cone_angle;

        new_light->layer_id = m_scene_root->layers().light()->id;
        new_light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

        erhe_node->attach(new_light);
    }

    class Primitive_context
    {
    public:
        cgltf_primitive*                          primitive                     {nullptr};
        std::shared_ptr<erhe::geometry::Geometry> erhe_geometry                 {};
        cgltf_size                                primitive_min_index           {0};
        cgltf_size                                primitive_max_index           {0};
        std::vector<cgltf_size>                   primitive_used_indices        {};
        std::vector<glm::vec3>                    vertex_positions              {};
        std::vector<cgltf_size>                   sorted_vertex_indices         {};
        std::vector<erhe::geometry::Point_id>     erhe_point_id_from_gltf_index {};
        erhe::geometry::Corner_id                 corner_id_start               {};
        erhe::geometry::Corner_id                 corner_id_end                 {};
        std::vector<cgltf_size>                   gltf_index_from_corner_id     {};
        std::shared_ptr<Raytrace_primitive>       erhe_raytrace_primitive       {};
    };

    void parse_primitive_attribute(
        Primitive_context& context,
        cgltf_attribute*   attribute
    )
    {
        const cgltf_size         attribute_index = attribute - context.primitive->attributes;
        const cgltf_accessor*    accessor        = attribute->data;
        const cgltf_buffer_view* buffer_view     = accessor->buffer_view;
        const intptr_t           accessor_id     = accessor    - m_data->accessors;
        const intptr_t           buffer_view_id  = buffer_view - m_data->buffer_views;

        log_parsers->trace(
            "Primitive attribute {}: index = {}, name = {}, type = {}",
            attribute_index,
            attribute->index,
            safe_str(attribute->name),
            c_str(attribute->type)
        );

        log_parsers->trace(
            "    Accessor: id = {}, normalized = {}, type = {}, offset = {}, count = {}, stride = {}",
            accessor_id,
            accessor->normalized,
            c_str(accessor->type),
            accessor->offset,
            accessor->count,
            accessor->stride
        );

        log_parsers->trace(
            "    Buffer view: id = {}, offset = {}, size = {}, stride = {}, type = {}",
            buffer_view_id,
            buffer_view->offset,
            buffer_view->size,
            buffer_view->stride,
            c_str(buffer_view->type)
        );

        auto property_descriptor_opt = to_erhe(attribute->type);
        if (!property_descriptor_opt.has_value())
        {
            log_parsers->warn("    Attribute not yet supported");
            return;
        }

        // TODO This is very very inefficient.
        // TODO Connectivity
        auto& property_descriptor = property_descriptor_opt.value();
        auto& point_attributes    = context.erhe_geometry->point_attributes();
        auto& corner_attributes   = context.erhe_geometry->corner_attributes();

        switch (accessor->type)
        {
            case cgltf_type::cgltf_type_scalar:
            {
                if (attribute->type == cgltf_attribute_type::cgltf_attribute_type_position)
                {
                    auto* property_map = point_attributes.create<float>(property_descriptor);
                    for (cgltf_size index : context.primitive_used_indices)
                    {
                        cgltf_float v;
                        cgltf_accessor_read_float(accessor, index, &v, 1);
                        const auto p = context.erhe_point_id_from_gltf_index.at(index - context.primitive_min_index);
                        property_map->put(p, v);
                    }
                }
                else
                {
                    auto* property_map = corner_attributes.create<float>(property_descriptor);
                    for (erhe::geometry::Corner_id corner_id = context.corner_id_start; corner_id != context.corner_id_end; ++corner_id)
                    {
                        const cgltf_size index = context.gltf_index_from_corner_id.at(corner_id - context.corner_id_start);
                        cgltf_float v;
                        cgltf_accessor_read_float(accessor, index, &v, 1);
                        property_map->put(corner_id, v);
                    }
                }
                break;
            }

            case cgltf_type::cgltf_type_vec2:
            {
                if (attribute->type == cgltf_attribute_type::cgltf_attribute_type_position)
                {
                    auto* property_map = point_attributes.create<glm::vec2>(property_descriptor);
                    for (cgltf_size index : context.primitive_used_indices)
                    {
                        cgltf_float v[2];
                        cgltf_accessor_read_float(accessor, index, &v[0], 2);
                        const auto value = glm::vec2{v[0], v[1]};
                        const auto p = context.erhe_point_id_from_gltf_index.at(index - context.primitive_min_index);
                        property_map->put(p, value);
                    }
                }
                else
                {
                    auto* property_map = corner_attributes.create<glm::vec2>(property_descriptor);
                    for (erhe::geometry::Corner_id corner_id = context.corner_id_start; corner_id != context.corner_id_end; ++corner_id)
                    {
                        const cgltf_size index = context.gltf_index_from_corner_id.at(corner_id - context.corner_id_start);
                        cgltf_float v[2];
                        cgltf_accessor_read_float(accessor, index, &v[0], 2);
                        const auto value = glm::vec2{v[0], v[1]};
                        property_map->put(corner_id, value);
                    }
                }
                break;
            }

            case cgltf_type::cgltf_type_vec3:
            {
                if (attribute->type == cgltf_attribute_type::cgltf_attribute_type_position)
                {
                    auto* property_map = point_attributes.create<glm::vec3>(property_descriptor);
                    for (cgltf_size index : context.primitive_used_indices)
                    {
                        cgltf_float v[3];
                        cgltf_accessor_read_float(accessor, index, &v[0], 3);
                        const auto value = glm::vec3{v[0], v[1], v[2]};
                        const auto p = context.erhe_point_id_from_gltf_index.at(index - context.primitive_min_index);
                        property_map->put(p, value);
                    }
                }
                else
                {
                    auto* property_map = corner_attributes.create<glm::vec3>(property_descriptor);
                    for (erhe::geometry::Corner_id corner_id = context.corner_id_start; corner_id != context.corner_id_end; ++corner_id)
                    {
                        const cgltf_size index = context.gltf_index_from_corner_id.at(corner_id - context.corner_id_start);
                        cgltf_float v[3];
                        cgltf_accessor_read_float(accessor, index, &v[0], 3);
                        const auto value = glm::vec3{v[0], v[1], v[2]};
                        property_map->put(corner_id, value);
                    }
                }
                break;
            }

            case cgltf_type::cgltf_type_vec4:
            {
                if (attribute->type == cgltf_attribute_type::cgltf_attribute_type_position)
                {
                    auto* property_map = point_attributes.create<glm::vec4>(property_descriptor);
                    for (cgltf_size index : context.primitive_used_indices)
                    {
                        cgltf_float v[4];
                        cgltf_accessor_read_float(accessor, index, &v[0], 4);
                        const auto value = glm::vec4{v[0], v[1], v[2], v[3]};
                        const auto p = context.erhe_point_id_from_gltf_index.at(index - context.primitive_min_index);
                        property_map->put(p, value);
                    }
                }
                else
                {
                    auto* property_map = corner_attributes.create<glm::vec4>(property_descriptor);
                    for (erhe::geometry::Corner_id corner_id = context.corner_id_start; corner_id != context.corner_id_end; ++corner_id)
                    {
                        const cgltf_size index = context.gltf_index_from_corner_id.at(corner_id - context.corner_id_start);
                        cgltf_float v[4];
                        cgltf_accessor_read_float(accessor, index, &v[0], 4);
                        const auto value = glm::vec4{v[0], v[1], v[2], v[3]};
                        property_map->put(corner_id, value);
                    }
                }
                break;
            }
            case cgltf_type::cgltf_type_mat2:
            case cgltf_type::cgltf_type_mat3:
            case cgltf_type::cgltf_type_mat4:
            case cgltf_type::cgltf_type_invalid:
            default:
            {
                abort();
                // TODO
            }
        }
    }

    void parse_primitive_used_indices(Primitive_context& context)
    {
        const cgltf_accessor* accessor = context.primitive->indices;

        for (cgltf_size i = 0; i < accessor->count; ++i)
        {
            const cgltf_size index = cgltf_accessor_read_index(accessor, i);
            context.primitive_used_indices.push_back(index);
        }

        std::sort(context.primitive_used_indices.begin(), context.primitive_used_indices.end());
        context.primitive_used_indices.erase(
            std::unique(
                context.primitive_used_indices.begin(),
                context.primitive_used_indices.end()
            ),
            context.primitive_used_indices.end()
        );

        context.primitive_max_index = cgltf_size{0};
        context.primitive_min_index = std::numeric_limits<cgltf_size>::max();
        for (const cgltf_size index : context.primitive_used_indices)
        {
            context.primitive_min_index = std::min(index, context.primitive_min_index);
            context.primitive_max_index = std::max(index, context.primitive_max_index);
        }
        log_parsers->trace("min index = {}", context.primitive_min_index);
        log_parsers->trace("max index = {}", context.primitive_max_index);
        log_parsers->trace("index ranges size = {}", context.primitive_max_index - context.primitive_min_index + 1);
        log_parsers->trace("unique index count = {}", context.primitive_used_indices.size());
    }

    void parse_primitive_make_points(Primitive_context& context)
    {
        cgltf_attribute* position_attribute{nullptr};
        cgltf_int min_index = std::numeric_limits<cgltf_int>::max();

        for (cgltf_size i = 0; i < context.primitive->attributes_count; ++i)
        {
            cgltf_attribute* attribute = &context.primitive->attributes[i];
            if (
                (attribute->type == cgltf_attribute_type::cgltf_attribute_type_position) &&
                (attribute->index < min_index)
            )
            {
                position_attribute = attribute;
                min_index = attribute->index;
            }
        }
        if (position_attribute == nullptr)
        {
            log_parsers->error("No vertex position attribute found");
            return;
        }

        // Get vertex positions and calculate bounding box
        context.vertex_positions.clear();
        cgltf_accessor* accessor = position_attribute->data;

        const cgltf_size num_components = std::min(
            cgltf_size{3},
            cgltf_num_components(accessor->type)
        );
        vec3 min_corner{std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max()};
        vec3 max_corner{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        const cgltf_size vertex_count = context.primitive_max_index - context.primitive_min_index + 1;
        context.vertex_positions.resize(vertex_count);
        switch (accessor->type)
        {
            case cgltf_type::cgltf_type_scalar:
            case cgltf_type::cgltf_type_vec2:
            case cgltf_type::cgltf_type_vec3:
            case cgltf_type::cgltf_type_vec4:
            {
                for (cgltf_size index : context.primitive_used_indices)
                {
                    cgltf_float v[3] = { 0.0f, 0.0f, 0.0f };
                    cgltf_accessor_read_float(accessor, index, &v[0], num_components);
                    const auto position = glm::vec3{v[0], v[1], v[2]};
                    context.vertex_positions.at(index - context.primitive_min_index) = position;
                    min_corner = glm::min(min_corner, position);
                    max_corner = glm::max(max_corner, position);
                }
                break;
            }
            case cgltf_type::cgltf_type_mat2:
            case cgltf_type::cgltf_type_mat3:
            case cgltf_type::cgltf_type_mat4:
            case cgltf_type::cgltf_type_invalid:
            default:
            {
                abort();
                // TODO
            }

        }

        // Sort vertices
        std::set<glm::vec3::length_type> available_axis = { 0, 1, 2};
        std::set<glm::vec3::length_type> used_axis;

        const vec3 bounding_box_size0 = max_corner - min_corner;
        const auto axis0 = erhe::toolkit::max_axis_index(bounding_box_size0);
        available_axis.erase(axis0);
        used_axis.insert(axis0);
        vec3 bounding_box_size1 = bounding_box_size0;
        bounding_box_size1[axis0] = 0.0f;

        auto axis1 = erhe::toolkit::max_axis_index(bounding_box_size1);
        if (used_axis.count(axis1) > 0)
        {
            axis1 = *available_axis.begin();
        }
        available_axis.erase(axis1);
        used_axis.insert(axis1);

        vec3 bounding_box_size2 = bounding_box_size1;
        bounding_box_size2[axis1] = 0.0f;
        auto axis2 = erhe::toolkit::max_axis_index(bounding_box_size2);
        if (used_axis.count(axis2) > 0)
        {
            axis2 = * available_axis.begin();
        }
        available_axis.erase(axis2);
        used_axis.insert(axis2);

        log_parsers->trace("Bounding box   = {}", bounding_box_size0);
        log_parsers->trace("Primary   axis = {} {}", axis0, c_str(axis0));
        log_parsers->trace("Secondary axis = {} {}", axis1, c_str(axis1));
        log_parsers->trace("Tertiary  axis = {} {}", axis2, c_str(axis2));

        context.sorted_vertex_indices         .resize(vertex_count);
        context.erhe_point_id_from_gltf_index .resize(vertex_count);

        std::fill(
            context.sorted_vertex_indices.begin(),
            context.sorted_vertex_indices.end(),
            std::numeric_limits<cgltf_size>::max()
        );
        std::fill(
            context.erhe_point_id_from_gltf_index.begin(),
            context.erhe_point_id_from_gltf_index.end(),
            std::numeric_limits<Point_id>::max()
        );
        for (cgltf_size index : context.primitive_used_indices)
        {
            context.sorted_vertex_indices[index - context.primitive_min_index] = index;
        }

        std::sort(
            context.sorted_vertex_indices.begin(),
            context.sorted_vertex_indices.end(),
            [axis0, axis1, axis2, &context](
                const cgltf_size& lhs,
                const cgltf_size& rhs
            )
            {
                if (rhs == std::numeric_limits<cgltf_size>::max())
                {
                    return true;
                }
                const vec3 position_lhs = context.vertex_positions[lhs - context.primitive_min_index];
                const vec3 position_rhs = context.vertex_positions[rhs - context.primitive_min_index];
                if (position_lhs[axis0] != position_rhs[axis0])
                {
                    return position_lhs[axis0] < position_rhs[axis0];
                }
                if (position_lhs[axis1] != position_rhs[axis1])
                {
                    return position_lhs[axis1] < position_rhs[axis1];
                }
                return position_lhs[axis2] < position_rhs[axis2];
            }
        );

        // Create points for each unique vertex
        vec3 previous_position{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        erhe::geometry::Point_id point_id{0};
        std::size_t point_share_count{0};
        for (cgltf_size index : context.sorted_vertex_indices)
        {
            if (index == std::numeric_limits<cgltf_size>::max())
            {
                continue;
            }
            const vec3 position = context.vertex_positions[index - context.primitive_min_index];
            if (position != previous_position)
            {
                point_id = context.erhe_geometry->make_point();
                previous_position = position;
            }
            else
            {
                ++point_share_count;
            }
            context.erhe_point_id_from_gltf_index[index - context.primitive_min_index] = point_id;
        }
        log_parsers->trace(
            "point count = {}, point share count = {}",
            context.erhe_geometry->get_point_count(),
            point_share_count
        );
    }

    void parse_points()
    {
        log_parsers->error("parse_points() - not yet implemented");
    }
    void parse_lines()
    {
        log_parsers->error("parse_lines() - not yet implemented");
    }
    void parse_line_loop()
    {
        log_parsers->error("parse_line_loop() - not yet implemented");
    }
    void parse_line_strip()
    {
        log_parsers->error("parse_line_strip() - not yet implemented");
    }
    void parse_triangles(Primitive_context& context)
    {
        const cgltf_accessor* accessor = context.primitive->indices;

        const cgltf_size triangle_count = accessor->count / 3;

        log_parsers->trace(
            "index count = {}, unique vertex count = {}, triangle count = {}",
            accessor->count,
            context.primitive_used_indices.size(),
            triangle_count
        );

        context.erhe_geometry->reserve_polygons(triangle_count);
        context.gltf_index_from_corner_id.resize(3 * accessor->count);
        context.corner_id_start = context.erhe_geometry->m_next_corner_id;
        for (cgltf_size i = 0; i < accessor->count;)
        {
            const cgltf_size v0         = cgltf_accessor_read_index(accessor, i++);
            const cgltf_size v1         = cgltf_accessor_read_index(accessor, i++);
            const cgltf_size v2         = cgltf_accessor_read_index(accessor, i++);
            const Point_id   p0         = context.erhe_point_id_from_gltf_index.at(v0 - context.primitive_min_index);
            const Point_id   p1         = context.erhe_point_id_from_gltf_index.at(v1 - context.primitive_min_index);
            const Point_id   p2         = context.erhe_point_id_from_gltf_index.at(v2 - context.primitive_min_index);
            const Polygon_id polygon_id = context.erhe_geometry->make_polygon();
            const Corner_id  c0         = context.erhe_geometry->make_polygon_corner(polygon_id, p0);
            const Corner_id  c1         = context.erhe_geometry->make_polygon_corner(polygon_id, p1);
            const Corner_id  c2         = context.erhe_geometry->make_polygon_corner(polygon_id, p2);
            context.gltf_index_from_corner_id[c0 - context.corner_id_start] = v0;
            context.gltf_index_from_corner_id[c1 - context.corner_id_start] = v1;
            context.gltf_index_from_corner_id[c2 - context.corner_id_start] = v2;
            SPDLOG_LOGGER_TRACE(log_parsers, "vertex {} corner {} for polygon {}", v0, c0, polygon_id);
            SPDLOG_LOGGER_TRACE(log_parsers, "vertex {} corner {} for polygon {}", v1, c1, polygon_id);
            SPDLOG_LOGGER_TRACE(log_parsers, "vertex {} corner {} for polygon {}", v2, c2, polygon_id);
        }
        context.corner_id_end = context.erhe_geometry->m_next_corner_id;
    }
    void parse_triangle_strip()
    {
        log_parsers->error("parse_triangle_strip() - not yet implemented");
    }
    void parse_triangle_fan()
    {
        log_parsers->error("parse_triangle_fan() - not yet implemented");
    }
    void load_new_primitive_geometry(
        Primitive_context& context,
        Geometry_entry&    geometry_entry
    )
    {
        log_parsers->info("Loading new geometry");

        context.erhe_geometry = std::make_shared<erhe::geometry::Geometry>("");

        parse_primitive_used_indices(context);
        parse_primitive_make_points(context);

        switch (context.primitive->type)
        {
            case cgltf_primitive_type::cgltf_primitive_type_points:         parse_points        (); break;
            case cgltf_primitive_type::cgltf_primitive_type_lines:          parse_lines         (); break;
            case cgltf_primitive_type::cgltf_primitive_type_line_loop:      parse_line_loop     (); break;
            case cgltf_primitive_type::cgltf_primitive_type_line_strip:     parse_line_strip    (); break;
            case cgltf_primitive_type::cgltf_primitive_type_triangles:      parse_triangles     (context); break;
            case cgltf_primitive_type::cgltf_primitive_type_triangle_strip: parse_triangle_strip(); break;
            case cgltf_primitive_type::cgltf_primitive_type_triangle_fan:   parse_triangle_fan  (); break;
            default:
                break;
        }

        for (cgltf_size i = 0; i < context.primitive->attributes_count; ++i)
        {
            parse_primitive_attribute(context, &context.primitive->attributes[i]);
        }

        context.erhe_geometry->make_point_corners();
        context.erhe_geometry->build_edges();

        // TODO Debug issues reported here
        //context.erhe_geometry->sanity_check();
        //// context.erhe_geometry->compute_polygon_normals();
        //// context.erhe_geometry->compute_polygon_centroids();
        //// context.erhe_geometry->generate_polygon_texture_coordinates();
        //// context.erhe_geometry->compute_tangents(true, true);

        auto node_raytrace = std::make_shared<Node_raytrace>(context.erhe_geometry);
        geometry_entry.node_raytrace = node_raytrace;
        geometry_entry.raytrace_primitive = node_raytrace->raytrace_primitive();
        const auto normal_style = erhe::primitive::Normal_style::corner_normals;
        geometry_entry.gl_primitive_geometry = make_primitive(
            *context.erhe_geometry.get(),
            m_build_info,
            normal_style
        );
        m_geometries.push_back(geometry_entry);
    }
    auto get_primitive_geometry(
        Primitive_context& context,
        Geometry_entry&    geometry_entry
    )
    {
        geometry_entry.index_accessor = static_cast<cgltf_size>(context.primitive->indices - m_data->accessors);
        geometry_entry.attribute_accessors.clear();

        for (cgltf_size i = 0; i < context.primitive->attributes_count; ++i)
        {
            const cgltf_accessor* accessor = context.primitive->attributes[i].data;
            const cgltf_size attribute_accessor_index = accessor - m_data->accessors;
            geometry_entry.attribute_accessors.push_back(attribute_accessor_index);
        }

        for (const auto& entry : m_geometries)
        {
            if (entry.index_accessor != geometry_entry.index_accessor)
            {
                continue;
            }
            if (entry.attribute_accessors.size() != geometry_entry.attribute_accessors.size())
            {
                continue;
            }
            for (std::size_t i = 0, end = entry.attribute_accessors.size(); i < end; ++i)
            {
                if (entry.attribute_accessors[i] != geometry_entry.attribute_accessors[i])
                {
                    continue;
                }
            }
            // Found existing entry
            geometry_entry = entry;
            return;
        }

        load_new_primitive_geometry(context, geometry_entry);
    }
    void parse_primitive(
        const std::shared_ptr<erhe::scene::Node>& erhe_node,
        const std::shared_ptr<erhe::scene::Mesh>& erhe_mesh,
        cgltf_mesh*                               mesh,
        cgltf_primitive*                          primitive
    )
    {
        static_cast<void>(erhe_node);

        const cgltf_size primitive_index = primitive - mesh->primitives;

        auto name = (mesh->name != nullptr)
            ? fmt::format("{}[{}]", mesh->name, primitive_index)
            : fmt::format("primitive[{}]", primitive_index);

        log_parsers->trace("Primitive type: {}", c_str(primitive->type));

        Primitive_context context
        {
            .primitive = primitive,
        };
        Geometry_entry geometry_entry;
        get_primitive_geometry(context, geometry_entry);

        std::shared_ptr<erhe::primitive::Material> material;
        if (primitive->material != nullptr)
        {
            cgltf_size material_index = primitive->material - m_data->materials;
            material = m_materials.at(material_index);
        }

        const auto normal_style = erhe::primitive::Normal_style::corner_normals;
        erhe_mesh->mesh_data.primitives.push_back(
            erhe::primitive::Primitive{
                .material              = material,
                .gl_primitive_geometry = geometry_entry.gl_primitive_geometry,
                .rt_primitive_geometry = geometry_entry.raytrace_primitive->primitive_geometry,
                .rt_vertex_buffer      = geometry_entry.raytrace_primitive->vertex_buffer,
                .rt_index_buffer       = geometry_entry.raytrace_primitive->index_buffer,
                .source_geometry       = context.erhe_geometry,
                .normal_style          = normal_style
            }
        );

        //// erhe_node->attach(node_raytrace); TODO
    }
    void parse_mesh(cgltf_node* node)
    {
        cgltf_mesh* mesh = node->mesh;
        const cgltf_size node_index = node - m_data->nodes;
        const cgltf_size mesh_index = mesh - m_data->meshes;
        log_parsers->trace(
            "Mesh: node index = {}, mesh index = {}, name = {}",
            node_index, mesh_index, safe_str(mesh->name)
        );

        auto erhe_node = m_nodes.at(node_index);
        auto erhe_mesh = m_scene_root->content_library()->meshes.make(mesh->name);
        erhe_mesh->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui | Item_flags::shadow_cast);
        for (cgltf_size i = 0; i < mesh->primitives_count; ++i)
        {
            parse_primitive(erhe_node, erhe_mesh, mesh, &mesh->primitives[i]);
        }

        erhe_mesh->enable_flag_bits(
            erhe::scene::Item_flags::visible     |
            erhe::scene::Item_flags::content     |
            erhe::scene::Item_flags::shadow_cast |
            erhe::scene::Item_flags::id
        );

        erhe_mesh->mesh_data.layer_id = m_scene_root->layers().content()->id;
        erhe_node->attach(erhe_mesh);
    }
    void parse_node(
        cgltf_node*                        node,
        std::shared_ptr<erhe::scene::Node> erhe_parent
    )
    {
        const cgltf_size node_index = node - m_data->nodes;
        log_parsers->trace("Node: node index = {}, name = {}", node_index, safe_str(node->name));
        auto erhe_node = std::make_shared<erhe::scene::Node>(
            (node->name != nullptr) ? node->name : ""
        );
        erhe_node->set_parent(erhe_parent);
        erhe_node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        m_nodes[node_index] = erhe_node;
        parse_node_transform(node, erhe_node);

        if (node->camera != nullptr)
        {
            parse_camera(node);
        }

        if (node->light != nullptr)
        {
            parse_light(node);
        }

        if (node->mesh != nullptr)
        {
            parse_mesh(node);
        }

        for (cgltf_size i = 0; i < node->children_count; ++i)
        {
            parse_node(node->children[i], erhe_node);
        }
    }
    void fix_node_hierarchy(cgltf_node* node)
    {
        const cgltf_size node_index = node - m_data->nodes;
        auto& erhe_parent_node = m_nodes.at(node_index);
        for (cgltf_size i = 0; i < node->children_count; ++i)
        {
            cgltf_node* child_node       = node->children[i];
            cgltf_size  child_node_index = child_node - m_data->nodes;
            auto& erhe_child_node        = m_nodes.at(child_node_index);
            erhe_child_node->set_parent(erhe_parent_node);
        }

        for (cgltf_size i = 0; i < node->children_count; ++i)
        {
            fix_node_hierarchy(node->children[i]);
        }
    }
    void parse_scene(cgltf_scene* scene)
    {
        const cgltf_size scene_index = scene - m_data->scenes;
        log_parsers->trace("Scene: id = {}, name = {}", scene_index, safe_str(scene->name));

        m_nodes.resize(m_data->nodes_count);
        std::fill(
            m_nodes.begin(),
            m_nodes.end(),
            std::shared_ptr<erhe::scene::Node>{}
        );
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        {
            parse_node(
                scene->nodes[i],
                m_scene_root->scene().get_root_node()
            );
        }

        // Setup node hierarchy
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        {
            fix_node_hierarchy(scene->nodes[i]);
        }
        m_nodes.clear();
    }

    std::shared_ptr<Materials>       m_materials_;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Content_library> m_content_library;
    erhe::primitive::Build_info&     m_build_info;
    std::vector<Geometry_entry>      m_geometries;

    cgltf_data*                                             m_data{nullptr};

    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;

    // Scene context
    std::vector<std::shared_ptr<erhe::scene::Node>>   m_nodes;
};

void parse_gltf(
    const std::shared_ptr<Scene_root>& scene_root,
    erhe::primitive::Build_info&       build_info,
    const std::filesystem::path&       path
)
{
    Gltf_parser parser{scene_root, build_info, path};
    parser.parse_and_build();
}

} // namespace editor
