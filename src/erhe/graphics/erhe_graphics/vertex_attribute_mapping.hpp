#pragma once

#include "erhe_graphics/vertex_attribute.hpp"

#include <string_view>

namespace erhe::graphics
{

class Vertex_attribute_mapping
{
public:
    uint32_t                     layout_location{0};
    Glsl_attribute_type          shader_type    {Glsl_attribute_type::float_};
    std::string_view             name           {};
    Vertex_attribute::Usage      src_usage      {};
    Vertex_attribute::Usage_type dst_usage_type {Vertex_attribute::Usage_type::automatic};

    [[nodiscard]] static auto a_position_float_vec2() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = Glsl_attribute_type::float_vec2,
            .name            = "a_position",
            .src_usage =
            {
                .type        = Vertex_attribute::Usage_type::position
            }
        };
    }
    [[nodiscard]] static auto a_position_float_vec3() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = Glsl_attribute_type::float_vec3,
            .name            = "a_position",
            .src_usage =
            {
                .type        = Vertex_attribute::Usage_type::position
            }
        };
    }
    [[nodiscard]] static auto a_position_float_vec4() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = Glsl_attribute_type::float_vec4,
            .name            = "a_position",
            .src_usage =
            {
                .type        = Vertex_attribute::Usage_type::position
            }
        };
    }
    [[nodiscard]] static auto a_position0_float_vec4() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = Glsl_attribute_type::float_vec4,
            .name            = "a_position0",
            .src_usage =
            {
                .type        = Vertex_attribute::Usage_type::position,
                .index       = 0
            }
        };
    }
    [[nodiscard]] static auto a_position1_float_vec4() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = Glsl_attribute_type::float_vec4,
            .name            = "a_position1",
            .src_usage =
            {
                .type        = Vertex_attribute::Usage_type::position,
                .index       = 1
            }
        };
    }
    [[nodiscard]] static auto a_color_float_vec4() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 1,
            .shader_type     = Glsl_attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage =
            {
                .type        = Vertex_attribute::Usage_type::color
            }
        };
    }
    [[nodiscard]] static auto a_texcoord_float_vec2() -> Vertex_attribute_mapping
    {
        return Vertex_attribute_mapping{
            .layout_location = 2,
            .shader_type     = Glsl_attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage =
            {
                .type        = erhe::graphics::Vertex_attribute::Usage_type::tex_coord
            }
        };
    }

};

} // namespace erhe::graphics
