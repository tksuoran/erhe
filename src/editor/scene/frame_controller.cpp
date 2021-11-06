#include "scene/frame_controller.hpp"
#include "scene/controller.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "log.hpp"

//#define GLM_SWIZZLE_XYZW
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace editor
{

using namespace glm;

using namespace erhe::toolkit;

void Frame_controller::set_position(const vec3 position)
{
    m_position = position;
    update();
}

void Frame_controller::set_elevation(const float value)
{
    m_elevation = value;
    update();
}

void Frame_controller::set_heading(const float value)
{
    m_heading = value;
    m_heading_matrix = create_rotation(m_heading, vec3_unit_y);
    update();
}

vec3 Frame_controller::position() const
{
    return m_position;
}

float Frame_controller::elevation() const
{
    return m_elevation;
}

float Frame_controller::heading() const
{
    return m_heading;
}


Frame_controller::Frame_controller()
{
    clear();
    rotate_x      .set_damp     (0.700f);
    rotate_y      .set_damp     (0.700f);
    rotate_z      .set_damp     (0.700f);
    rotate_x      .set_max_delta(0.02f);
    rotate_y      .set_max_delta(0.02f);
    rotate_z      .set_max_delta(0.02f);
    translate_x   .set_damp     (0.92f);
    translate_y   .set_damp     (0.92f);
    translate_z   .set_damp     (0.92f);
    translate_x   .set_max_delta(0.004f);
    translate_y   .set_max_delta(0.004f);
    translate_z   .set_max_delta(0.004f);
    speed_modifier.set_max_value(3.0f);
    speed_modifier.set_damp     (0.92f);
    speed_modifier.set_max_delta(0.5f);

    update();
}

void Frame_controller::set_frame(erhe::scene::Node* node)
{
    m_node = node;

    if (!m_node)
    {
        return;
    }

    // node->world_from_node() is not necessarily valid, so make sure it is.
    //node->update_transform();

    const vec4 position  = node->position_in_world();
    const vec4 direction = node->direction_in_world();

    m_position = position;
    float heading{0.0f};
    float elevation{0.0f};
    cartesian_to_heading_elevation(direction, elevation, heading);
    m_elevation = elevation;
    m_heading   = heading;

    m_heading_matrix = create_rotation(m_heading, vec3_unit_y);

    update();
}

auto Frame_controller::node() -> erhe::scene::Node*
{
    return m_node;
}

void Frame_controller::clear()
{
    translate_x.clear();
    translate_y.clear();
    translate_z.clear();
    rotate_x.clear();
    rotate_y.clear();
    rotate_z.clear();
}

void Frame_controller::update()
{
    if (m_node == nullptr)
    {
        return;
    }

    const mat4 elevation_matrix = create_rotation(m_elevation, vec3_unit_x);
    m_rotation_matrix = m_heading_matrix * elevation_matrix;

    mat4 parent_from_local = m_rotation_matrix;
    //mat4 parent_to_local = transpose(local_to_parent);

    // HACK
    // if (m_position.y < 0.03f)
    // {
    //     m_position.y = 0.03f;
    // }

    // Put translation to column 3
    parent_from_local[3] = vec4{m_position, 1.0f};

    // Put inverse translation to column 3
    /*parentToLocal._03 = parentToLocal._00 * -positionInParent.X + parentToLocal._01 * -positionInParent.Y + parentToLocal._02 * - positionInParent.Z;
   parentToLocal._13 = parentToLocal._10 * -positionInParent.X + parentToLocal._11 * -positionInParent.Y + parentToLocal._12 * - positionInParent.Z;
   parentToLocal._23 = parentToLocal._20 * -positionInParent.X + parentToLocal._21 * -positionInParent.Y + parentToLocal._22 * - positionInParent.Z;
   parentToLocal._33 = 1.0f;
   */
    //m_local_from_parent = inverse(m_parent_from_local);
    m_node->set_parent_from_node(parent_from_local);

    //vec4 position  = m_node->world_from_local.matrix() * vec4{0.0f, 0.0f, 0.0f, 1.0f};
    //vec4 direction = m_node->world_from_local.matrix() * vec4{0.0f, 0.0f, 1.0f, 0.0f};
    //float elevation;
    //float heading;
    //cartesian_to_heading_elevation(direction, elevation, heading);
    //log_render.info("elevation = {:.2f}, heading = {:.2f}\n", elevation / glm::pi<float>(), heading / glm::pi<float>());

    //Frame.LocalToParent.Set(localToParent, parentToLocal);
}

auto Frame_controller::right() const -> vec3
{
    return vec3{m_heading_matrix[0].x, m_heading_matrix[0].y, m_heading_matrix[0].z};
}

auto Frame_controller::up() const -> vec3
{
    return vec3{m_heading_matrix[1].x, m_heading_matrix[1].y, m_heading_matrix[1].z};
}

auto Frame_controller::back() const -> vec3
{
    return vec3{m_heading_matrix[2].x, m_heading_matrix[2].y, m_heading_matrix[2].z};
}

void Frame_controller::update_fixed_step()
{
    translate_x.update();
    translate_y.update();
    translate_z.update();
    rotate_x.update();
    rotate_y.update();
    rotate_z.update();
    speed_modifier.update();

    const float speed = 0.8f + speed_modifier.current_value();

    if (translate_x.current_value() != 0.0f)
    {
        m_position += right() * translate_x.current_value() * speed;
    }

    if (translate_y.current_value() != 0.0f)
    {
        m_position += up() * translate_y.current_value() * speed;
    }

    if (translate_z.current_value() != 0.0f)
    {
        m_position += back() * translate_z.current_value() * speed;
    }

    if ((rotate_x.current_value() != 0.0f) ||
        (rotate_y.current_value() != 0.0f))
    {
        m_heading += rotate_y.current_value();
        m_elevation += rotate_x.current_value();
        mat4 elevation_matrix = create_rotation(m_elevation, vec3_unit_x);
        m_heading_matrix = create_rotation(m_heading, vec3_unit_y);
        m_rotation_matrix = m_heading_matrix * elevation_matrix;
    }

    update();
#if 0
   m_parent_from_local = m_rotation_matrix;
   //Matrix4 parentToLocal;

   //Matrix4.Transpose(localToParent, out parentToLocal);

   // HACK
   if (m_position.y < 0.03f)
      m_position.y = 0.03f;

   /*  Put translation to column 3  */ 
   m_parent_from_local[3] = vec4{m_position, 1.0f};

#    if 0
   localToParent._03 = positionInParent.X;
   localToParent._13 = positionInParent.Y;
   localToParent._23 = positionInParent.Z;
   localToParent._33 = 1.0f;

   /*  Put inverse translation to column 3 */ 
   parentToLocal._03 = parentToLocal._00 * -positionInParent.X + parentToLocal._01 * -positionInParent.Y + parentToLocal._02 * - positionInParent.Z;
   parentToLocal._13 = parentToLocal._10 * -positionInParent.X + parentToLocal._11 * -positionInParent.Y + parentToLocal._12 * - positionInParent.Z;
   parentToLocal._23 = parentToLocal._20 * -positionInParent.X + parentToLocal._21 * -positionInParent.Y + parentToLocal._22 * - positionInParent.Z;
   parentToLocal._33 = 1.0f;

   Frame.LocalToParent.Set(localToParent, parentToLocal);
#    endif

   m_local_from_parent = inverse(m_parent_from_local);
#endif
}

} // namespace editor
