// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "gltf.hpp"
#include "example_log.hpp"
#include "image_transfer.hpp"

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/png_loader.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

extern "C" {
    #include "cgltf.h"
}

#include <glm/glm.hpp>
#include <gsl/gsl>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <unordered_set>

#include <vector>

namespace example {

namespace {

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

[[nodiscard]] auto to_erhe(const cgltf_primitive_type value) -> igl::PrimitiveType
{
    switch (value) {
        case cgltf_primitive_type::cgltf_primitive_type_points:         return igl::PrimitiveType::points;
        case cgltf_primitive_type::cgltf_primitive_type_lines:          return igl::PrimitiveType::lines;
        case cgltf_primitive_type::cgltf_primitive_type_line_loop:      return igl::PrimitiveType::line_loop;
        case cgltf_primitive_type::cgltf_primitive_type_line_strip:     return igl::PrimitiveType::line_strip;
        case cgltf_primitive_type::cgltf_primitive_type_triangles:      return igl::PrimitiveType::triangles;
        case cgltf_primitive_type::cgltf_primitive_type_triangle_strip: return igl::PrimitiveType::triangle_strip;
        case cgltf_primitive_type::cgltf_primitive_type_triangle_fan:   return igl::PrimitiveType::triangle_fan;
        default:                                                        return igl::PrimitiveType::points;
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

class Gltf_parser
{
public:
    const cgltf_size null_index{std::numeric_limits<cgltf_size>::max()};

    explicit Gltf_parser(Parse_context& context)
        : m_context{context}
    {
        if (!open(context.path)) {
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
        m_default_material = std::make_shared<erhe::primitive::Material>(
            "Default",
            glm::vec3{0.500f, 0.500f, 0.500f},
            glm::vec2{1.0f, 1.0f},
            0.0f
        );

        m_images.resize(m_data->images_count);
        for (cgltf_size i = 0; i < m_data->images_count; ++i) {
            parse_image(i);
        }

        m_samplers.resize(m_data->samplers_count);
        for (cgltf_size i = 0; i < m_data->samplers_count; ++i) {
            parse_sampler(i);
        }

        m_materials.resize(m_data->materials_count);
        for (cgltf_size i = 0; i < m_data->materials_count; ++i) {
            parse_material(i);
        }

        m_cameras.resize(m_data->cameras_count);
        for (cgltf_size i = 0; i < m_data->cameras_count; ++i) {
            parse_camera(i);
        }

        m_lights.resize(m_data->lights_count);
        for (cgltf_size i = 0; i < m_data->lights_count; ++i) {
            parse_light(i);
        }

        m_meshes.resize(m_data->meshes_count);
        for (cgltf_size i = 0; i < m_data->meshes_count; ++i) {
            parse_mesh(i);
        }

        m_skins.resize(m_data->skins_count);
        for (cgltf_size i = 0; i < m_data->skins_count; ++i) {
            parse_skin(i);
        }

        const auto root = m_context.scene.get_root_node();
        m_nodes.resize(m_data->nodes_count);
        for (cgltf_size i = 0; i < m_data->nodes_count; ++i) {
            parse_node(&m_data->nodes[i], root);
        }

        for (cgltf_size i = 0; i < m_data->nodes_count; ++i) {
            fix_pointers(i);
        }

        m_animations.reserve(m_data->animations_count);
        for (cgltf_size i = 0; i < m_data->animations_count; ++i) {
            parse_animation(i);
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

        if (parse_result != cgltf_result::cgltf_result_success) {
            log_gltf->error("glTF parse error: {}", c_str(parse_result));
            return false;
        }

        const cgltf_result load_buffers_result = cgltf_load_buffers(&parse_options, m_data, path.string().c_str());
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
        m_context.animations.push_back(erhe_animation);
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
            log_gltf->info(
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
            const auto& target_node = m_nodes.at(channel.target_node - m_data->nodes);
            const cgltf_size sampler_index = channel.sampler - animation->samplers;
            log_gltf->info(
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
        m_animations[animation_index] = erhe_animation;
    }
    auto load_image_file(
        const std::filesystem::path& path
    ) -> std::shared_ptr<erhe::graphics::Texture>
    {
        const bool is_ok = erhe::file::check_is_existing_non_empty_regular_file("Gltf_parser::load_image_file", path);
        if (!is_ok) {
            return {};
        }

        erhe::graphics::Image_info image_info;
        erhe::graphics::PNG_loader loader;

        if (!loader.open(path, image_info)) {
            return {};
        }

        auto& slot = m_context.image_transfer.get_slot();

        erhe::graphics::Texture_create_info texture_create_info{
            .instance        = m_context.graphics_instance,
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
        texture->set_debug_label(path.string());

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
    auto load_png_buffer(
	    const cgltf_buffer_view* buffer_view
    ) -> std::shared_ptr<erhe::graphics::Texture>
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

        auto& slot = m_context.image_transfer.get_slot();

        erhe::graphics::Texture_create_info texture_create_info{
            .instance        = m_context.graphics_instance,
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
            erhe_texture = load_image_file(m_context.path.replace_filename(uri));
            if (!erhe_texture) {
                erhe_texture = load_image_file(image->uri);
            }
        } else if (image->buffer_view != nullptr) {
            erhe_texture = load_png_buffer(image->buffer_view);
        }
        if (erhe_texture) {
            erhe_texture->set_debug_label(image_name);
            m_context.images.push_back(erhe_texture);
        }
        m_images[image_index] = erhe_texture;
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
        create_info.max_anisotropy = m_context.graphics_instance.limits.max_texture_max_anisotropy;
        create_info.debug_label    = sampler_name;

        auto erhe_sampler = std::make_shared<erhe::graphics::Sampler>(create_info);
        erhe_sampler->set_debug_label(sampler_name);
        m_samplers[sampler_index] = erhe_sampler;
        m_context.samplers.push_back(erhe_sampler);
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
        m_context.materials.push_back(new_material);
        m_materials[material_index] = new_material;
        if (material->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr_metallic_roughness = material->pbr_metallic_roughness;
            if (pbr_metallic_roughness.base_color_texture.texture != nullptr) {
                const cgltf_texture* texture = pbr_metallic_roughness.base_color_texture.texture;
                if (texture->image != nullptr) {
                    const cgltf_size image_index = texture->image - m_data->images;
                    new_material->base_color_texture = m_images[image_index];
                }
                if (texture->sampler != nullptr) {
                    const cgltf_size sampler_index = texture->sampler - m_data->samplers;
                    new_material->base_color_sampler = m_samplers[sampler_index];
                }
            }
            if (pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
                const cgltf_texture* texture = pbr_metallic_roughness.metallic_roughness_texture.texture;
                if (texture->image != nullptr) {
                    const cgltf_size image_index = texture->image - m_data->images;
                    new_material->metallic_roughness_texture = m_images[image_index];
                }
                if (texture->sampler != nullptr) {
                    const cgltf_size sampler_index = texture->sampler - m_data->samplers;
                    new_material->metallic_roughness_sampler = m_samplers[sampler_index];
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
    void parse_camera(const cgltf_size camera_index)
    {
        const cgltf_camera* camera = &m_data->cameras[camera_index];
        const std::string camera_name = safe_resource_name(camera->name, "camera", camera_index);
        log_gltf->trace("Camera: camera index = {}, name = {}", camera_index, camera_name);

        auto erhe_camera = std::make_shared<erhe::scene::Camera>(camera_name);
        m_context.cameras.push_back(erhe_camera);
        m_cameras[camera_index] = erhe_camera;
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

            case cgltf_camera_type::cgltf_camera_type_orthographic:  {
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
        m_context.lights.push_back(erhe_light);
        m_lights[light_index] = erhe_light;
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
    void parse_primitive(
        const std::shared_ptr<erhe::scene::Mesh>& erhe_mesh,
        const cgltf_mesh*                         mesh,
        const cgltf_size                          primitive_index
    )
    {
        const cgltf_primitive* primitive = &mesh->primitives[primitive_index];

        auto name = (mesh->name != nullptr)
            ? fmt::format("{}[{}]", mesh->name, primitive_index)
            : fmt::format("primitive[{}]", primitive_index);

        log_gltf->trace("Primitive type: {}", c_str(primitive->type));

        // NOTE: If same geometry is used in multiple primitives,
        //       this naive loader will create duplicate vertex
        //       and index buffer sections for each.
        //       Parser in erhe::gltf does better job, but it goes
        //       through erhe::geometry::Geometry.
        erhe::primitive::Geometry_mesh erhe_gl_primitive;

        // Index buffer
        {
            const cgltf_accessor* const    accessor     = primitive->indices;
            const cgltf_buffer_view* const buffer_view  = accessor->buffer_view;
            const cgltf_buffer* const      buffer       = buffer_view->buffer;
            const cgltf_size               buffer_index = buffer - m_data->buffers;

            log_gltf->trace(
                "Index buffer index = {}, component type = {}, type = {}, "
                "count = {}, accessor offset = {}, buffer view_offset = {}",
                buffer_index,
                c_str(accessor->component_type),
                c_str(accessor->type),
                accessor->count,
                accessor->offset,
                buffer_view->offset
            );

            const cgltf_size  index_stride            = 4; // always 32-bit unsigned integer
            const std::size_t index_buffer_byte_count = primitive->indices->count * index_stride;

            std::vector<uint32_t> read_indices(primitive->indices->count);
            const cgltf_size unpack_count = cgltf_accessor_unpack_indices(accessor, &read_indices[0], primitive->indices->count);
            if (unpack_count != primitive->indices->count) {
                log_gltf->error(
                    "cgltf_accessor_unpack_indices() failed: expected {}, got {}",
                    primitive->indices->count,
                    unpack_count
                );
                return;
            }

            std::vector<uint8_t> data(index_buffer_byte_count);
            memcpy(data.data(), read_indices.data(), data.size());

            erhe::primitive::Buffer_range index_range = m_context.buffer_sink.allocate_index_buffer(primitive->indices->count, index_stride);
            m_context.buffer_sink.enqueue_index_data(index_range.byte_offset, std::move(data));

            erhe_gl_primitive.index_buffer_range.byte_offset       = index_range.byte_offset;
            erhe_gl_primitive.index_buffer_range.count             = primitive->indices->count;
            erhe_gl_primitive.index_buffer_range.element_size      = index_stride;
            erhe_gl_primitive.triangle_fill_indices.primitive_type = to_erhe(primitive->type);
            erhe_gl_primitive.triangle_fill_indices.first_index    = 0;
            erhe_gl_primitive.triangle_fill_indices.index_count    = primitive->indices->count;
        }

        // Vertex buffer
        {
            const cgltf_size  vertex_count             = primitive->attributes[0].data->count;
            const std::size_t vertex_buffer_byte_count = vertex_count * m_context.vertex_format.stride();
            erhe::primitive::Buffer_range vertex_range = m_context.buffer_sink.allocate_vertex_buffer(vertex_count, m_context.vertex_format.stride());

            std::vector<uint8_t> data(vertex_buffer_byte_count);
            uint8_t* span_start = data.data();

            erhe_gl_primitive.vertex_buffer_range.byte_offset  = vertex_range.byte_offset;
            erhe_gl_primitive.vertex_buffer_range.count        = vertex_count;
            erhe_gl_primitive.vertex_buffer_range.element_size = m_context.vertex_format.stride();

            for (cgltf_size attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
                const cgltf_attribute* const attribute = &primitive->attributes[attribute_index];

                const auto erhe_usage_opt = to_erhe(attribute->type);
                if (!erhe_usage_opt.has_value()) {
                    continue;
                }

                const auto erhe_usage = erhe_usage_opt.value();
                const erhe::graphics::Vertex_attribute* erhe_attribute = m_context.vertex_format.find_attribute_maybe(
                    erhe_usage, attribute->index
                );
                if (erhe_attribute == nullptr) {
                    continue;
                }
        
                const cgltf_accessor* accessor = attribute->data;

                log_gltf->trace(
                    "Primitive attribute[{}]: name = {}, attribute type = {}[{}], "
                    "component type = {}, accessor type = {}, normalized = {}, count = {}, "
                    "stride = {}, accessor offset = {}",
                    attribute_index,
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

                switch (erhe_attribute->data_type.type) {
                    case igl::VertexAttributeFormat::float_: {
                        for (cgltf_size i = 0; i < accessor->count; ++i) {
                            cgltf_float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            cgltf_accessor_read_float(accessor, i, &value[0], component_count);
                            float* dest = reinterpret_cast<float*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                            for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                                dest[c] = value[c];
                            }
                            cgltf_accessor_read_float(
                                accessor,
                                i,
                                reinterpret_cast<float*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride()),
                                erhe_attribute->data_type.dimension
                            );
                        }
                        break;
                    }

                    case igl::VertexAttributeFormat::unsigned_byte: {
                        for (cgltf_size i = 0; i < accessor->count; ++i) {
                            cgltf_uint value[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                            uint8_t* dest = reinterpret_cast<uint8_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                            for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                                ERHE_VERIFY((value[c] & ~0xffu) == 0);
                                dest[c] = value[c] & 0xffu;
                            }
                        }
                        break;
                    }
                    case igl::VertexAttributeFormat::byte: {
                        for (cgltf_size i = 0; i < accessor->count; ++i) {
                            cgltf_uint value[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                            int8_t* dest = reinterpret_cast<int8_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                            for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                                ERHE_VERIFY((value[c] & ~0xffu) == 0);
                                dest[c] = value[c] & 0xffu;
                            }
                        }
                        break;
                    }
                    case igl::VertexAttributeFormat::unsigned_short: {
                        for (cgltf_size i = 0; i < accessor->count; ++i) {
                            cgltf_uint value[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                            uint16_t* dest = reinterpret_cast<uint16_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                            for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                                ERHE_VERIFY((value[c] & ~0xffffu) == 0);
                                dest[c] = value[c] & 0xffffu;
                            }
                        }
                        break;
                    }
                    case igl::VertexAttributeFormat::short_: {
                        for (cgltf_size i = 0; i < accessor->count; ++i) {
                            cgltf_uint value[4] = { 0, 0, 0, 0 };
                            cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                            int16_t* dest = reinterpret_cast<int16_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                            for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                                ERHE_VERIFY((value[c] & ~0xffffu) == 0);
                                dest[c] = value[c] & 0xffffu;
                            }
                        }
                        break;
                    }
                    default: {
                        // TODO
                        break;
                    }
                }
            }

            m_context.buffer_sink.enqueue_vertex_data(vertex_range.byte_offset, std::move(data));
        }

        const cgltf_size material_index = primitive->material - m_data->materials;
        erhe_mesh->add_primitive(
            erhe::primitive::Primitive{
                .material           = m_materials.at(material_index),
                .geometry_primitive = std::make_shared<erhe::primitive::Geometry_primitive>(
                    std::move(erhe_gl_primitive)
                )
            }
        );
    }
    void parse_skin(const cgltf_size skin_index)
    {
        const cgltf_skin* skin = &m_data->skins[skin_index];
        const std::string skin_name = safe_resource_name(skin->name, "skin", skin_index);
        log_gltf->info("Skin: skin index = {}, name = {}", skin_index, skin_name);

        auto erhe_skin = std::make_shared<erhe::scene::Skin>(skin_name);
        m_context.skins.push_back(erhe_skin);
        m_skins[skin_index] = erhe_skin;
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
                auto& erhe_joint_node = m_nodes.at(joint_node_index);
                ERHE_VERIFY(erhe_joint_node);
                erhe_skin->skin_data.joints[i] = erhe_joint_node;
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
                erhe_skin->skin_data.inverse_bind_matrices[i] = glm::mat4{1.0f};
            }
        }
        if (skin->skeleton != nullptr) {
            const cgltf_size skeleton_node_index = skin->skeleton - m_data->nodes;
            erhe_skin->skin_data.skeleton = m_nodes.at(skeleton_node_index);
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
        m_context.meshes.push_back(erhe_mesh);
        m_meshes[mesh_index] = erhe_mesh;
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

        erhe_mesh->layer_id = 0;
    }
    void parse_node(
        const cgltf_node*                  node,
        std::shared_ptr<erhe::scene::Node> erhe_parent
    )
    {
        const cgltf_size node_index = node - m_data->nodes;
        const std::string node_name = safe_resource_name(node->name, "node", node_index);
        log_gltf->trace("Node: node index = {}, name = {}", node_index, node_name);
        auto erhe_node = std::make_shared<erhe::scene::Node>(node_name);
        erhe_node->set_parent(erhe_parent);
        erhe_node->enable_flag_bits(
            Item_flags::content |
            Item_flags::visible |
            Item_flags::show_in_ui
        );
        m_nodes[node_index] = erhe_node;
        parse_node_transform(node, erhe_node);

        for (cgltf_size i = 0; i < node->children_count; ++i) {
            parse_node(node->children[i], erhe_node);
        }
    }
    void fix_pointers(const cgltf_size node_index)
    {
        const cgltf_node* node = &m_data->nodes[node_index];
        auto& erhe_node = m_nodes.at(node_index);
        for (cgltf_size i = 0; i < node->children_count; ++i) {
            cgltf_node* child_node       = node->children[i];
            cgltf_size  child_node_index = child_node - m_data->nodes;
            auto& erhe_child_node        = m_nodes.at(child_node_index);
            erhe_child_node->set_parent(erhe_node);
        }

        if (node->camera != nullptr) {
            const cgltf_size camera_index = node->camera - m_data->cameras;
            erhe_node->attach(m_cameras[camera_index]);
        }

        if (node->light != nullptr) {
            const cgltf_size light_index = node->light - m_data->lights;
            erhe_node->attach(m_lights[light_index]);
        }

        if (node->mesh != nullptr) {
            const cgltf_size mesh_index = node->mesh - m_data->meshes;
            if (node->skin != nullptr) {
                const cgltf_size skin_index = node->skin - m_data->skins;
                m_meshes[mesh_index]->skin = m_skins[skin_index];
            }
            erhe_node->attach(m_meshes[mesh_index]);
        }
    }

    Parse_context&                                          m_context;
    cgltf_data*                                             m_data{nullptr};
    std::shared_ptr<erhe::primitive::Material>              m_default_material;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>         m_meshes;
    std::vector<std::shared_ptr<erhe::scene::Animation>>    m_animations;
    std::vector<std::shared_ptr<erhe::scene::Camera>>       m_cameras;
    std::vector<std::shared_ptr<erhe::scene::Light>>        m_lights;
    std::vector<std::shared_ptr<erhe::scene::Skin>>         m_skins;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>   m_images;
    std::vector<std::shared_ptr<erhe::graphics::Sampler>>   m_samplers;
    std::vector<std::shared_ptr<erhe::scene::Node>>         m_nodes;
};

void parse_gltf(Parse_context& context)
{
    Gltf_parser parser{context};
    parser.parse_and_build();
}

} // namespace example
