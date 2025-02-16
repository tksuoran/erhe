#include "scene/frame_controller.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/input_axis.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace editor {

using glm::mat4;
using glm::vec3;
using glm::vec4;

Frame_controller::Frame_controller()
    : Item          {"frame controller"}
    , rotate_x      {"Rotate X"}
    , rotate_y      {"Rotate Y"}
    , rotate_z      {"Rotate Z"}
    , translate_x   {"Translate X"}
    , translate_y   {"Translate Y"}
    , translate_z   {"Translate Z"}
    , speed_modifier{"Speed modifier"}

{
    reset();
    rotate_x      .disable_power_base();
    rotate_y      .disable_power_base();
    rotate_z      .disable_power_base();
    translate_x   .set_power_base(4.0f);
    translate_y   .set_power_base(4.0f);
    translate_z   .set_power_base(4.0f);
    speed_modifier.set_power_base(4.0f);
    update();
}

Frame_controller::Frame_controller(const Frame_controller&) = default;
Frame_controller& Frame_controller::operator=(const Frame_controller&) = default;
Frame_controller::~Frame_controller() noexcept = default;

auto Frame_controller::clone() const -> std::shared_ptr<erhe::Item_base>
{
    // It doesn't make sense copy Frame_controller - does it?
    return std::shared_ptr<erhe::Item_base>{};
}

auto Frame_controller::get_variable(const Variable control) -> erhe::math::Input_axis&
{
    switch (control) {
        case Variable::translate_x: return translate_x;
        case Variable::translate_y: return translate_y;
        case Variable::translate_z: return translate_z;
        case Variable::rotate_x   : return rotate_x;
        case Variable::rotate_y   : return rotate_y;
        case Variable::rotate_z   : return rotate_z;
        default: {
            ERHE_FATAL("bad control %04x", static_cast<unsigned int>(control));
        }
    }
}

void Frame_controller::set_position(const vec3 position)
{
    m_position = position;
    update();
}

void Frame_controller::set_orientation(const glm::mat4& orientation)
{
    m_orientation = orientation;
    update();
}

auto Frame_controller::get_position() const -> vec3
{
    return m_position;
}

auto Frame_controller::get_orientation() const -> glm::mat4
{
    return m_orientation;
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
    const erhe::scene::Trs_transform& transform = node->world_from_node_transform();
    m_position = transform.get_translation();
    m_orientation = glm::toMat4(transform.get_rotation());
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
    update();
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

void Frame_controller::update()
{
    auto* node = get_node();
    if (node == nullptr) {
        return;
    }

    m_transform_update = true;
    node->set_world_from_node(erhe::scene::Trs_transform{m_position, m_orientation});
    m_transform_update = false;
}

auto Frame_controller::get_axis_x() const -> vec3
{
    return vec3{m_orientation[0]};
}

auto Frame_controller::get_axis_y() const -> vec3
{
    return vec3{m_orientation[1]};
}

auto Frame_controller::get_axis_z() const -> vec3
{
    return vec3{m_orientation[2]};
}

void Frame_controller::tick(std::chrono::steady_clock::time_point timestamp)
{
    get_transform_from_node(get_node());

    translate_x   .tick(timestamp);
    translate_y   .tick(timestamp);
    translate_z   .tick(timestamp);
    rotate_x      .tick(timestamp);
    rotate_y      .tick(timestamp);
    rotate_z      .tick(timestamp);
    speed_modifier.tick(timestamp);

    const float speed = move_speed + speed_modifier.get_velocity();

    if (translate_x.get_tick_distance() != 0.0f) {
        m_position += get_axis_x() * translate_x.get_tick_distance() * speed;
    }

    if (translate_y.get_tick_distance() != 0.0f) {
        m_position += get_axis_y() * translate_y.get_tick_distance() * speed;
    }

    if (translate_z.get_tick_distance() != 0.0f) {
        m_position += get_axis_z() * translate_z.get_tick_distance() * speed;
    }

    apply_rotation(rotate_x.get_tick_distance(), rotate_y.get_tick_distance(), 0.0f);

    update();
}

void Frame_controller::apply_rotation(float rx, float ry, float rz)
{
    glm::mat4 new_orientation = m_orientation;
    if (rx != 0.0f) {
        glm::mat4 rotate = erhe::math::create_rotation<float>(rx, get_axis_x());
        new_orientation = rotate * new_orientation;
    }
    if (ry != 0.0f) {
        glm::mat4 rotate = erhe::math::create_rotation<float>(ry, glm::vec3{0.0f, 1.0f, 0.0f}); //get_axis_y());
        new_orientation = rotate * new_orientation;
    }
    if (rz != 0.0f) {
        glm::mat4 rotate = erhe::math::create_rotation<float>(rz, get_axis_z());
        new_orientation = rotate * new_orientation;
    }
    m_orientation = new_orientation;
    update();
}

void Frame_controller::apply_tumble(glm::vec3 pivot, float rx, float ry, float rz)
{
    glm::mat4 new_orientation = m_orientation;
    if (rx != 0.0f) {
        glm::mat4 rotate = erhe::math::create_rotation<float>(rx, get_axis_x());
        new_orientation = rotate * new_orientation;
    }
    if (ry != 0.0f) {
        glm::mat4 rotate = erhe::math::create_rotation<float>(ry, glm::vec3{0.0f, 1.0f, 0.0f}); //get_axis_y());
        new_orientation = rotate * new_orientation;
    }
    if (rz != 0.0f) {
        glm::mat4 rotate = erhe::math::create_rotation<float>(rz, get_axis_z());
        new_orientation = rotate * new_orientation;
    }
    {
        glm::mat4 old_world_from_view = m_orientation;
        glm::mat4 old_view_from_world = glm::transpose(old_world_from_view);
        glm::mat4 new_world_from_view = new_orientation;
        glm::vec3 direction_in_world  = m_position - pivot;
        glm::vec4 direction_in_view   = old_view_from_world * glm::vec4{direction_in_world, 0.0f};
        m_position = pivot + glm::vec3{new_world_from_view * direction_in_view};
    }
    m_orientation = new_orientation;
    update();
}

auto is_frame_controller(const erhe::Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(item->get_type(), erhe::Item_type::frame_controller);
}

auto is_frame_controller(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_frame_controller(item.get());
}

auto as_frame_controller(erhe::Item_base* item) -> Frame_controller*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!erhe::bit::test_all_rhs_bits_set(item->get_type(),erhe::Item_type::frame_controller)) {
        return nullptr;
    }
    return static_cast<Frame_controller*>(item);
}

auto get_frame_controller(const erhe::scene::Node* node) -> std::shared_ptr<Frame_controller>
{
    for (const auto& attachment : node->get_attachments()) {
        auto frame_controller = std::dynamic_pointer_cast<Frame_controller>(attachment);
        if (frame_controller) {
            return frame_controller;
        }
    }
    return {};
}

} // namespace editor
