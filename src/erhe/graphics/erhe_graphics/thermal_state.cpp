#include "erhe_graphics/thermal_state.hpp"

namespace erhe::graphics {

auto c_str(const Thermal_state value) -> const char*
{
    switch (value) {
        case Thermal_state::unknown:  return "unknown";
        case Thermal_state::nominal:  return "nominal";
        case Thermal_state::fair:     return "fair";
        case Thermal_state::serious:  return "serious";
        case Thermal_state::critical: return "critical";
        default:                      return "?";
    }
}

#if !defined(__APPLE__)
auto get_thermal_state() -> Thermal_state
{
    return Thermal_state::unknown;
}

void start_thermal_state_logging()
{
}
#endif

} // namespace erhe::graphics
