// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "gltf.hpp"
#include "gltf_log.hpp"
#include "image_transfer.hpp"

#include "erhe_file/file.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/png_loader.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/skin.hpp"

#include "erhe_file/file.hpp"
#include "erhe_verify/verify.hpp"

extern "C" {
    #include "cgltf.h"
}

#include <glm/glm.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>
#include <string>

namespace erhe::gltf {

namespace {

[[nodiscard]] auto is_float(const cgltf_component_type component_type)
{
    switch (component_type) {
        case cgltf_component_type_invalid:  return false;
        case cgltf_component_type_r_8:      return false;
        case cgltf_component_type_r_8u:     return false;
        case cgltf_component_type_r_16:     return false;
        case cgltf_component_type_r_16u:    return false;
        case cgltf_component_type_r_32u:    return false;
        case cgltf_component_type_r_32f:    return true;
        case cgltf_component_type_max_enum: return false;
        default: return false;
    }
}

[[nodiscard]] const char* c_str(const cgltf_result value)
{
    switch (value) {
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

[[nodiscard]] auto c_str(const cgltf_attribute_type value) -> const char*
{
    switch (value) {
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

[[nodiscard]] auto c_str(const cgltf_buffer_view_type value) -> const char*
{
    switch (value) {
        case cgltf_buffer_view_type::cgltf_buffer_view_type_invalid:  return "invalid";
        case cgltf_buffer_view_type::cgltf_buffer_view_type_vertices: return "vertices";
        case cgltf_buffer_view_type::cgltf_buffer_view_type_indices:  return "indices";
        default:                                                      return "?";
    }
};

[[nodiscard]] auto c_str(const cgltf_component_type value) -> const char*
{
    switch (value) {
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

[[nodiscard]] auto c_str(const cgltf_type value) -> const char*
{
    switch (value) {
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

[[nodiscard]] auto c_str(const cgltf_primitive_type value) -> const char*
{
    switch (value) {
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

[[nodiscard]] auto to_erhe(const cgltf_primitive_type value) -> gl::Primitive_type
{
    switch (value) {
        case cgltf_primitive_type::cgltf_primitive_type_points:         return gl::Primitive_type::points;
        case cgltf_primitive_type::cgltf_primitive_type_lines:          return gl::Primitive_type::lines;
        case cgltf_primitive_type::cgltf_primitive_type_line_loop:      return gl::Primitive_type::line_loop;
        case cgltf_primitive_type::cgltf_primitive_type_line_strip:     return gl::Primitive_type::line_strip;
        case cgltf_primitive_type::cgltf_primitive_type_triangles:      return gl::Primitive_type::triangles;
        case cgltf_primitive_type::cgltf_primitive_type_triangle_strip: return gl::Primitive_type::triangle_strip;
        case cgltf_primitive_type::cgltf_primitive_type_triangle_fan:   return gl::Primitive_type::triangle_fan;
        default:                                                        return gl::Primitive_type::points;
    }
};

// [[nodiscard]] auto c_str(const cgltf_alpha_mode value) -> const char*
// {
//     switch (value) {
//         case cgltf_alpha_mode::cgltf_alpha_mode_opaque: return "opaque";
//         case cgltf_alpha_mode::cgltf_alpha_mode_mask:   return "blend";
//         case cgltf_alpha_mode::cgltf_alpha_mode_blend:  return "mask";
//         default:                                        return "?";
//     }
// };

[[nodiscard]] auto c_str(const cgltf_animation_path_type value) -> const char*
{
    switch (value) {
        case cgltf_animation_path_type::cgltf_animation_path_type_invalid:     return "invalid";
        case cgltf_animation_path_type::cgltf_animation_path_type_translation: return "translation";
        case cgltf_animation_path_type::cgltf_animation_path_type_rotation:    return "rotation";
        case cgltf_animation_path_type::cgltf_animation_path_type_scale:       return "scale";
        case cgltf_animation_path_type::cgltf_animation_path_type_weights:     return "weights";
        default:                                                               return "?";
    }
};

[[nodiscard]] auto to_erhe(const cgltf_animation_path_type value) -> erhe::scene::Animation_path
{
    switch (value) {
        case cgltf_animation_path_type::cgltf_animation_path_type_invalid:     return erhe::scene::Animation_path::INVALID;
        case cgltf_animation_path_type::cgltf_animation_path_type_translation: return erhe::scene::Animation_path::TRANSLATION;
        case cgltf_animation_path_type::cgltf_animation_path_type_rotation:    return erhe::scene::Animation_path::ROTATION;
        case cgltf_animation_path_type::cgltf_animation_path_type_scale:       return erhe::scene::Animation_path::SCALE;
        case cgltf_animation_path_type::cgltf_animation_path_type_weights:     return erhe::scene::Animation_path::WEIGHTS;
        default:                                                               return erhe::scene::Animation_path::INVALID;
    }
};

[[nodiscard]] auto c_str(const cgltf_interpolation_type value) -> const char*
{
    switch (value) {
        case cgltf_interpolation_type::cgltf_interpolation_type_linear:       return "linear";
        case cgltf_interpolation_type::cgltf_interpolation_type_step:         return "step";
        case cgltf_interpolation_type::cgltf_interpolation_type_cubic_spline: return "cubicspline";
        default:                                                              return "?";
    }
};

[[nodiscard]] auto to_erhe(const cgltf_interpolation_type value) -> erhe::scene::Animation_interpolation_mode
{
    switch (value) {
        case cgltf_interpolation_type::cgltf_interpolation_type_linear:       return erhe::scene::Animation_interpolation_mode::LINEAR;
        case cgltf_interpolation_type::cgltf_interpolation_type_step:         return erhe::scene::Animation_interpolation_mode::STEP;
        case cgltf_interpolation_type::cgltf_interpolation_type_cubic_spline: return erhe::scene::Animation_interpolation_mode::CUBICSPLINE;
        default:                                                              return erhe::scene::Animation_interpolation_mode::STEP;
    }
};

// [[nodiscard]] auto c_str(const cgltf_camera_type value) -> const char*
// {
//     switch (value) {
//         case cgltf_camera_type::cgltf_camera_type_invalid:      return "invalid";
//         case cgltf_camera_type::cgltf_camera_type_perspective:  return "perspective";
//         case cgltf_camera_type::cgltf_camera_type_orthographic: return "orthographic";
//         default:                                                return "?";
//     }
// };

[[nodiscard]] auto safe_resource_name(
    const char* const text,
    const char* const resource_type,
    const cgltf_size  resource_index
) -> std::string
{
    if ((text == nullptr) || strlen(text) == 0) {
        return fmt::format("{}-{}", resource_type, resource_index);
    }
    return std::string(text);
}

} // anonymous namespace

using namespace glm;
using namespace erhe::geometry;

namespace {
[[nodiscard]] auto to_erhe(const cgltf_light_type gltf_light_type) -> erhe::scene::Light_type
{
    switch (gltf_light_type) {
        case cgltf_light_type::cgltf_light_type_invalid:     return erhe::scene::Light_type::directional;
        case cgltf_light_type::cgltf_light_type_directional: return erhe::scene::Light_type::directional;
        case cgltf_light_type::cgltf_light_type_point:       return erhe::scene::Light_type::point;
        case cgltf_light_type::cgltf_light_type_spot:        return erhe::scene::Light_type::spot;
        default:                                             return erhe::scene::Light_type::directional;
    }
}

[[nodiscard]] auto is_per_point(const cgltf_attribute_type gltf_attribute_type) -> bool
{
    switch (gltf_attribute_type) {
        case cgltf_attribute_type::cgltf_attribute_type_invalid:  return false;
        case cgltf_attribute_type::cgltf_attribute_type_position: return true;
        case cgltf_attribute_type::cgltf_attribute_type_normal:   return false;
        case cgltf_attribute_type::cgltf_attribute_type_tangent:  return false;
        case cgltf_attribute_type::cgltf_attribute_type_texcoord: return false;
        case cgltf_attribute_type::cgltf_attribute_type_color:    return false;
        case cgltf_attribute_type::cgltf_attribute_type_joints:   return true;
        case cgltf_attribute_type::cgltf_attribute_type_weights:  return true;
        default:                                                  return false;
    }
}

[[nodiscard]] auto is_per_corner(const cgltf_attribute_type gltf_attribute_type) -> bool
{
    switch (gltf_attribute_type) {
        case cgltf_attribute_type::cgltf_attribute_type_invalid:  return false;
        case cgltf_attribute_type::cgltf_attribute_type_position: return false;
        case cgltf_attribute_type::cgltf_attribute_type_normal:   return true;
        case cgltf_attribute_type::cgltf_attribute_type_tangent:  return true;
        case cgltf_attribute_type::cgltf_attribute_type_texcoord: return true;
        case cgltf_attribute_type::cgltf_attribute_type_color:    return true;
        case cgltf_attribute_type::cgltf_attribute_type_joints:   return false;
        case cgltf_attribute_type::cgltf_attribute_type_weights:  return false;
        default:                                                  return false;
    }
}

[[nodiscard]] auto to_erhe(const cgltf_attribute_type gltf_attribute_type) -> std::optional<erhe::graphics::Vertex_attribute::Usage_type>
{
    using Usage_type = erhe::graphics::Vertex_attribute::Usage_type;
    switch (gltf_attribute_type) {
        case cgltf_attribute_type::cgltf_attribute_type_invalid:  return {};
        case cgltf_attribute_type::cgltf_attribute_type_position: return Usage_type::position;
        case cgltf_attribute_type::cgltf_attribute_type_normal:   return Usage_type::normal;
        case cgltf_attribute_type::cgltf_attribute_type_tangent:  return Usage_type::tangent;
        case cgltf_attribute_type::cgltf_attribute_type_texcoord: return Usage_type::tex_coord;
        case cgltf_attribute_type::cgltf_attribute_type_color:    return Usage_type::color;
        case cgltf_attribute_type::cgltf_attribute_type_joints:   return Usage_type::joint_indices;
        case cgltf_attribute_type::cgltf_attribute_type_weights:  return Usage_type::joint_weights;
        default:                                                  return {};
    }
}

[[nodiscard]] auto get_component_byte_count(const cgltf_component_type component_type) -> cgltf_size
{
    switch (component_type) {
        case cgltf_component_type::cgltf_component_type_invalid : return 0;
        case cgltf_component_type::cgltf_component_type_r_8     : return 1;
        case cgltf_component_type::cgltf_component_type_r_8u    : return 1;
        case cgltf_component_type::cgltf_component_type_r_16    : return 2;
        case cgltf_component_type::cgltf_component_type_r_16u   : return 2;
        case cgltf_component_type::cgltf_component_type_r_32u   : return 4;
        case cgltf_component_type::cgltf_component_type_r_32f   : return 4;
        case cgltf_component_type::cgltf_component_type_max_enum: return 0;
        default                                                 : return 0;
    }
}
}

using Item_flags = erhe::Item_flags;

[[nodiscard]] auto to_gl(erhe::graphics::Image_format format) -> gl::Internal_format
{
    switch (format) {
        //using enum erhe::graphics::Image_format;
        case erhe::graphics::Image_format::srgb8:        return gl::Internal_format::srgb8;
        case erhe::graphics::Image_format::srgb8_alpha8: return gl::Internal_format::srgb8_alpha8;
        default: {
            ERHE_FATAL("Bad image format %04x", static_cast<unsigned int>(format));
        }
    }
    // std::unreachable() return gl::Internal_format::rgba8;
}

namespace {

cgltf_result cgltf_custom_file_read(
    const cgltf_memory_options* ,
    const cgltf_file_options*   ,
    const char*                 path,
    cgltf_size*                 size,
    void**                      data
)
{
    std::string str_path{path};
    std::filesystem::path fs_path = std::filesystem::u8path(str_path);
    std::optional<std::string> file_contents_opt = erhe::file::read("cgltf file read", fs_path);
    if (!file_contents_opt.has_value())  {
        if (size != nullptr) {
            *size = 0;
        }
        if (data != nullptr) {
            *data = nullptr;
        }
        return cgltf_result_file_not_found;
    }
    const std::string& file_contents = file_contents_opt.value();
    cgltf_size file_size = file_contents.size();
    char* file_data = (char*)malloc(file_size);
    if (file_data == nullptr) {
        return cgltf_result_out_of_memory;
    }
    memcpy(file_data, file_contents.data(), file_size);

    if (size) {
        *size = file_size;
    }
    if (data) {
        *data = file_data;
    }

    return cgltf_result_success;
}

void cgltf_custom_file_release(
    const cgltf_memory_options* ,
    const cgltf_file_options*   ,
    void*                       data
)
{
    free(data);
}

}

class Gltf_parser
{
public:
    const cgltf_size null_index{std::numeric_limits<cgltf_size>::max()};

    Gltf_parser(
        Gltf_data&                                gltf_data,
        erhe::graphics::Instance&                 graphics_instance,
        Image_transfer&                           image_transfer,
        const std::shared_ptr<erhe::scene::Node>& root_node,
        erhe::scene::Layer_id                     mesh_layer_id,
        std::filesystem::path                     path
    )
        : m_data_out         {gltf_data}
        , m_graphics_instance{graphics_instance}
        , m_image_transfer   {image_transfer}
        , m_root_node        {root_node}
        , m_mesh_layer_id    {mesh_layer_id}
        , m_path             {path}
    {
        if (!open(path)) {
            return;
        }

        trace_info();
    }

    void parse_and_build()
    {
        if (m_data == nullptr) {
            log_gltf->error("No data loaded to parse glTF");
            return;
        }

        m_data_out.images.resize(m_data->images_count);
        for (cgltf_size i = 0; i < m_data->images_count; ++i) {
            parse_image(i);
        }

        m_data_out.samplers.resize(m_data->samplers_count);
        for (cgltf_size i = 0; i < m_data->samplers_count; ++i) {
            parse_sampler(i);
        }

        m_data_out.materials.resize(m_data->materials_count);
        for (cgltf_size i = 0; i < m_data->materials_count; ++i) {
            parse_material(i);
        }

        m_data_out.cameras.resize(m_data->cameras_count);
        for (cgltf_size i = 0; i < m_data->cameras_count; ++i) {
            parse_camera(i);
        }

        m_data_out.lights.resize(m_data->lights_count);
        for (cgltf_size i = 0; i < m_data->lights_count; ++i) {
            parse_light(i);
        }

        m_data_out.meshes.resize(m_data->meshes_count);
        for (cgltf_size i = 0; i < m_data->meshes_count; ++i) {
            parse_mesh(i);
        }

        m_data_out.nodes.resize(m_data->nodes_count);
        if (m_data->scene != nullptr) {
            for (cgltf_size i = 0; i < m_data->scene->nodes_count; ++i) {
                parse_node(m_data->scene->nodes[i], m_root_node);
            }
        //} else {
        //    for (cgltf_size i = 0; i < m_data->scenes_count; ++i) {
        //        const cgltf_scene* scene = &m_data->scenes[i];
        //        for (cgltf_size j = 0; i < scene->nodes_count; ++i) {
        //            parse_node(scene->nodes[j], m_root_node);
        //        }
        //        break; // only first scene for now
        //    }
        }

        m_data_out.skins.resize(m_data->skins_count);
        for (cgltf_size i = 0; i < m_data->skins_count; ++i) {
            parse_skin(i);
        }

        for (cgltf_size i = 0; i < m_data->nodes_count; ++i) {
            fix_pointers(i);
        }

        m_data_out.animations.reserve(m_data->animations_count);
        for (cgltf_size i = 0; i < m_data->animations_count; ++i) {
            parse_animation(i);
        }
    }

private:
    auto open(const std::filesystem::path& path) -> bool
    {
        std::optional<std::string> file_contents_opt = erhe::file::read("GLTF file", path);
        if (!file_contents_opt.has_value()) {
            return false;
        }
        const cgltf_options parse_options{
            .type             = cgltf_file_type_invalid, // auto
            .json_token_count = 0, // 0 == auto
            .memory = {
                .alloc_func   = nullptr,
                .free_func    = nullptr,
                .user_data    = nullptr
            },
            .file = {
                .read         = cgltf_custom_file_read,
                .release      = cgltf_custom_file_release,
                .user_data    = nullptr
            }
        };

        const std::string& file_contents = file_contents_opt.value();
        const cgltf_result parse_result = cgltf_parse(
            &parse_options,
            file_contents.data(),
            file_contents.size(),
            &m_data
        );

        if (parse_result != cgltf_result::cgltf_result_success) {
            log_gltf->error("glTF parse error: {}", c_str(parse_result));
            return false;
        }

        const std::string path_string = erhe::file::to_string(path);
        const cgltf_result load_buffers_result = cgltf_load_buffers(&parse_options, m_data, path_string.c_str());
        if (load_buffers_result != cgltf_result::cgltf_result_success) {
            log_gltf->error("glTF load buffers error: {}", c_str(load_buffers_result));
            return false;
        }

        return true;
    }
    void trace_info() const
    {
        if (m_data->asset.version != nullptr) {
            log_gltf->trace("Asset Version:    {}", m_data->asset.version);
        }
        if (m_data->asset.min_version != nullptr) {
            log_gltf->trace("Asset MinVersion: {}", m_data->asset.min_version);
        }
        if (m_data->asset.generator != nullptr) {
            log_gltf->trace("Asset Generator:  {}", m_data->asset.generator);
        }
        if (m_data->asset.copyright != nullptr) {
            log_gltf->trace("Asset Copyright:  {}", m_data->asset.copyright);
        }
        log_gltf->trace("Node Count:       {}", m_data->nodes_count);
        log_gltf->trace("Camera Count:     {}", m_data->cameras_count);
        log_gltf->trace("Light Count:      {}", m_data->lights_count);
        log_gltf->trace("Material Count:   {}", m_data->materials_count);
        log_gltf->trace("Mesh Count:       {}", m_data->meshes_count);
        log_gltf->trace("Skin Count:       {}", m_data->skins_count);
        log_gltf->trace("Image Count:      {}", m_data->images_count);
        log_gltf->trace("Texture Count:    {}", m_data->textures_count);
        log_gltf->trace("Sampler Count:    {}", m_data->samplers_count);
        log_gltf->trace("Buffer Count:     {}", m_data->buffers_count);
        log_gltf->trace("BufferView Count: {}", m_data->buffer_views_count);
        log_gltf->trace("Accessor Count:   {}", m_data->accessors_count);
        log_gltf->trace("Animation Count:  {}", m_data->animations_count);
        log_gltf->trace("Scene Count:      {}", m_data->scenes_count);

        for (cgltf_size i = 0; i < m_data->extensions_used_count; ++i) {
            log_gltf->trace("Extension Used: {}", m_data->extensions_used[i]);
        }

        for (cgltf_size i = 0; i < m_data->extensions_required_count; ++i) {
            log_gltf->trace("Extension Required: {}", m_data->extensions_required[i]);
        }
    }
    void parse_animation(const cgltf_size animation_index)
    {
        cgltf_animation* animation = &m_data->animations[animation_index];
        const std::string animation_name = safe_resource_name(animation->name, "animation", animation_index);
        log_gltf->trace(
            "Animation: id = {}, name = {}",
            animation_index,
            animation_name
        );

        auto erhe_animation = std::make_shared<erhe::scene::Animation>(animation_name);
        erhe_animation->set_source_path(m_path);
        erhe_animation->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        m_data_out.animations.push_back(erhe_animation);
        erhe_animation->samplers.resize(animation->samplers_count);
        for (cgltf_size sampler_index = 0; sampler_index < animation->samplers_count; ++sampler_index) {
            cgltf_animation_sampler& sampler = animation->samplers[sampler_index];
            auto& erhe_sampler = erhe_animation->samplers[sampler_index];
            if (sampler.input == nullptr) {
                log_gltf->warn("Animation `{}` sampler {} has nullptr input", animation_name, sampler_index);
                continue;
            }
            if (sampler.output == nullptr) {
                log_gltf->warn("Animation `{}` sampler {} has nullptr output", animation_name, sampler_index);
                continue;
            }
            log_gltf->trace(
                "Animation `{}` sampler index = {}, "
                "input type = {}, output type = {}, "
                "interpolation = {}, "
                "input timestamp count = {}, value count = {}",
                animation_name,
                sampler_index,
                c_str(sampler.input->type),
                c_str(sampler.output->type),
                c_str(sampler.interpolation),
                sampler.input->count,
                sampler.output->count
            );

            erhe_sampler.interpolation_mode = to_erhe(sampler.interpolation);
            const cgltf_size   output_component_count = cgltf_num_components(sampler.output->type);
            const cgltf_size   output_float_count     = sampler.output->count * output_component_count;
            std::vector<float> input_timestamps(sampler.input ->count);
            std::vector<float> output_values   (output_float_count);
            const cgltf_size   timestamps_read_count = cgltf_accessor_unpack_floats(
                sampler.input, input_timestamps.data(), sampler.input->count
            );
            if (timestamps_read_count != sampler.input->count) {
                log_gltf->warn(
                    "Animation `{}` channel {} timestamp read count mismatch: read {} timestamps, expected {}",
                    animation_name,
                    sampler_index,
                    timestamps_read_count,
                    sampler.input->count
                );
            }
            //for (cgltf_size j = 0; j < timestamps_read_count; ++j) {
            //    log_gltf->trace("Timestamp {}: {}", j, input_timestamps[j]);
            //}

            const cgltf_size value_read_count = cgltf_accessor_unpack_floats(
                sampler.output, output_values.data(), output_float_count
            );
            if (value_read_count != output_float_count) {
                log_gltf->warn(
                    "Animation `{}` sampler {} value read count mismatch: read {} values, expected {}",
                    animation_name,
                    sampler_index,
                    value_read_count,
                    output_float_count
                );
            }

            erhe_sampler.set(std::move(input_timestamps), std::move(output_values));
        }
        erhe_animation->channels.resize(animation->channels_count);
        for (cgltf_size channel_index = 0; channel_index < animation->channels_count; ++channel_index) {
            cgltf_animation_channel& channel = animation->channels[channel_index];
            if (channel.target_node == nullptr) {
                log_gltf->warn("Animation `{}` channel {} has nullptr target", animation_name, channel_index);
                continue;
            }
            if (channel.sampler == nullptr) {
                log_gltf->warn("Animation `{}` channel {} has nullptr sampler", animation_name, channel_index);
                continue;
            }
            erhe::scene::Animation_path path = to_erhe(channel.target_path);
            const auto& target_node = m_data_out.nodes.at(channel.target_node - m_data->nodes);
            const cgltf_size sampler_index = channel.sampler - animation->samplers;
            log_gltf->trace(
                "Channel {} target = {}, pos = {} path = {}, sampler index = {}",
                channel_index,
                target_node->get_name(),
                target_node->position_in_world(),
                c_str(channel.target_path),
                sampler_index
            );

            //const auto& sampler = new_animation->samplers.at(sampler_index);
            //const auto component_count = erhe::scene::get_component_count(to_erhe(channel.target_path));
            //cgltf_size t = 0;
            //for (cgltf_size j = 0; j < sampler.data.size();) {
            //    std::stringstream ss;
            //    ss << fmt::format("t = {}: ", sampler.timestamps.at(t++));
            //    bool first = true;
            //    for (cgltf_size k = 0; k < component_count; ++k) {
            //        if (!first) {
            //            ss << ", ";
            //        } else {
            //            first = false;
            //        }
            //        ss << fmt::format("v{} = {}", k, sampler.data[j]);
            //        ++j;
            //    }
            //    log_gltf->trace(ss.str());
            //}

            erhe_animation->channels[channel_index] = erhe::scene::Animation_channel{
                .path           = to_erhe(channel.target_path),
                .sampler_index  = sampler_index,
                .target         = target_node,
                .start_position = 0,
                .value_offset   = (channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                    ? get_component_count(path)
                    : 0
            };
        }
        m_data_out.animations[animation_index] = erhe_animation;
    }
    auto load_image_file(const std::filesystem::path& path) -> std::shared_ptr<erhe::graphics::Texture>
    {
        const bool file_is_ok = erhe::file::check_is_existing_non_empty_regular_file("Gltf_parser::load_image_file", path);
        if (!file_is_ok) {
            return {};
        }

        erhe::graphics::Image_info image_info;
        erhe::graphics::PNG_loader loader;

        if (!loader.open(path, image_info)) {
            return {};
        }

        auto& slot = m_image_transfer.get_slot();

        erhe::graphics::Texture_create_info texture_create_info{
            .instance        = m_graphics_instance,
            .internal_format = to_gl(image_info.format),
            .use_mipmaps     = true, //(image_info.level_count > 1),
            .width           = image_info.width,
            .height          = image_info.height,
            .depth           = image_info.depth,
            .level_count     = image_info.level_count,
            .row_stride      = image_info.row_stride,
        };
        const int  mipmap_count    = texture_create_info.calculate_level_count();
        const bool generate_mipmap = mipmap_count != image_info.level_count;
        if (generate_mipmap) {
            texture_create_info.level_count = mipmap_count;
        }
        gsl::span<std::byte> span = slot.begin_span_for(
            image_info.width,
            image_info.height,
            texture_create_info.internal_format
        );

        const bool ok = loader.load(span);
        loader.close();
        if (!ok) {
            slot.end();
            return {};
        }

        auto texture = std::make_shared<erhe::graphics::Texture>(texture_create_info);
        // TODO texture->set_source_path(m_path);
        texture->set_debug_label(erhe::file::to_string(path));

        gl::flush_mapped_named_buffer_range(slot.gl_name(), 0, span.size_bytes());
        gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(texture_create_info.internal_format, texture_create_info.width, texture_create_info.height);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        slot.end();
        if (generate_mipmap) {
            gl::generate_texture_mipmap(texture->gl_name());
        }
        return texture;
    }
    auto load_png_buffer(const cgltf_buffer_view* buffer_view) -> std::shared_ptr<erhe::graphics::Texture>
    {
        const cgltf_size  buffer_view_index = buffer_view - m_data->buffer_views;
        const std::string name              = safe_resource_name(buffer_view->name, "buffer_view", buffer_view_index);
        erhe::graphics::Image_info image_info;
        erhe::graphics::PNG_loader loader;

        const uint8_t*   data_u8 = cgltf_buffer_view_data(buffer_view);
        const std::byte* data    = reinterpret_cast<const std::byte*>(data_u8);
        gsl::span<const std::byte> png_encoded_buffer_view{
            data,
            static_cast<std::size_t>(buffer_view->size)
        };
        if (!loader.open(png_encoded_buffer_view, image_info)) {
            log_gltf->error("Failed to parse PNG encoded image from buffer view '{}'", name);
            return {};
        }

        auto& slot = m_image_transfer.get_slot();

        erhe::graphics::Texture_create_info texture_create_info{
            .instance        = m_graphics_instance,
            .internal_format = to_gl(image_info.format),
            .use_mipmaps     = true, //(image_info.level_count > 1),
            .width           = image_info.width,
            .height          = image_info.height,
            .depth           = image_info.depth,
            .level_count     = image_info.level_count,
            .row_stride      = image_info.row_stride,
        };
        const int  mipmap_count    = texture_create_info.calculate_level_count();
        const bool generate_mipmap = mipmap_count != image_info.level_count;
        if (generate_mipmap) {
            texture_create_info.level_count = mipmap_count;
        }
        gsl::span<std::byte> span = slot.begin_span_for(
            image_info.width,
            image_info.height,
            texture_create_info.internal_format
        );

        const bool ok = loader.load(span);
        loader.close();
        if (!ok) {
            slot.end();
            return {};
        }

        auto texture = std::make_shared<erhe::graphics::Texture>(texture_create_info);
        // TODO texture->set_source_path(m_path);
        texture->set_debug_label(name);

        gl::flush_mapped_named_buffer_range(slot.gl_name(), 0, span.size_bytes());
        gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(texture_create_info.internal_format, texture_create_info.width, texture_create_info.height);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        slot.end();
        if (generate_mipmap) {
            gl::generate_texture_mipmap(texture->gl_name());
        }
        return texture;
    }
    void parse_image(const cgltf_size image_index)
    {
        const cgltf_image* image = &m_data->images[image_index];
        const std::string image_name = safe_resource_name(image->name, "image", image_index);
        log_gltf->trace("Image: image index = {}, name = {}", image_index, image_name);

        std::shared_ptr<erhe::graphics::Texture> erhe_texture;
        if (image->uri != nullptr) {
            std::filesystem::path uri{image->uri};
            erhe_texture = load_image_file(m_path.replace_filename(uri));
            if (!erhe_texture) {
                erhe_texture = load_image_file(image->uri);
            }
        } else if (image->buffer_view != nullptr) {
            erhe_texture = load_png_buffer(image->buffer_view);
        }
        if (erhe_texture) {
            erhe_texture->set_debug_label(image_name);
            m_data_out.images.push_back(erhe_texture);
        }
        m_data_out.images[image_index] = erhe_texture;
    }
    void parse_sampler(const cgltf_size sampler_index)
    {
        const cgltf_sampler* sampler = &m_data->samplers[sampler_index];
        const std::string sampler_name = safe_resource_name(sampler->name, "sampler", sampler_index);
        log_gltf->trace("Sampler: sampler index = {}, name = {}", sampler_index, sampler_name);

        erhe::graphics::Sampler_create_info create_info;
        create_info.min_filter     = (sampler->min_filter != 0) ? static_cast<gl::Texture_min_filter>(sampler->min_filter) : gl::Texture_min_filter::nearest;
        create_info.mag_filter     = (sampler->mag_filter != 0) ? static_cast<gl::Texture_mag_filter>(sampler->mag_filter) : gl::Texture_mag_filter::nearest;
        create_info.wrap_mode[0]   = (sampler->wrap_s != 0) ? static_cast<gl::Texture_wrap_mode>(sampler->wrap_s) : gl::Texture_wrap_mode::repeat;
        create_info.wrap_mode[1]   = (sampler->wrap_t != 0) ? static_cast<gl::Texture_wrap_mode>(sampler->wrap_t) : gl::Texture_wrap_mode::repeat;
        create_info.max_anisotropy = m_graphics_instance.limits.max_texture_max_anisotropy;
        create_info.debug_label    = sampler_name;

        auto erhe_sampler = std::make_shared<erhe::graphics::Sampler>(create_info);
        // TODO erhe_sampler->set_source_path(m_path);
        erhe_sampler->set_debug_label(sampler_name);
        m_data_out.samplers[sampler_index] = erhe_sampler;
        m_data_out.samplers.push_back(erhe_sampler);
    }
    void parse_material(const cgltf_size material_index)
    {
        const cgltf_material* material = &m_data->materials[material_index];
        const std::string material_name = safe_resource_name(material->name, "material", material_index);
        log_gltf->trace(
            "Primitive material: id = {}, name = {}",
            material_index,
            material_name
        );

        auto new_material = std::make_shared<erhe::primitive::Material>(material_name);
        // TODO new_material->set_source_path(m_path);
        m_data_out.materials[material_index] = new_material;
        if (material->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr_metallic_roughness = material->pbr_metallic_roughness;
            if (pbr_metallic_roughness.base_color_texture.texture != nullptr) {
                const cgltf_texture* texture = pbr_metallic_roughness.base_color_texture.texture;
                if (texture->image != nullptr) {
                    const cgltf_size image_index = texture->image - m_data->images;
                    new_material->base_color_texture = m_data_out.images[image_index];
                }
                if (texture->sampler != nullptr) {
                    const cgltf_size sampler_index = texture->sampler - m_data->samplers;
                    new_material->base_color_sampler = m_data_out.samplers[sampler_index];
                }
            }
            if (pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
                const cgltf_texture* texture = pbr_metallic_roughness.metallic_roughness_texture.texture;
                if (texture->image != nullptr) {
                    const cgltf_size image_index = texture->image - m_data->images;
                    new_material->metallic_roughness_texture = m_data_out.images  [image_index];
                }
                if (texture->sampler != nullptr) {
                    const cgltf_size sampler_index = texture->sampler - m_data->samplers;
                    new_material->metallic_roughness_sampler = m_data_out.samplers[sampler_index];
                }
            }
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
            new_material->enable_flag_bits(erhe::Item_flags::show_in_ui);
            log_gltf->trace(
                "Material PBR metallic roughness base color factor = {}, {}, {}, {}",
                pbr_metallic_roughness.base_color_factor[0],
                pbr_metallic_roughness.base_color_factor[1],
                pbr_metallic_roughness.base_color_factor[2],
                pbr_metallic_roughness.base_color_factor[3]
            );
            log_gltf->trace(
                "Material PBR metallic roughness metallic factor = {}",
                pbr_metallic_roughness.metallic_factor
            );
            log_gltf->trace(
                "Material PBR metallic roughness roughness factor = {}",
                pbr_metallic_roughness.roughness_factor
            );
        }
        if (material->has_pbr_specular_glossiness) {
            const cgltf_pbr_specular_glossiness& pbr_specular_glossiness = material->pbr_specular_glossiness;
            log_gltf->trace(
                "Material PBR specular glossiness diffuse factor = {}, {}, {}, {}",
                pbr_specular_glossiness.diffuse_factor[0],
                pbr_specular_glossiness.diffuse_factor[1],
                pbr_specular_glossiness.diffuse_factor[2],
                pbr_specular_glossiness.diffuse_factor[3]
            );
            log_gltf->trace(
                "Material PBR specular glossiness specular factor = {}, {}, {}",
                pbr_specular_glossiness.specular_factor[0],
                pbr_specular_glossiness.specular_factor[1],
                pbr_specular_glossiness.specular_factor[2]
            );
            log_gltf->trace(
                "Material PBR specular glossiness glossiness factor = {}",
                pbr_specular_glossiness.glossiness_factor
            );
            log_gltf->warn("Material PBR specular glossiness is not yet implemented");
        }
    }
    void parse_node_transform(
        const cgltf_node*                         node,
        const std::shared_ptr<erhe::scene::Node>& erhe_node
    )
    {
        if (node->has_matrix) {
            const auto& m = node->matrix;
            const glm::mat4 matrix{
                m[ 0], m[ 1], m[ 2], m[ 3],
                m[ 4], m[ 5], m[ 6], m[ 7],
                m[ 8], m[ 9], m[10], m[11],
                m[12], m[13], m[14], m[15]
            };
            erhe_node->set_parent_from_node(matrix);
        } else {
            const auto& t = node->translation;
            const auto& r = node->rotation;
            const auto& s = node->scale;
            erhe_node->node_data.transforms.parent_from_node.set_trs(
                glm::vec3{t[0], t[1], t[2]},
                glm::quat{r[3], r[0], r[1], r[2]},
                glm::vec3{s[0], s[1], s[2]}
            );
            erhe_node->update_world_from_node();
            erhe_node->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
        }
    }
    void parse_camera(const cgltf_size camera_index)
    {
        const cgltf_camera* camera = &m_data->cameras[camera_index];
        const std::string camera_name = safe_resource_name(camera->name, "camera", camera_index);
        log_gltf->trace("Camera: camera index = {}, name = {}", camera_index, camera_name);

        auto erhe_camera = std::make_shared<erhe::scene::Camera>(camera_name);
        erhe_camera->set_source_path(m_path);
        m_data_out.cameras[camera_index] = erhe_camera;
        erhe_camera->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        auto* projection = erhe_camera->projection();
        switch (camera->type) {
            case cgltf_camera_type::cgltf_camera_type_perspective: {
                const cgltf_camera_perspective& perspective = camera->data.perspective;
                log_gltf->trace("Camera.has_aspect_ration: {}", perspective.has_aspect_ratio);
                log_gltf->trace("Camera.aspect_ratio:      {}", perspective.aspect_ratio);
                log_gltf->trace("Camera.yfov:              {}", perspective.yfov);
                log_gltf->trace("Camera.has_zfar:          {}", perspective.has_zfar);
                log_gltf->trace("Camera.zfar:              {}", perspective.zfar);
                log_gltf->trace("Camera.znear:             {}", perspective.znear);
                projection->projection_type = erhe::scene::Projection::Type::perspective_vertical;
                projection->fov_y           = perspective.yfov;
                projection->z_far           = (perspective.has_zfar != 0)
                    ? perspective.zfar
                    : 0.0f;
                break;
            }

            case cgltf_camera_type::cgltf_camera_type_orthographic: {
                const cgltf_camera_orthographic& orthographic = camera->data.orthographic;
                log_gltf->trace("Camera.xmag:              {}", orthographic.xmag);
                log_gltf->trace("Camera.ymag:              {}", orthographic.ymag);
                log_gltf->trace("Camera.zfar:              {}", orthographic.zfar);
                log_gltf->trace("Camera.znear:             {}", orthographic.znear);
                projection->projection_type = erhe::scene::Projection::Type::orthogonal;
                projection->ortho_width     = orthographic.xmag;
                projection->ortho_height    = orthographic.ymag;
                projection->z_far           = orthographic.zfar;
                projection->z_near          = orthographic.znear;
                break;
            }

            default: {
                log_gltf->warn("Camera.Projection: unknown projection type {}");
                break;
            }
        }
    }
    void parse_light(const cgltf_size light_index)
    {
        const cgltf_light* light = &m_data->lights[light_index];
        const std::string light_name = safe_resource_name(light->name, "light", light_index);
        log_gltf->trace("Light: camera index = {}, name = {}", light_index, light_name);

        auto erhe_light = std::make_shared<erhe::scene::Light>(light_name);
        erhe_light->set_source_path(m_path);
        m_data_out.lights[light_index] = erhe_light;
        erhe_light->color = glm::vec3{
            light->color[0],
            light->color[1],
            light->color[2]
        };
        erhe_light->intensity        = light->intensity;
        erhe_light->type             = to_erhe(light->type);
        erhe_light->range            = light->range;
        erhe_light->inner_spot_angle = light->spot_inner_cone_angle;
        erhe_light->outer_spot_angle = light->spot_outer_cone_angle;

        erhe_light->layer_id = 0;
        erhe_light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    }

    class Primitive_to_geometry
    {
    public:
        explicit Primitive_to_geometry(const cgltf_primitive* primitive)
            : primitive{primitive}
            , geometry {std::make_shared<erhe::geometry::Geometry>()}
        {
            std::unordered_map<cgltf_attribute_type, cgltf_int> attribute_max_index;
            for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
                const cgltf_attribute* const attribute = &primitive->attributes[i];
                auto j = attribute_max_index.find(attribute->type);
                if (j != attribute_max_index.end()) {
                    j->second = std::max(j->second, attribute->index);
                } else {
                    attribute_max_index[attribute->type] = attribute->index;
                }
            }
            for (auto [attribute_type, last_index] : attribute_max_index) {
                switch (attribute_type) {
                    case cgltf_attribute_type_position: point_locations    .resize(last_index + 1); break;
                    case cgltf_attribute_type_normal:   corner_normals     .resize(last_index + 1); break;
                    case cgltf_attribute_type_tangent:  corner_tangents    .resize(last_index + 1); break;
                    case cgltf_attribute_type_texcoord: corner_texcoords   .resize(last_index + 1); break;
                    case cgltf_attribute_type_color:    corner_colors      .resize(last_index + 1); break;
                    case cgltf_attribute_type_joints:   point_joint_indices.resize(last_index + 1); break;
                    case cgltf_attribute_type_weights:  point_joint_weights.resize(last_index + 1); break;
                    default: continue;
                }
            }
            for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
                const cgltf_attribute* const attribute = &primitive->attributes[i];
                switch (attribute->type) {
                    case cgltf_attribute_type_position: point_locations    [attribute->index] = geometry->point_attributes ().create<vec3 >(erhe::geometry::c_point_locations    ); break;
                    case cgltf_attribute_type_normal:   corner_normals     [attribute->index] = geometry->corner_attributes().create<vec3 >(erhe::geometry::c_corner_normals     ); break;
                    case cgltf_attribute_type_tangent:  corner_tangents    [attribute->index] = geometry->corner_attributes().create<vec4 >(erhe::geometry::c_corner_tangents    ); break;
                    case cgltf_attribute_type_texcoord: corner_texcoords   [attribute->index] = geometry->corner_attributes().create<vec2 >(erhe::geometry::c_corner_texcoords   ); break;
                    case cgltf_attribute_type_color:    corner_colors      [attribute->index] = geometry->corner_attributes().create<vec4 >(erhe::geometry::c_corner_colors      ); break;
                    case cgltf_attribute_type_joints:   point_joint_indices[attribute->index] = geometry->point_attributes ().create<uvec4>(erhe::geometry::c_point_joint_indices); break;
                    case cgltf_attribute_type_weights:  point_joint_weights[attribute->index] = geometry->point_attributes ().create<vec4 >(erhe::geometry::c_point_joint_weights); break;
                    default: continue;
                }
            }

            get_used_indices();
            make_points();
            switch (primitive->type) {
                case cgltf_primitive_type::cgltf_primitive_type_triangles: {
                    parse_triangles();
                    break;
                }
                default: {
                    log_gltf->warn("Unsupported glTF primitive type {}", c_str(primitive->type));
                    break;
                }
            }
            parse_vertex_data();
            geometry->make_point_corners();
            geometry->build_edges();
        }
        void get_used_indices()
        {
            const cgltf_accessor* const    accessor    = primitive->indices;
            const cgltf_buffer_view* const buffer_view = accessor->buffer_view;

            log_gltf->trace(
                "Index buffer component type = {}, type = {}, "
                "count = {}, accessor offset = {}, buffer view_offset = {}",
                c_str(accessor->component_type),
                c_str(accessor->type),
                accessor->count,
                accessor->offset,
                buffer_view->offset
            );

            used_indices.resize(primitive->indices->count);
            const cgltf_size unpack_count = cgltf_accessor_unpack_indices(accessor, &used_indices[0], primitive->indices->count);
            if (unpack_count != primitive->indices->count) {
                log_gltf->error(
                    "cgltf_accessor_unpack_indices() failed: expected {}, got {}",
                    primitive->indices->count,
                    unpack_count
                );
                return;
            }

            // Remove duplicates
            std::sort(used_indices.begin(), used_indices.end());
            used_indices.erase(
                std::unique(
                    used_indices.begin(),
                    used_indices.end()
                ),
                used_indices.end()
            );

            // First and last index
            max_index = cgltf_size{0};
            min_index = std::numeric_limits<cgltf_size>::max();
            for (const cgltf_size index : used_indices) {
                min_index = std::min(index, min_index);
                max_index = std::max(index, max_index);
            }
        }
        void make_points()
        {
            cgltf_attribute* position_attribute{nullptr};
            cgltf_int min_attribute_index = std::numeric_limits<cgltf_int>::max();
            for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
                cgltf_attribute* attribute = &primitive->attributes[i];
                if (
                    (attribute->type == cgltf_attribute_type::cgltf_attribute_type_position) &&
                    (attribute->index < min_attribute_index)
                ) {
                    position_attribute = attribute;
                    min_attribute_index = attribute->index;
                }
            }
            if (position_attribute == nullptr) {
                log_gltf->warn("No vertex position attribute found");
                return;
            }

            // Get vertex positions and calculate bounding box
            vertex_positions.clear();
            cgltf_accessor* accessor = position_attribute->data;

            const cgltf_size num_components = std::min(
                cgltf_size{3},
                cgltf_num_components(accessor->type)
            );
            vec3 min_corner{std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max()};
            vec3 max_corner{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            const cgltf_size vertex_count = max_index - min_index + 1;
            vertex_positions.resize(vertex_count);
            switch (accessor->type) {
                case cgltf_type::cgltf_type_scalar:
                case cgltf_type::cgltf_type_vec2:
                case cgltf_type::cgltf_type_vec3:
                case cgltf_type::cgltf_type_vec4: {
                    for (cgltf_size index : used_indices) {
                        cgltf_float v[3] = { 0.0f, 0.0f, 0.0f };
                        cgltf_accessor_read_float(accessor, index, &v[0], num_components);
                        const auto position = glm::vec3{v[0], v[1], v[2]};
                        vertex_positions.at(index - min_index) = position;
                        min_corner = glm::min(min_corner, position);
                        max_corner = glm::max(max_corner, position);
                    }
                    break;
                }
                default: {
                    log_gltf->warn("Unsupported glTF position attribute accessor type");
                    return;
                }
            }

            // Sort vertices
            std::set<glm::vec3::length_type> available_axis = { 0, 1, 2 };
            std::set<glm::vec3::length_type> used_axis;

            const vec3 bounding_box_size0 = max_corner - min_corner;
            const auto axis0 = erhe::math::max_axis_index<float>(bounding_box_size0);
            available_axis.erase(axis0);
            used_axis.insert(axis0);
            vec3 bounding_box_size1 = bounding_box_size0;
            bounding_box_size1[axis0] = 0.0f;

            auto axis1 = erhe::math::max_axis_index<float>(bounding_box_size1);
            if (used_axis.count(axis1) > 0) {
                axis1 = *available_axis.begin();
            }
            available_axis.erase(axis1);
            used_axis.insert(axis1);

            vec3 bounding_box_size2 = bounding_box_size1;
            bounding_box_size2[axis1] = 0.0f;
            auto axis2 = erhe::math::max_axis_index<float>(bounding_box_size2);
            if (used_axis.count(axis2) > 0) {
                axis2 = * available_axis.begin();
            }
            available_axis.erase(axis2);
            used_axis.insert(axis2);

            log_gltf->trace("Bounding box   = {}", bounding_box_size0);
            log_gltf->trace("Primary   axis = {}", "XYZ"[axis0]);
            log_gltf->trace("Secondary axis = {}", "XYZ"[axis1]);
            log_gltf->trace("Tertiary  axis = {}", "XYZ"[axis2]);

            sorted_vertex_indices    .resize(vertex_count);
            point_id_from_gltf_index .resize(vertex_count);

            std::fill(
                sorted_vertex_indices.begin(),
                sorted_vertex_indices.end(),
                std::numeric_limits<cgltf_size>::max()
            );
            std::fill(
                point_id_from_gltf_index.begin(),
                point_id_from_gltf_index.end(),
                std::numeric_limits<Point_id>::max()
            );
            for (cgltf_size index : used_indices) {
                sorted_vertex_indices[index - min_index] = index;
            }

            std::sort(
                sorted_vertex_indices.begin(),
                sorted_vertex_indices.end(),
                [this, axis0, axis1, axis2](const cgltf_size lhs_index, const cgltf_size rhs_index) {
                    if (rhs_index == std::numeric_limits<cgltf_size>::max()) {
                        return true;
                    }
                    const vec3 position_lhs = vertex_positions[lhs_index - min_index];
                    const vec3 position_rhs = vertex_positions[rhs_index - min_index];
                    if (position_lhs[axis0] != position_rhs[axis0]) {
                        return position_lhs[axis0] < position_rhs[axis0];
                    }
                    if (position_lhs[axis1] != position_rhs[axis1]) {
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
            for (cgltf_size index : sorted_vertex_indices) {
                if (index == std::numeric_limits<cgltf_size>::max()) {
                    continue;
                }
                const vec3 position = vertex_positions[index - min_index];
                if (position != previous_position) {
                    point_id = geometry->make_point();
                    previous_position = position;
                } else {
                    ++point_share_count;
                }
                point_id_from_gltf_index[index - min_index] = point_id;
            }
            log_gltf->trace(
                "point count = {}, point share count = {}",
                geometry->get_point_count(),
                point_share_count
            );
        }
        void parse_triangles()
        {
            const cgltf_accessor* accessor = primitive->indices;
            const cgltf_size triangle_count = accessor->count / 3;

            log_gltf->trace(
                "index count = {}, unique vertex count = {}, triangle count = {}",
                accessor->count,
                used_indices.size(),
                triangle_count
            );

            geometry->reserve_polygons(triangle_count);
            corner_id_start = geometry->m_next_corner_id;
            gltf_index_from_corner_id.resize(3 * accessor->count);
            for (cgltf_size i = 0; i < accessor->count;) {
                const cgltf_size v0         = cgltf_accessor_read_index(accessor, i++);
                const cgltf_size v1         = cgltf_accessor_read_index(accessor, i++);
                const cgltf_size v2         = cgltf_accessor_read_index(accessor, i++);
                const Point_id   p0         = point_id_from_gltf_index.at(v0 - min_index);
                const Point_id   p1         = point_id_from_gltf_index.at(v1 - min_index);
                const Point_id   p2         = point_id_from_gltf_index.at(v2 - min_index);
                const Polygon_id polygon_id = geometry->make_polygon();
                const Corner_id  c0         = geometry->make_polygon_corner(polygon_id, p0);
                const Corner_id  c1         = geometry->make_polygon_corner(polygon_id, p1);
                const Corner_id  c2         = geometry->make_polygon_corner(polygon_id, p2);
                gltf_index_from_corner_id[c0 - corner_id_start] = v0;
                gltf_index_from_corner_id[c1 - corner_id_start] = v1;
                gltf_index_from_corner_id[c2 - corner_id_start] = v2;
                SPDLOG_LOGGER_TRACE(log_parsers, "vertex {} corner {} for polygon {}", v0, c0, polygon_id);
                SPDLOG_LOGGER_TRACE(log_parsers, "vertex {} corner {} for polygon {}", v1, c1, polygon_id);
                SPDLOG_LOGGER_TRACE(log_parsers, "vertex {} corner {} for polygon {}", v2, c2, polygon_id);
            }
            corner_id_end = geometry->m_next_corner_id;
        }
        void parse_vertex_data()
        {
            for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
                const cgltf_attribute* const attribute = &primitive->attributes[i];
                const cgltf_accessor* accessor = attribute->data;

                log_gltf->trace(
                    "Primitive attribute[{}]: name = {}, attribute type = {}[{}], "
                    "component type = {}, accessor type = {}, normalized = {}, count = {}, "
                    "stride = {}, accessor offset = {}",
                    i,
                    attribute->name,
                    c_str(attribute->type), // semantics
                    attribute->index,
                    c_str(accessor->component_type),
                    c_str(accessor->type),
                    accessor->normalized != 0,
                    accessor->count,
                    accessor->stride,
                    accessor->offset
                );

                const cgltf_size component_count = cgltf_num_components(accessor->type);
                if (is_per_point(attribute->type)) {
                    for (cgltf_size j = 0; j < accessor->count; ++j) {
                        const auto point_id = point_id_from_gltf_index.at(j - min_index);
                        if (point_id == std::numeric_limits<Point_id>::max()) {
                            continue;
                        }

                        if (is_float(accessor->component_type)) {
                            cgltf_float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            cgltf_accessor_read_float(accessor, j, &value[0], component_count);
                            put_point_attribute<cgltf_float>(attribute, point_id, value);
                        } else {
                            cgltf_uint value[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(accessor, j, &value[0], component_count);
                            put_point_attribute<cgltf_uint>(attribute, point_id, value);
                        }
                    }
                } else {
                    for (erhe::geometry::Corner_id corner_id = corner_id_start; corner_id != corner_id_end; ++corner_id) {
                        const cgltf_size j = gltf_index_from_corner_id.at(corner_id - corner_id_start);
                        if (is_float(accessor->component_type)) {
                            cgltf_float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            cgltf_accessor_read_float(accessor, j, &value[0], component_count);
                            put_corner_attribute<cgltf_float>(attribute, corner_id, value);
                        } else {
                            cgltf_uint value[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(accessor, j, &value[0], component_count);
                            put_corner_attribute<cgltf_uint>(attribute, corner_id, value);
                        }
                    }
                }
            }
        }

        template <typename T>
        void put_point_attribute(
            const cgltf_attribute*   attribute,
            erhe::geometry::Point_id point_id,
            T                        value[4]
        )
        {
            switch (attribute->type) {
                case cgltf_attribute_type::cgltf_attribute_type_position: {
                    point_locations[attribute->index]->put(point_id, glm::vec3{value[0], value[1], value[2]});
                    break;
                }
                case cgltf_attribute_type::cgltf_attribute_type_joints: {
                    point_joint_indices[attribute->index]->put(point_id, glm::uvec4{value[0], value[1], value[2], value[3]});
                    break;
                }
                case cgltf_attribute_type::cgltf_attribute_type_weights: {
                    point_joint_weights[attribute->index]->put(point_id, glm::vec4{value[0], value[1], value[2], value[3]});
                    break;
                }
                default: {
                    log_gltf->warn("Unsupported glTF attribute type {}", c_str(attribute->type));
                    break;
                }
            }
        }

        template <typename T>
        void put_corner_attribute(
            const cgltf_attribute*    attribute,
            erhe::geometry::Corner_id corner_id,
            T                         value[4]
        )
        {
            switch (attribute->type) {
                case cgltf_attribute_type::cgltf_attribute_type_normal: {
                    corner_normals[attribute->index]->put(corner_id, glm::vec3{value[0], value[1], value[2]});
                    break; 
                }

                case cgltf_attribute_type::cgltf_attribute_type_tangent: {
                    corner_tangents[attribute->index]->put(corner_id, glm::vec4{value[0], value[1], value[2], value[3]});
                    break;
                }

                case cgltf_attribute_type::cgltf_attribute_type_texcoord: {
                    corner_texcoords[attribute->index]->put(corner_id, glm::vec2{value[0], value[1]}); break;
                    break;
                }
                case cgltf_attribute_type::cgltf_attribute_type_color: {
                    corner_colors[attribute->index]->put(corner_id, glm::vec4{value[0], value[1], value[2], value[3]});
                    break;
                }
                default: {
                    log_gltf->warn("Unsupported glTF attribute type {}", c_str(attribute->type));
                    break;
                }
            }
        }

        const cgltf_primitive*                    primitive                {nullptr};
        std::shared_ptr<erhe::geometry::Geometry> geometry                 {};
        cgltf_size                                min_index                {0};
        cgltf_size                                max_index                {0};
        std::vector<uint32_t>                     used_indices             {};
        std::vector<glm::vec3>                    vertex_positions         {};
        std::vector<cgltf_size>                   sorted_vertex_indices    {};
        std::vector<erhe::geometry::Point_id>     point_id_from_gltf_index {};
        erhe::geometry::Corner_id                 corner_id_start          {};
        erhe::geometry::Corner_id                 corner_id_end            {};
        std::vector<cgltf_size>                   gltf_index_from_corner_id{};

        std::vector<Property_map<erhe::geometry::Corner_id, glm::vec3>*> corner_normals     ;
        std::vector<Property_map<erhe::geometry::Corner_id, glm::vec4>*> corner_tangents    ;
        std::vector<Property_map<erhe::geometry::Corner_id, glm::vec4>*> corner_bitangents  ;
        std::vector<Property_map<erhe::geometry::Corner_id, glm::vec2>*> corner_texcoords   ;
        std::vector<Property_map<erhe::geometry::Corner_id, glm::vec4>*> corner_colors      ;
        std::vector<Property_map<erhe::geometry::Corner_id, uint32_t>* > corner_indices     ;
        std::vector<Property_map<erhe::geometry::Point_id, glm::vec3>* > point_locations    ;
        std::vector<Property_map<erhe::geometry::Point_id, glm::uvec4>*> point_joint_indices;
        std::vector<Property_map<erhe::geometry::Point_id, glm::vec4>* > point_joint_weights;
    };

    class Geometry_entry
    {
    public:
        cgltf_size                                           index_accessor;
        std::vector<cgltf_size>                              attribute_accessors;
        std::shared_ptr<erhe::geometry::Geometry>            geometry;
        std::shared_ptr<erhe::primitive::Geometry_primitive> geometry_primitive;
    };
    std::vector<Geometry_entry> m_geometries;
    void load_new_primitive_geometry(const cgltf_primitive* primitive, Geometry_entry& geometry_entry)
    {
        Primitive_to_geometry primitive_to_geometry{primitive};
        geometry_entry.geometry = primitive_to_geometry.geometry;
        if (primitive_to_geometry.corner_tangents.empty()) {
            if (primitive_to_geometry.corner_texcoords.empty()) {
                primitive_to_geometry.geometry->generate_polygon_texture_coordinates();
            }
            primitive_to_geometry.geometry->compute_tangents();
        }
        geometry_entry.geometry_primitive = std::make_shared<erhe::primitive::Geometry_primitive>(
            geometry_entry.geometry
        );
        m_data_out.geometries.push_back(primitive_to_geometry.geometry);
        m_data_out.geometry_primitives.push_back(geometry_entry.geometry_primitive);
    }
    auto get_primitive_geometry(const cgltf_primitive* primitive, Geometry_entry& geometry_entry)
    {
        geometry_entry.index_accessor = static_cast<cgltf_size>(primitive->indices - m_data->accessors);
        geometry_entry.attribute_accessors.clear();
        for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
            const cgltf_accessor* accessor = primitive->attributes[i].data;
            const cgltf_size attribute_accessor_index = accessor - m_data->accessors;
            geometry_entry.attribute_accessors.push_back(attribute_accessor_index);
        }

        for (const auto& entry : m_geometries) {
            if (entry.index_accessor != geometry_entry.index_accessor) continue;
            if (entry.attribute_accessors.size() != geometry_entry.attribute_accessors.size()) continue;
            for (std::size_t i = 0, end = entry.attribute_accessors.size(); i < end; ++i) {
                if (entry.attribute_accessors[i] != geometry_entry.attribute_accessors[i]) continue;
            }
            // Found existing entry
            geometry_entry = entry;
            return;
        }

        load_new_primitive_geometry(primitive, geometry_entry);
    }

    void parse_primitive(
        const std::shared_ptr<erhe::scene::Mesh>& erhe_mesh,
        const cgltf_mesh*                         mesh,
        const cgltf_size                          primitive_index
    )
    {
        const cgltf_primitive* primitive = &mesh->primitives[primitive_index];

        std::string name = (mesh->name != nullptr)
            ? fmt::format("{}[{}]", mesh->name, primitive_index)
            : fmt::format("primitive[{}] {}", primitive_index, c_str(primitive->type));

        Geometry_entry geometry_entry;
        get_primitive_geometry(primitive, geometry_entry);

        erhe_mesh->add_primitive(
            erhe::primitive::Primitive{
                .material        = (primitive->material != nullptr)
                    ? m_data_out.materials.at(primitive->material - m_data->materials)
                    : std::shared_ptr<erhe::primitive::Material>{},
                .geometry_primitive = geometry_entry.geometry_primitive
            }
        );
    }
    void parse_skin(const cgltf_size skin_index)
    {
        const cgltf_skin* skin = &m_data->skins[skin_index];
        const std::string skin_name = safe_resource_name(skin->name, "skin", skin_index);
        log_gltf->info("Skin: skin index = {}, name = {}", skin_index, skin_name);

        auto erhe_skin = std::make_shared<erhe::scene::Skin>(skin_name);
        erhe_skin->set_source_path(m_path);
        m_data_out.skins[skin_index] = erhe_skin;
        erhe_skin->enable_flag_bits(
            Item_flags::content    |
            Item_flags::show_in_ui |
            Item_flags::id
        );
        erhe_skin->skin_data.joints.resize(skin->joints_count);
        erhe_skin->skin_data.inverse_bind_matrices.resize(skin->joints_count);
        for (cgltf_size i = 0; i < skin->joints_count; ++i) {
            if (skin->joints[i] != nullptr) {
                const cgltf_size joint_node_index = skin->joints[i] - m_data->nodes;
                auto& erhe_joint_node = m_data_out.nodes.at(joint_node_index);
                ERHE_VERIFY(erhe_joint_node);
                erhe_skin->skin_data.joints[i] = erhe_joint_node;
            } else {
                log_gltf->warn("Skin {} joint {} node missing", skin_index, i);
            }
            if (skin->inverse_bind_matrices != nullptr) {
                cgltf_float m[16];
                cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16);
                const glm::mat4 matrix{
                    m[ 0], m[ 1], m[ 2], m[ 3],
                    m[ 4], m[ 5], m[ 6], m[ 7],
                    m[ 8], m[ 9], m[10], m[11],
                    m[12], m[13], m[14], m[15]
                };
                erhe_skin->skin_data.inverse_bind_matrices[i] = matrix;
            } else {
                log_gltf->warn("Skin {} joint {} inverse bind matrix missing", skin_index, i);
                erhe_skin->skin_data.inverse_bind_matrices[i] = glm::mat4{1.0f};
            }
        }
        if (skin->skeleton != nullptr) {
            const cgltf_size skeleton_node_index = skin->skeleton - m_data->nodes;
            erhe_skin->skin_data.skeleton = m_data_out.nodes.at(skeleton_node_index);
        } else {
            erhe_skin->skin_data.skeleton.reset();;
        }
    }
    void parse_mesh(const cgltf_size mesh_index)
    {
        const cgltf_mesh* mesh = &m_data->meshes[mesh_index];
        const std::string mesh_name = safe_resource_name(mesh->name, "mesh", mesh_index);
        log_gltf->trace("Mesh: mesh index = {}, name = {}", mesh_index, mesh_name);

        auto erhe_mesh = std::make_shared<erhe::scene::Mesh>(mesh_name);
        erhe_mesh->set_source_path(m_path);
        erhe_mesh->layer_id = m_mesh_layer_id;
        m_data_out.meshes[mesh_index] = erhe_mesh;
        erhe_mesh->enable_flag_bits(
            Item_flags::content     |
            Item_flags::visible     |
            Item_flags::show_in_ui  |
            Item_flags::shadow_cast |
            Item_flags::opaque      |
            Item_flags::id
        );
        for (cgltf_size i = 0; i < mesh->primitives_count; ++i) {
            parse_primitive(erhe_mesh, mesh, i);
        }
    }
    void parse_node(
        const cgltf_node*                         node,
        const std::shared_ptr<erhe::scene::Node>& parent
    )
    {
        const cgltf_size node_index = node - m_data->nodes;
        const std::string node_name = safe_resource_name(node->name, "node", node_index);
        log_gltf->trace("Node: node index = {}, name = {}", node_index, node_name);
        auto erhe_node = std::make_shared<erhe::scene::Node>(node_name);
        erhe_node->set_source_path(m_path);
        erhe_node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        m_data_out.nodes[node_index] = erhe_node;
        erhe_node->Hierarchy::set_parent(parent);
        parse_node_transform(node, erhe_node);

        for (cgltf_size i = 0; i < node->children_count; ++i) {
            parse_node(node->children[i], erhe_node);
        }
    }
    void fix_pointers(const cgltf_size node_index)
    {
        const cgltf_node* node = &m_data->nodes[node_index];
        auto& erhe_node = m_data_out.nodes.at(node_index);
        for (cgltf_size i = 0; i < node->children_count; ++i) {
            cgltf_node* child_node       = node->children[i];
            cgltf_size  child_node_index = child_node - m_data->nodes;
            auto& erhe_child_node        = m_data_out.nodes.at(child_node_index);
            erhe_child_node->set_parent(erhe_node);
        }

        if (node->camera != nullptr) {
            const cgltf_size camera_index = node->camera - m_data->cameras;
            erhe_node->attach(m_data_out.cameras[camera_index]);
        }

        if (node->light != nullptr) {
            const cgltf_size light_index = node->light - m_data->lights;
            erhe_node->attach(m_data_out.lights[light_index]);
        }

        if (node->mesh != nullptr) {
            const cgltf_size mesh_index = node->mesh - m_data->meshes;
            const auto erhe_mesh = m_data_out.meshes[mesh_index];
            if (node->skin != nullptr) {
                const cgltf_size skin_index = node->skin - m_data->skins;
                erhe_mesh->skin = m_data_out.skins[skin_index];
            }
            erhe_node->attach(erhe_mesh);
        }
    }

    Gltf_data&                         m_data_out;
    erhe::graphics::Instance&          m_graphics_instance;
    Image_transfer&                    m_image_transfer;
    std::shared_ptr<erhe::scene::Node> m_root_node;
    erhe::scene::Layer_id              m_mesh_layer_id;
    std::filesystem::path              m_path;
    cgltf_data*                        m_data{nullptr};
};

auto parse_gltf(
    erhe::graphics::Instance&                 graphics_instance,
    Image_transfer&                           image_transfer,
    const std::shared_ptr<erhe::scene::Node>& root_node,
    erhe::scene::Layer_id                     mesh_layer_id,
    std::filesystem::path                     path
) -> Gltf_data
{
    Gltf_data result;
    Gltf_parser parser{result, graphics_instance, image_transfer, root_node, mesh_layer_id, path};
    parser.parse_and_build();
    return result;
}

auto scan_gltf(std::filesystem::path path) -> Gltf_scan
{
    std::optional<std::string> file_contents_opt = erhe::file::read("GLTF file", path);
    if (!file_contents_opt.has_value()) {
        return {};
    }
    const cgltf_options parse_options{
        .type             = cgltf_file_type_invalid, // auto
        .json_token_count = 0, // 0 == auto
        .memory = {
            .alloc_func   = nullptr,
            .free_func    = nullptr,
            .user_data    = nullptr
        },
        .file = {
            .read         = cgltf_custom_file_read,
            .release      = cgltf_custom_file_release,
            .user_data    = nullptr
        }
    };

    const std::string& file_contents = file_contents_opt.value();
    cgltf_data* data = nullptr;
    const cgltf_result parse_result = cgltf_parse(
        &parse_options,
        file_contents.data(),
        file_contents.size(),
        &data
    );

    if (parse_result != cgltf_result::cgltf_result_success) {
        log_gltf->error("glTF parse error: {}", c_str(parse_result));
        return {};
    }
    if (data == nullptr) {
        log_gltf->error("No data loaded to parse glTF");
        return {};
    }

    Gltf_scan result;
    result.images.resize(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        result.images[i] = safe_resource_name(data->images[i].name, "image", i);
    }

    result.samplers.resize(data->samplers_count);
    for (cgltf_size i = 0; i < data->samplers_count; ++i) {
        result.samplers[i] = safe_resource_name(data->samplers[i].name, "sampler", i);
    }

    result.materials.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        result.materials[i] = safe_resource_name(data->materials[i].name, "material", i);
    }

    result.cameras.resize(data->cameras_count);
    for (cgltf_size i = 0; i < data->cameras_count; ++i) {
        result.cameras[i] = safe_resource_name(data->cameras[i].name, "camera", i);
    }

    result.lights.resize(data->lights_count);
    for (cgltf_size i = 0; i < data->lights_count; ++i) {
        result.lights[i] = safe_resource_name(data->lights[i].name, "light", i);
    }

    result.meshes.resize(data->meshes_count);
    for (cgltf_size i = 0; i < data->meshes_count; ++i) {
        result.meshes[i] = safe_resource_name(data->meshes[i].name, "mesh", i);
    }

    result.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        result.nodes[i] = safe_resource_name(data->nodes[i].name, "node", i);
    }

    result.skins.resize(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        result.skins[i] = safe_resource_name(data->skins[i].name, "skin", i);
    }

    result.animations.resize(data->animations_count);
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        result.animations[i] = safe_resource_name(data->animations[i].name, "animation", i);
    }

    result.scenes.resize(data->scenes_count);
    for (cgltf_size i = 0; i < data->scenes_count; ++i) {
        result.scenes[i] = safe_resource_name(data->scenes[i].name, "scene", i);
    }

    return result;
}

} // namespace example
