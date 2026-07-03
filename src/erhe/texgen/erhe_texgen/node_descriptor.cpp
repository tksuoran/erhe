#include "erhe_texgen/node_descriptor.hpp"

namespace erhe::texgen {

namespace {

[[nodiscard]] auto contains_seed_reference(const std::string& text) -> bool
{
    return
        (text.find("$seed")   != std::string::npos) ||
        (text.find("$(seed)") != std::string::npos) ||
        (text.find("$rnd(")   != std::string::npos) ||
        (text.find("$rndi(")  != std::string::npos);
}

} // anonymous namespace

auto Node_descriptor::uses_seed() const -> bool
{
    if (contains_seed_reference(code)) {
        return true;
    }
    for (const Output_descriptor& output : outputs) {
        if (contains_seed_reference(output.expression)) {
            return true;
        }
    }
    for (const Input_descriptor& input : inputs) {
        if (contains_seed_reference(input.default_expression)) {
            return true;
        }
    }
    // Any enum value's code fragment may be selected at compose time, so a
    // seed reference living only inside one of them still requires the
    // per-node seed uniform.
    for (const Parameter_descriptor& parameter : parameters) {
        for (const Enum_value& enum_value : parameter.enum_values) {
            if (contains_seed_reference(enum_value.code)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace erhe::texgen
