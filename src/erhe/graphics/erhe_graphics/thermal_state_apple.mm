#include "erhe_graphics/thermal_state.hpp"
#include "erhe_graphics/graphics_log.hpp"

#import <Foundation/Foundation.h>

#include <mutex>

namespace erhe::graphics {

auto get_thermal_state() -> Thermal_state
{
    switch ([[NSProcessInfo processInfo] thermalState]) {
        case NSProcessInfoThermalStateNominal:  return Thermal_state::nominal;
        case NSProcessInfoThermalStateFair:     return Thermal_state::fair;
        case NSProcessInfoThermalStateSerious:  return Thermal_state::serious;
        case NSProcessInfoThermalStateCritical: return Thermal_state::critical;
        default:                                return Thermal_state::unknown;
    }
}

void start_thermal_state_logging()
{
    static std::once_flag s_once;
    std::call_once(s_once, []() {
        log_startup->info("thermal state: {}", c_str(get_thermal_state()));
        // The observer lives for the process; the notification arrives on
        // the thread that changed the state, and the logger is thread-safe.
        [[NSNotificationCenter defaultCenter]
            addObserverForName: NSProcessInfoThermalStateDidChangeNotification
            object:             nil
            queue:              nil
            usingBlock:         ^(NSNotification*) {
                log_context->warn("thermal state changed: {}", c_str(get_thermal_state()));
            }
        ];
    });
}

} // namespace erhe::graphics
