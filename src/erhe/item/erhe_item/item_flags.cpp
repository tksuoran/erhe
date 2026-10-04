#include "erhe_item/item_flags.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>

namespace erhe {

namespace {

// The registered application flags (register_application_flags); the span
// refers to storage the application owns for the process.
std::span<const Item_flag_info> s_application_flags{};
uint64_t                        s_application_transient_bits{0};
uint64_t                        s_application_purpose_guide_bits{0};

} // anonymous namespace

void Item_flags::register_application_flags(
    const std::span<const Item_flag_info> flags,
    const uint64_t                        transient_bits,
    const uint64_t                        purpose_guide_when_set_bits
)
{
    uint64_t seen = 0u;
    for (const Item_flag_info& flag : flags) {
        ERHE_VERIFY((flag.bit & application_mask) == flag.bit); // one bit, in the application range
        ERHE_VERIFY((flag.bit & (flag.bit - 1u)) == 0u);
        ERHE_VERIFY((seen & flag.bit) == 0u);
        ERHE_VERIFY(flag.label != nullptr);
        seen |= flag.bit;
    }
    ERHE_VERIFY((transient_bits & ~seen) == 0u);
    ERHE_VERIFY((purpose_guide_when_set_bits & ~seen) == 0u);
    s_application_flags              = flags;
    s_application_transient_bits     = transient_bits;
    s_application_purpose_guide_bits = purpose_guide_when_set_bits;
}

auto Item_flags::get_application_flags() -> std::span<const Item_flag_info>
{
    return s_application_flags;
}

auto Item_flags::label(const uint64_t bit_position) -> const char*
{
    if (bit_position < count) {
        return c_bit_labels[bit_position];
    }
    const uint64_t bit = uint64_t{1} << bit_position;
    for (const Item_flag_info& flag : s_application_flags) {
        if (flag.bit == bit) {
            return flag.label;
        }
    }
    return nullptr;
}

auto Item_flags::get_transient_bits() -> uint64_t
{
    return transient | s_application_transient_bits;
}

auto Item_flags::get_purpose_guide_when_set_bits() -> uint64_t
{
    return s_application_purpose_guide_bits;
}

auto Item_flags::get_purpose_inputs() -> uint64_t
{
    return s_application_purpose_guide_bits | purpose_guide_when_clear;
}

auto Item_flags::to_string(const uint64_t flags) -> std::string
{
    std::stringstream ss;

    bool first = true;
    for (uint64_t bit_position = 0; bit_position < 64; ++bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        const bool     value    = erhe::utility::test_bit_set(flags, bit_mask);
        if (value) {
            if (!first) {
                ss << " | ";
            }
            const char* bit_label = label(bit_position);
            if (bit_label != nullptr) {
                ss << bit_label;
            } else {
                ss << "bit " << bit_position;
            }
            first = false;
        }
    }
    return ss.str();
}

auto Item_filter::operator()(const uint64_t visibility_mask) const -> bool
{
    if ((visibility_mask & require_all_bits_set) != require_all_bits_set) {
        return false;
    }
    if (require_at_least_one_bit_set != 0u) {
        if ((visibility_mask & require_at_least_one_bit_set) == 0u) {
            return false;
        }
    }
    if ((visibility_mask & require_all_bits_clear) != 0u) {
        return false;
    }
    if (require_at_least_one_bit_clear != 0u) {
        if ((visibility_mask & require_at_least_one_bit_clear) == require_at_least_one_bit_clear) {
            return false;
        }
    }
    return true;
}

auto Item_filter::describe() const -> std::string
{
    bool first = true;
    std::stringstream ss;
    if (require_all_bits_set != 0) {
        ss << "require_all_bits_set = " << Item_flags::to_string(this->require_all_bits_set);
        first = false;
    }
    if (require_at_least_one_bit_set != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_at_least_one_bit_set = " << Item_flags::to_string(this->require_at_least_one_bit_set);
        first = false;
    }
    if (require_all_bits_clear != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_all_bits_clear = " << Item_flags::to_string(this->require_all_bits_clear);
        first = false;
    }
    if (require_at_least_one_bit_clear != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_at_least_one_bit_clear = " << Item_flags::to_string(this->require_at_least_one_bit_clear);
    }
    return ss.str();
}

} // namespace erhe
