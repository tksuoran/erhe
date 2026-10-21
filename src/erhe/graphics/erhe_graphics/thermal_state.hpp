#pragma once

namespace erhe::graphics {

// The operating system's thermal state of the machine (Apple:
// NSProcessInfo.thermalState). A machine past nominal throttles CPU and GPU
// clocks, which moves frame costs and the frame pacer's measurements; the
// frame pacing tools report it next to their results. unknown where the
// platform has no such state.
enum class Thermal_state : unsigned int {
    unknown = 0,
    nominal,
    fair,
    serious,
    critical
};

[[nodiscard]] auto c_str(Thermal_state value) -> const char*;
[[nodiscard]] auto get_thermal_state() -> Thermal_state;

// Logs the current thermal state and, from then on, every change (Apple:
// NSProcessInfoThermalStateDidChangeNotification). Called once at device
// init; later calls do nothing.
void start_thermal_state_logging();

} // namespace erhe::graphics
