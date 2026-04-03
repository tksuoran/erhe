#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"

#import <Foundation/Foundation.h>

namespace erhe::graphics {

static Device* s_device = nullptr;

static void metal_exception_handler(NSException* exception)
{
    NSString* name   = [exception name];
    NSString* reason = [exception reason];

    std::string name_str   = name   ? [name   UTF8String] : "unknown";
    std::string reason_str = reason ? [reason UTF8String] : "unknown";

    std::string error_message = name_str + ": " + reason_str;

    log_startup->error("Metal validation error: {}", error_message);

    if (s_device != nullptr) {
        s_device->device_error(error_message);
    }
}

void install_metal_error_handler(Device& device)
{
    s_device = &device;
    NSSetUncaughtExceptionHandler(&metal_exception_handler);
}

} // namespace erhe::graphics
