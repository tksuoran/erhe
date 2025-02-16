#include "frame_controller.hpp"
#include "example_log.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/input_axis.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace example {

using glm::mat4;
using glm::vec3;
using glm::vec4;

Frame_controller::Frame_controller()
    : Node_attachment{"frame controller"}
    , rotate_x{"rotate_x"}
    , rotate_y{"rotate_y"}
    , rotate_z{"rotate_z"}
    , translate_x{"translate_x"}
    , translate_y{"translate_y"}
    , translate_z{"translate_z"}
    , speed_modifier{"speed_modifier"}

{
    reset();
    rotate_x      .disable_power_base();
    rotate_y      .disable_power_base();
    rotate_z      .disable_power_base();
    translate_x   .set_power_base(10.0f);
    translate_y   .set_power_base(10.0f);
    translate_z   .set_power_base(10.0f);
    speed_modifier.set_power_base(10.0f);
    update_transform();
}

auto Frame_controller::get_controller(const Control control) -> erhe::math::Input_axis&
{
    switch (control) {
        case Control::translate_x: return translate_x;
        case Control::translate_y: return translate_y;
        case Control::translate_z: return translate_z;
        case Control::rotate_x   : return rotate_x;
        case Control::rotate_y   : return rotate_y;
        case Control::rotate_z   : return rotate_z;
        default: {
            ERHE_FATAL("bad control %04x", static_cast<unsigned int>(control));
        }
    }
}

void Frame_controller::set_position(const vec3 position)
{
    m_position = position;
    update_transform();
}

void Frame_controller::set_elevation(const float value)
{
    m_elevation = value;
    update_transform();
}

void Frame_controller::set_heading(const float value)
{
    m_heading = value;
    m_heading_matrix = erhe::math::create_rotation(m_heading, erhe::math::vector_types<float>::vec3_unit_y());
    update_transform();
}

auto Frame_controller::get_position() const -> vec3
{
    return m_position;
}

auto Frame_controller::get_elevation() const -> float
{
    return m_elevation;
}

auto Frame_controller::get_heading() const -> float
{
    return m_heading;
}

auto Frame_controller::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::frame_controller;
}

auto Frame_controller::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Frame_controller::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Frame_controller::get_transform_from_node(erhe::scene::Node* node)
{
    if (node == nullptr) {
        return;
    }
    const vec4 position  = node->position_in_world();
    const vec4 direction = node->direction_in_world();

    m_position = position;
    float heading  {0.0f};
    float elevation{0.0f};
    erhe::math::cartesian_to_heading_elevation(direction, elevation, heading);
    m_elevation = elevation;
    m_heading   = heading;

    m_heading_matrix = erhe::math::create_rotation(m_heading, erhe::math::vector_types<float>::vec3_unit_y());
}

void Frame_controller::handle_node_update(erhe::scene::Node* old_node, erhe::scene::Node* new_node)
{
    static_cast<void>(old_node);
    if (new_node == nullptr) {
        return;
    }
    get_transform_from_node(new_node);
}

void Frame_controller::handle_node_transform_update()
{
    if (m_transform_update) {
        return;
    }

    auto* node = get_node();
    if (node == nullptr) {
        return;
    }
    get_transform_from_node(node);
    update_transform();
}

void Frame_controller::reset()
{
    translate_x.reset();
    translate_y.reset();
    translate_z.reset();
    rotate_x.reset();
    rotate_y.reset();
    rotate_z.reset();
}

void Frame_controller::update_transform()
{
    auto* node = get_node();
    if (node == nullptr) {
        return;
    }

    const mat4 elevation_matrix = erhe::math::create_rotation(m_elevation, erhe::math::vector_types<float>::vec3_unit_x());
    m_rotation_matrix = m_heading_matrix * elevation_matrix;

    mat4 parent_from_local = m_rotation_matrix;

    // Put translation to column 3
    parent_from_local[3] = vec4{m_position, 1.0f};

    m_transform_update = true;
    node->set_parent_from_node(parent_from_local);
    m_transform_update = false;
}

auto Frame_controller::get_axis_x() const -> vec3
{
    return vec3{m_rotation_matrix[0]};
    //return vec3{m_heading_matrix[0]};
}

auto Frame_controller::get_axis_y() const -> vec3
{
    return vec3{m_rotation_matrix[1]};
    //return vec3{m_heading_matrix[1]};
}

auto Frame_controller::get_axis_z() const -> vec3
{
    return vec3{m_rotation_matrix[2]};
    //return vec3{m_heading_matrix[2]};
}

void Frame_controller::on_frame_begin()
{
    translate_x   .on_frame_begin();
    translate_y   .on_frame_begin();
    translate_z   .on_frame_begin();
    rotate_x      .on_frame_begin();
    rotate_y      .on_frame_begin();
    rotate_z      .on_frame_begin();
    speed_modifier.on_frame_begin();
}

void Frame_controller::on_frame_end()
{
    translate_x   .on_frame_end();
    translate_y   .on_frame_end();
    translate_z   .on_frame_end();
    rotate_x      .on_frame_end();
    rotate_y      .on_frame_end();
    rotate_z      .on_frame_end();
    speed_modifier.on_frame_end();
}

void Frame_controller::tick(std::chrono::steady_clock::time_point timestamp)
{
    translate_x.tick(timestamp);
    translate_y.tick(timestamp);
    translate_z.tick(timestamp);
    rotate_x.tick(timestamp);
    rotate_y.tick(timestamp);
    rotate_z.tick(timestamp);
    speed_modifier.tick(timestamp);

    const float speed_scale = 10.0f + 10.0f * speed_modifier.get_value();

    if (translate_x.get_tick_distance() != 0.0f) {
        m_position += get_axis_x() * translate_x.get_tick_distance() * speed_scale;
    }

    if (translate_y.get_tick_distance() != 0.0f) {
        m_position += get_axis_y() * translate_y.get_tick_distance() * speed_scale;
    }

    if (translate_z.get_tick_distance() != 0.0f) {
        m_position += get_axis_z() * translate_z.get_tick_distance() * speed_scale;
    }

    if ((rotate_x.get_tick_distance() != 0.0f) || (rotate_y.get_tick_distance() != 0.0f)) {
        m_heading += rotate_y.get_tick_distance();
        m_elevation += rotate_x.get_tick_distance();
        const mat4 elevation_matrix = erhe::math::create_rotation(m_elevation, erhe::math::vector_types<float>::vec3_unit_x());
        m_heading_matrix = erhe::math::create_rotation(m_heading, erhe::math::vector_types<float>::vec3_unit_y());
        m_rotation_matrix = m_heading_matrix * elevation_matrix;
    }

    update_transform();
}

auto is_frame_controller(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    using namespace erhe::bit;
    return test_all_rhs_bits_set(item->get_type(), erhe::Item_type::frame_controller);
}

auto is_frame_controller(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_frame_controller(item.get());
}

auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>
{
    for (const auto& attachment : node->get_attachments()) {
        const auto frame_controller = std::dynamic_pointer_cast<Frame_controller>(attachment);
        if (frame_controller) {
            return frame_controller;
        }
    }
    return {};
}

} // namespace example
