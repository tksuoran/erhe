#include "transform/channel_locks.hpp"

#include "erhe_item/item.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace editor {

auto apply_channel_locks(
    const erhe::scene::Trs_transform& before,
    const erhe::scene::Trs_transform& after,
    const uint64_t                    lock_flags
) -> erhe::scene::Trs_transform
{
    using namespace erhe::utility;
    using Item_flags = erhe::Item_flags;
    const uint64_t locks = lock_flags & Item_flags::lock_channel_mask;
    if (locks == 0) {
        return after;
    }
    erhe::scene::Trs_transform result = after;
    if ((locks & Item_flags::lock_translation_mask) != 0) {
        glm::vec3       translation        = result.get_translation();
        const glm::vec3 before_translation = before.get_translation();
        if (test_bit_set(locks, Item_flags::lock_translation_x)) { translation.x = before_translation.x; }
        if (test_bit_set(locks, Item_flags::lock_translation_y)) { translation.y = before_translation.y; }
        if (test_bit_set(locks, Item_flags::lock_translation_z)) { translation.z = before_translation.z; }
        result.set_translation(translation);
    }
    if ((locks & Item_flags::lock_rotation_mask) == Item_flags::lock_rotation_mask) {
        // Every axis locked: the rotation stays exactly (no Euler round trip).
        result.set_rotation(before.get_rotation());
    } else if ((locks & Item_flags::lock_rotation_mask) != 0) {
        // Any 3-D rotation has two Euler XYZ representations: (x, y, z) and
        // the alternate branch (x + pi, pi - y, z + pi). glm::eulerAngles
        // returns whichever is canonical, which can jump branches as the
        // rotation crosses ~90 degrees - masking components across two
        // independent decompositions would then snap the node to a wildly
        // wrong orientation. Pick the branch of the NEW rotation closest to
        // the reference decomposition before masking, so the locked
        // component compares within one consistent branch.
        const auto wrap_angle = [](float angle) -> float {
            while (angle >  glm::pi<float>()) { angle -= glm::two_pi<float>(); }
            while (angle < -glm::pi<float>()) { angle += glm::two_pi<float>(); }
            return angle;
        };
        const glm::vec3 before_euler = glm::eulerAngles(before.get_rotation());
        glm::vec3       euler        = glm::eulerAngles(result.get_rotation());
        const glm::vec3 alternate{
            wrap_angle(euler.x + glm::pi<float>()),
            wrap_angle(glm::pi<float>() - euler.y),
            wrap_angle(euler.z + glm::pi<float>())
        };
        const auto branch_distance = [&](const glm::vec3& e) -> float {
            return
                std::abs(wrap_angle(e.x - before_euler.x)) +
                std::abs(wrap_angle(e.y - before_euler.y)) +
                std::abs(wrap_angle(e.z - before_euler.z));
        };
        if (branch_distance(alternate) < branch_distance(euler)) {
            euler = alternate;
        }
        if (test_bit_set(locks, Item_flags::lock_rotation_x)) { euler.x = before_euler.x; }
        if (test_bit_set(locks, Item_flags::lock_rotation_y)) { euler.y = before_euler.y; }
        if (test_bit_set(locks, Item_flags::lock_rotation_z)) { euler.z = before_euler.z; }
        result.set_rotation(glm::quat{euler});
    }
    if ((locks & Item_flags::lock_scale_mask) != 0) {
        glm::vec3       scale        = result.get_scale();
        const glm::vec3 before_scale = before.get_scale();
        if (test_bit_set(locks, Item_flags::lock_scale_x)) { scale.x = before_scale.x; }
        if (test_bit_set(locks, Item_flags::lock_scale_y)) { scale.y = before_scale.y; }
        if (test_bit_set(locks, Item_flags::lock_scale_z)) { scale.z = before_scale.z; }
        result.set_scale(scale);
    }
    return result;
}

} // namespace editor
