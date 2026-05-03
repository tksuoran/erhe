#include "erhe_graphics/vulkan/vulkan_metal_layer_mac.hpp"

#if defined(ERHE_OS_MACOS) && defined(ERHE_GRAPHICS_API_VULKAN)

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL.h>

namespace erhe::graphics {

namespace {

CAMetalLayer* find_metal_layer(NSView* view)
{
    if (view == nil) {
        return nil;
    }
    if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
        return (CAMetalLayer*)view.layer;
    }
    for (NSView* sub in view.subviews) {
        CAMetalLayer* found = find_metal_layer(sub);
        if (found != nil) {
            return found;
        }
    }
    return nil;
}

CAMetalLayer* metal_layer_for_sdl_window(void* sdl_window_opaque)
{
    SDL_Window* sdl_window = static_cast<SDL_Window*>(sdl_window_opaque);
    if (sdl_window == nullptr) {
        return nil;
    }
    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
    if (props == 0) {
        return nil;
    }
    NSWindow* ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr
    );
    if (ns_window == nil) {
        return nil;
    }
    return find_metal_layer(ns_window.contentView);
}

} // anonymous namespace

bool set_sdl_window_drawable_size(void* sdl_window_opaque, int width, int height)
{
    CAMetalLayer* layer = metal_layer_for_sdl_window(sdl_window_opaque);
    if (layer == nil) {
        return false;
    }
    layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
    return true;
}

} // namespace erhe::graphics

#endif
