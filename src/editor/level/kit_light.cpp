#include "level/kit_light.hpp"
#include "level/kit_shape.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace editor {

auto c_str(const Kit_light_type type) -> const char*
{
    switch (type) {
        case Kit_light_type::point: return "point";
        case Kit_light_type::spot:  return "spot";
        default:                    return "?";
    }
}

void get_shape_light_frames(const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames)
{
    out_frames.clear();
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape != nullptr) && shape->get_light_frames) {
        shape->get_light_frames(values, out_frames);
    }
}

namespace {

[[nodiscard]] auto is_finite(const glm::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // anonymous namespace

auto check_light_frames(const std::span<const Kit_light_frame> frames) -> std::string
{
    if (frames.size() > c_max_light_frames) {
        return fmt::format("{} light frames, at most {}", frames.size(), c_max_light_frames);
    }
    for (std::size_t i = 0, end = frames.size(); i < end; ++i) {
        const Kit_light_frame& frame = frames[i];
        const std::string_view name{frame.name};
        if (name.empty()) {
            return fmt::format("light frame {} has no name", i);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (std::string_view{frames[j].name} == name) {
                return fmt::format("light frame name '{}' is repeated", name);
            }
        }
        if (!is_finite(frame.position) || !is_finite(frame.direction) || !is_finite(frame.start.color)) {
            return fmt::format("light frame '{}' has a value that is not finite", name);
        }
        if (std::abs(glm::length(frame.direction) - 1.0f) > 1.0e-4f) {
            return fmt::format("light frame '{}': the direction is not unit length", name);
        }
        const Kit_light_start& start = frame.start;
        if (!(start.range > 0.0f) || !(start.intensity > 0.0f) || !std::isfinite(start.range) || !std::isfinite(start.intensity)) {
            return fmt::format("light frame '{}': the range and the intensity must be positive", name);
        }
        if ((start.color.r < 0.0f) || (start.color.g < 0.0f) || (start.color.b < 0.0f)) {
            return fmt::format("light frame '{}': a negative color", name);
        }
        if (start.type == Kit_light_type::spot) {
            if (!(start.outer_spot_angle > 0.0f) || (start.inner_spot_angle < 0.0f) || (start.inner_spot_angle > start.outer_spot_angle) ||
                (start.outer_spot_angle >= glm::pi<float>())) {
                return fmt::format("light frame '{}': a spot needs 0 <= inner <= outer < pi radians and outer > 0", name);
            }
        }
    }
    return {};
}

auto find_light_frame(const std::span<const Kit_light_frame> frames, const std::string_view name) -> const Kit_light_frame*
{
    for (const Kit_light_frame& frame : frames) {
        if (std::string_view{frame.name} == name) {
            return &frame;
        }
    }
    return nullptr;
}

auto get_light_frame_rotation(const glm::vec3& direction) -> glm::quat
{
    const glm::vec3 z    = -glm::normalize(direction);
    const glm::vec3 hint = (std::abs(z.y) > 0.999f) ? glm::vec3{0.0f, 0.0f, -1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 x    = glm::normalize(glm::cross(hint, z));
    const glm::vec3 y    = glm::cross(z, x);
    return glm::normalize(glm::quat_cast(glm::mat3{x, y, z}));
}

auto is_at_light_frame(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale, const Kit_light_frame& frame) -> bool
{
    constexpr float tolerance = 1.0e-5f;
    const glm::quat frame_rotation = get_light_frame_rotation(frame.direction);
    return
        (glm::length(translation - frame.position) <= tolerance) &&
        (std::abs(glm::dot(glm::normalize(rotation), frame_rotation)) >= (1.0f - tolerance)) &&
        (glm::length(scale - glm::vec3{1.0f}) <= tolerance);
}

void match_light_frames(const std::span<const Kit_light_frame> frames, const std::span<const std::string> node_frames, Kit_light_match& out_match)
{
    out_match.frame_nodes.assign(frames.size(), c_no_light_node);
    out_match.removed_nodes.clear();
    out_match.released_nodes.clear();
    for (std::size_t node = 0, node_end = node_frames.size(); node < node_end; ++node) {
        std::size_t stated = c_no_light_node;
        for (std::size_t frame = 0, frame_end = frames.size(); frame < frame_end; ++frame) {
            if (node_frames[node] == frames[frame].name) {
                stated = frame;
                break;
            }
        }
        if (stated == c_no_light_node) {
            out_match.removed_nodes.push_back(node);
        } else if (out_match.frame_nodes[stated] == c_no_light_node) {
            out_match.frame_nodes[stated] = node;
        } else {
            out_match.released_nodes.push_back(node);
        }
    }
}

} // namespace editor
