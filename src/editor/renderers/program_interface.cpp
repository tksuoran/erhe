#include "renderers/program_interface.hpp"
#include "renderers/mesh_memory.hpp"
#include "editor_log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/window.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::graphics
{
class Vertex_attribute;
}

namespace editor {

using erhe::graphics::Vertex_attribute;

Program_interface* g_program_interface{nullptr};

Program_interface::Program_interface()
    : erhe::components::Component{c_type_name}
{
}

Program_interface::~Program_interface() noexcept
{
}

void Program_interface::deinitialize_component()
{
    ERHE_VERIFY(g_program_interface == this);
    shader_resources.reset();
    g_program_interface = nullptr;
}

void Program_interface::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Window>(); // ensures we have graphics::Instance info

    // TODO Make erhe::graphics::Instance a component
}

Program_interface::Shader_resources::Shader_resources(
    std::size_t max_material_count,
    std::size_t max_light_count,
    std::size_t max_camera_count,
    std::size_t max_primitive_count,
    std::size_t max_joint_count
)
    : fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = gl::Fragment_shader_output_type::float_vec4,
            .location = 0
        }
    }
    , attribute_mappings{
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_position_texcoord",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::position | Vertex_attribute::Usage_type::tex_coord
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 0,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_position",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::position
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 1,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal",
            .src_usage       =
            {
                .type = Vertex_attribute::Usage_type::normal
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 2,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal_flat",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::normal,
                .index = 1
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 3,
            .shader_type     = gl::Attribute_type::float_vec3,
            .name            = "a_normal_smooth",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::normal,
                .index = 2
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 4,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_tangent",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::tangent,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 5,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_bitangent",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::bitangent,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 6,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_color",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::color,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 7,
            .shader_type     = gl::Attribute_type::float_vec2,
            .name            = "a_texcoord",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::tex_coord,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 8,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_id",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::id,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 9,
            .shader_type     = gl::Attribute_type::unsigned_int_vec4,
            .name            = "a_joints",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::joint_indices,
            }
        },
        erhe::graphics::Vertex_attribute_mapping{
            .layout_location = 10,
            .shader_type     = gl::Attribute_type::float_vec4,
            .name            = "a_weights",
            .src_usage       =
            {
                .type  = Vertex_attribute::Usage_type::joint_weights,
            }
        }
    }
    , camera_interface   {max_camera_count   }
    , light_interface    {max_light_count    }
    , material_interface {max_material_count }
    , primitive_interface{max_primitive_count}
    , joint_interface    {max_joint_count    }
{
}

void Program_interface::initialize_component()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(g_program_interface == nullptr);

    auto ini = erhe::application::get_ini("erhe.ini", "renderer");
    ini->get("max_material_count",  config.max_material_count );
    ini->get("max_light_count",     config.max_light_count    );
    ini->get("max_camera_count",    config.max_camera_count   );
    ini->get("max_primitive_count", config.max_primitive_count);
    ini->get("max_draw_count",      config.max_draw_count     );
    ini->get("max_joint_count",     config.max_joint_count    );

    shader_resources = std::make_unique<Shader_resources>(
        config.max_material_count,
        config.max_light_count,
        config.max_camera_count,
        config.max_primitive_count,
        config.max_joint_count
    );

    g_program_interface = this;
}

} // namespace editor
