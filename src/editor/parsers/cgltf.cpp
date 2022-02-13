#include "parsers/gltf.hpp"
#include "log.hpp"
#include "scene/scene_root.hpp"

#include "erhe/scene/camera.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"

#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>

#include "cgltf.h"

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <fstream>
#include <limits>
#include <string>

namespace editor {

namespace {

auto c_str(const cgltf_result value) -> const char*
{
    switch (value)
    {
	    case cgltf_result::cgltf_result_success:         return "sucess";
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

auto c_str(const cgltf_component_type value) -> const char*
{
    switch (value)
    {
        case cgltf_component_type::cgltf_component_type_invalid : return "invalid";
        case cgltf_component_type::cgltf_component_type_r_8     : return "r_8";
        case cgltf_component_type::cgltf_component_type_r_8u    : return "r_8u";
        case cgltf_component_type::cgltf_component_type_r_16    : return "r_16";
        case cgltf_component_type::cgltf_component_type_r_16u   : return "r_16u";
        case cgltf_component_type::cgltf_component_type_r_32u   : return "r_32u";
        case cgltf_component_type::cgltf_component_type_r_32f   : return "r_32f";
        default:                                                  return "?";
    }
};

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

auto c_str(const cgltf_alpha_mode value) -> const char*
{
    switch (value)
    {
        case cgltf_alpha_mode::cgltf_alpha_mode_opaque: return "opaque";
        case cgltf_alpha_mode::cgltf_alpha_mode_mask:   return "blend";
        case cgltf_alpha_mode::cgltf_alpha_mode_blend:  return "mask";
        default:                                        return "?";
    }
};

auto c_str(const cgltf_animation_path_type value) -> const char*
{
    switch (value)
    {
        case cgltf_animation_path_type::cgltf_animation_path_type_invalid:     return "invalid";
        case cgltf_animation_path_type::cgltf_animation_path_type_translation: return "translation";
        case cgltf_animation_path_type::cgltf_animation_path_type_rotation:    return "rotation";
        case cgltf_animation_path_type::cgltf_animation_path_type_scale:       return "scale";
        case cgltf_animation_path_type::cgltf_animation_path_type_weights:     return "weights";
        default:                 return "?";
    }
};

auto c_str(const cgltf_interpolation_type value) -> const char*
{
    switch (value)
    {
        case cgltf_interpolation_type::cgltf_interpolation_type_linear:       return "linear";
        case cgltf_interpolation_type::cgltf_interpolation_type_step:         return "step";
        case cgltf_interpolation_type::cgltf_interpolation_type_cubic_spline: return "cubicspline";
        default:                                                              return "?";
    }
};

auto c_str(const cgltf_camera_type value) -> const char*
{
    switch (value)
    {
        case cgltf_camera_type::cgltf_camera_type_invalid:      return "invalid";
        case cgltf_camera_type::cgltf_camera_type_perspective:  return "perspective";
        case cgltf_camera_type::cgltf_camera_type_orthographic: return "orthographic";
        default:                                                return "?";
    }
};

}

using namespace glm;
using namespace erhe::geometry;


auto parse_gltf(
    const std::shared_ptr<Scene_root>& scene_root,
    const fs::path&                    path
) -> bool
{
    ERHE_PROFILE_FUNCTION

    //try
    //{
    //    fs::path canonical_path = fs::canonical(relative_path);
    //    fs::path path           = fs::current_path() / canonical_path;
    //    log_parsers.trace("path = {}\n", path.generic_string());
    //}
    //catch (...)
    //{
    //    log_parsers.trace("invalid path = {}\n", relative_path.string());
    //    return {};
    //}

    cgltf_options parse_options
    {
	    .type             = cgltf_file_type_invalid, // auto
	    .json_token_count = 0, // 0 == auto
	    .memory = {
            .alloc     = nullptr,
            .free      = nullptr,
            .user_data = nullptr
        },
        .file = {
            .read      = nullptr,
            .release   = nullptr,
            .user_data = nullptr
        }
    };

    cgltf_data* gltf_data{nullptr};
    cgltf_result result = cgltf_parse_file(
        &parse_options,
        path.string().c_str(),
        &gltf_data
    );

    if (result != cgltf_result::cgltf_result_success)
    {
        log_parsers.error("glTF parse error: {}\n", c_str(result));
        return {};
    }

    if (gltf_data->asset.version != nullptr)
    {
        log_parsers.info("Asset Version:    {}\n", gltf_data->asset.version);
    }
    if (gltf_data->asset.min_version != nullptr)
    {
        log_parsers.info("Asset MinVersion: {}\n", gltf_data->asset.min_version);
    }
    if (gltf_data->asset.generator != nullptr)
    {
        log_parsers.info("Asset Generator:  {}\n", gltf_data->asset.generator);
    }
    if (gltf_data->asset.copyright != nullptr)
    {
        log_parsers.info("Asset Copyright:  {}\n", gltf_data->asset.copyright);
    }

    // TODO asset extras and extensions

    // Scene Info
    log_parsers.info("Scene Count: {}\n", gltf_data->scenes_count);

    // Entity Info
    log_parsers.info("Node Count:       {}\n", gltf_data->nodes_count);
    log_parsers.info("Camera Count:     {}\n", gltf_data->cameras_count);
    log_parsers.info("Light Count:      {}\n", gltf_data->lights_count);
    log_parsers.info("Material Count:   {}\n", gltf_data->materials_count);
    log_parsers.info("Mesh Count:       {}\n", gltf_data->meshes_count);
    log_parsers.info("Skin Count:       {}\n", gltf_data->skins_count);
    log_parsers.info("Image Count:      {}\n", gltf_data->images_count);
    log_parsers.info("Texture Count:    {}\n", gltf_data->textures_count);
    log_parsers.info("Sampler Count:    {}\n", gltf_data->samplers_count);
    log_parsers.info("Buffer Count:     {}\n", gltf_data->buffers_count);
    log_parsers.info("BufferView Count: {}\n", gltf_data->buffer_views_count);
    log_parsers.info("Accessor Count:   {}\n", gltf_data->accessors_count);
    log_parsers.info("Animation Count:  {}\n", gltf_data->animations_count);

    for (cgltf_size i = 0; i < gltf_data->extensions_used_count; ++i)
    {
        log_parsers.info("Extension Used: {}\n", gltf_data->extensions_used[i]);
    }

    for (cgltf_size i = 0; i < gltf_data->extensions_required_count; ++i)
    {
        log_parsers.info("Extension Required: {}\n", gltf_data->extensions_required[i]);
    }

    log_parsers.info("scene count = {}", gltf_data->scenes_count);

    auto& root_scene = scene_root->scene();
    for (cgltf_size scene_index = 0; scene_index < gltf_data->scenes_count; ++scene_index)
    {
        const cgltf_scene* scene = &gltf_data->scenes[scene_index];
        for (cgltf_size node_index = 0; node_index < scene->nodes_count; ++node_index)
        {
            const cgltf_node* node = scene->nodes[node_index];
            log_parsers.info("Node {}: - {}\n", node_index, node->name);

            if (node->parent != nullptr)
            {
                const cgltf_node* parent    = node->parent;
                const intptr_t    parent_id = parent - gltf_data->nodes;
                log_parsers.info("Node parent: id = {}, name = {}\n", parent_id, parent->name);
            }

            log_parsers.info("Node children count: - {}\n", node->children_count);

            if (node->camera != nullptr)
            {
                const cgltf_camera* camera    = node->camera;
                const intptr_t      camera_id = camera - gltf_data->cameras;
                log_parsers.info("Camera: {} - {}\n", camera_id, camera->name);

                auto new_camera = std::make_shared<erhe::scene::Camera>(camera->name);
                switch (camera->type)
                {
                    case cgltf_camera_type::cgltf_camera_type_perspective:
                    {
                        const cgltf_camera_perspective& perspective = camera->data.perspective;
                        log_parsers.info("Camera.has_aspect_ration: {}\n", perspective.has_aspect_ratio);
                        log_parsers.info("Camera.aspect_ratio:      {}\n", perspective.aspect_ratio);
                        log_parsers.info("Camera.yfov:              {}\n", perspective.yfov);
                        log_parsers.info("Camera.has_zfar:          {}\n", perspective.has_zfar);
                        log_parsers.info("Camera.zfar:              {}\n", perspective.zfar);
                        log_parsers.info("Camera.znear:             {}\n", perspective.znear);
                        new_camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
                        new_camera->projection()->fov_y           = perspective.yfov;
                        new_camera->projection()->z_far           = (perspective.has_zfar != 0)
                            ? perspective.zfar
                            : 0.0f;
                        break;
                    }

                    case cgltf_camera_type::cgltf_camera_type_orthographic:
                    {
                        const cgltf_camera_orthographic& orthographic = camera->data.orthographic;
                        log_parsers.info("Camera.xmag:              {}\n", orthographic.xmag);
                        log_parsers.info("Camera.ymag:              {}\n", orthographic.ymag);
                        log_parsers.info("Camera.zfar:              {}\n", orthographic.zfar);
                        log_parsers.info("Camera.znear:             {}\n", orthographic.znear);
                        new_camera->projection()->projection_type = erhe::scene::Projection::Type::orthogonal;
                        new_camera->projection()->ortho_width     = orthographic.xmag;
                        new_camera->projection()->ortho_height    = orthographic.ymag;
                        new_camera->projection()->z_far           = orthographic.zfar;
                        new_camera->projection()->z_near          = orthographic.znear;
                        break;
                    }

                    default:
                    {
                        log_parsers.warn("Camera.Projection: unknown projection type {}\n");
                        break;
                    }
                }

                scene_root->scene().cameras.push_back(new_camera);

                root_scene.nodes.emplace_back(new_camera);
                root_scene.nodes_sorted = false;
            }

            if (node->mesh != nullptr)
            {
                const cgltf_mesh* mesh    = node->mesh;
                const intptr_t    mesh_id = mesh - gltf_data->meshes;
                log_parsers.info("Mesh: {} - {}\n", mesh_id, mesh->name);

                for (cgltf_size primitive_index = 0; primitive_index < mesh->primitives_count; ++primitive_index)
                {
                    const cgltf_primitive* primitive = &mesh->primitives[primitive_index];
                    log_parsers.error("Primitive: attribute count = {}\n", primitive->attributes_count);
                    for (cgltf_size attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index)
                    {
                        const cgltf_attribute* attribute = &primitive->attributes[attribute_index];
                        const cgltf_accessor*  accessor  = attribute->data;
                        if (accessor == nullptr)
                        {
                            log_parsers.error("Primitive attribute {} has null accessor", attribute_index);
                            continue;
                        }
                        const cgltf_buffer_view* buffer_view = accessor->buffer_view;
                        if (buffer_view == nullptr)
                        {
                            log_parsers.error("Primitive attribute {} has null buffer view", attribute_index);
                            continue;
                        }

                        const intptr_t accessor_id    = accessor    - gltf_data->accessors;
                        const intptr_t buffer_view_id = buffer_view - gltf_data->buffer_views;

                        log_parsers.info(
                            "Primitive attribute {}: Index = {}, name = {}, type = {}\n",
                            attribute_index,
                            attribute->index,
                            attribute->name,
                            c_str(attribute->type)
                        );

                        log_parsers.info(
                            "    Accessor: id = {}, normalized = {}, type = {}, offset = {}, count = {}, stride = {}\n",
                            accessor_id,
                            accessor->normalized,
                            c_str(accessor->type),
                            accessor->offset,
                            accessor->count,
                            accessor->stride
                        );

                        log_parsers.info(
                            "    Buffer view: id = {}, offset = {}, size = {}, stride = {}, type = {}\n",
                            buffer_view_id,
                            buffer_view->offset,
                            buffer_view->size,
                            buffer_view->stride,
                            c_str(buffer_view->type)
                        );
                    }
                    //log_parsers.error("Primitive: mappings count  = {}\n", primitive->mappings_count);
                    //for (cgltf_size mapping_index = 0; mapping_index < primitive->mappings_count; ++mapping_index)
                    //{
                    //}

                    if (primitive->material != nullptr)
                    {
                        const cgltf_material* material    = primitive->material;
                        const intptr_t        material_id = material - gltf_data->materials;

                        log_parsers.info(
                            "Primitive material: id = {}, name = {}\n",
                            material_id,
                            material->name
                        );

                        if (material->has_pbr_metallic_roughness)
                        {
                            const cgltf_pbr_metallic_roughness& pbr_metallic_roughness = material->pbr_metallic_roughness;
                            log_parsers.info(
                                "Material PBR metallic roughness base color factor = {}, {}, {}, {}\n",
                                pbr_metallic_roughness.base_color_factor[0],
                                pbr_metallic_roughness.base_color_factor[1],
                                pbr_metallic_roughness.base_color_factor[2],
                                pbr_metallic_roughness.base_color_factor[3]
                            );
                            log_parsers.info(
                                "Material PBR metallic roughness metallic factor = {}\n",
                                pbr_metallic_roughness.metallic_factor
                            );
                            log_parsers.info(
                                "Material PBR metallic roughness roughness factor = {}\n",
                                pbr_metallic_roughness.roughness_factor
                            );
                        }
                        if (material->has_pbr_specular_glossiness)
                        {
                            const cgltf_pbr_specular_glossiness& pbr_specular_glossiness = material->pbr_specular_glossiness;
                            log_parsers.info(
                                "Material PBR specular glossiness diffuse factor = {}, {}, {}, {}\n",
                                pbr_specular_glossiness.diffuse_factor[0],
                                pbr_specular_glossiness.diffuse_factor[1],
                                pbr_specular_glossiness.diffuse_factor[2],
                                pbr_specular_glossiness.diffuse_factor[3]
                            );
                            log_parsers.info(
                                "Material PBR specular glossiness specular factor = {}, {}, {}\n",
                                pbr_specular_glossiness.specular_factor[0],
                                pbr_specular_glossiness.specular_factor[1],
                                pbr_specular_glossiness.specular_factor[2]
                            );
                            log_parsers.info(
                                "Material PBR specular glossiness glossiness factor = {}\n",
                                pbr_specular_glossiness.glossiness_factor
                            );
                        }
                    }

                    {
                        const cgltf_accessor* accessor = primitive->indices;
                        assert(accessor != nullptr);

                        const cgltf_buffer_view* buffer_view = accessor->buffer_view;
                        assert(buffer_view != nullptr);

                        const intptr_t accessor_id    = accessor - gltf_data->accessors;
                        const intptr_t buffer_view_id = buffer_view - gltf_data->buffer_views;

                        log_parsers.info("Primitive indices:\n");

                        log_parsers.info(
                            "    Accessor: id = {}, normalized = {}, type = {}, offset = {}, count = {}, stride = {}\n",
                            accessor_id,
                            accessor->normalized,
                            c_str(accessor->type),
                            accessor->offset,
                            accessor->count,
                            accessor->stride
                        );

                        log_parsers.info(
                            "    Buffer view: id = {}, offset = {}, size = {}, stride = {}, type = {}\n",
                            buffer_view_id,
                            buffer_view->offset,
                            buffer_view->size,
                            buffer_view->stride,
                            c_str(buffer_view->type)
                        );
                    }
                }
            }
        }
    }

    return {};
}

} // namespace editor
