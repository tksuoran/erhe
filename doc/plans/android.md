# Android: full editor on a phone

Status: proposed

Extends `doc/android.md`, which describes the Android build and the launch
path that exist. This plan is Android phase 3: everything between "the editor
renders and survives backgrounding" and "the editor is usable on a phone".

- **Touch input mapping.** SDL3 already provides synthetic mouse events via
  `SDL_HINT_TOUCH_MOUSE_EVENTS=1`, which is enough to click; real multi-touch
  gestures (pan, pinch zoom, two-finger orbit) are the work. The rendergraph
  and physics paths are backend-agnostic and need no changes.
- **Soft keyboard.** Text entry in ImGui fields has no way to raise the
  on-screen keyboard.
- **UI density and DPI scaling.** The desktop layout is unusable at phone
  pixel density.
- **Android Vulkan driver shader issues.** Resolve whatever the mobile drivers
  reject or miscompile once the full editor renders.
- **Package id.** The applicationId is still the `org.libsdl.app` placeholder
  inherited from the SDL template.
