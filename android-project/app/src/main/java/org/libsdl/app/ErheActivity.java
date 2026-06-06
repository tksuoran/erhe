package org.libsdl.app;

/**
 * erhe-owned launch activity. This file is NOT part of the upstream SDL Java
 * shim: SDLActivity.java and the other org.libsdl.app classes in this
 * directory are kept byte-identical to upstream SDL (the erhe branch of
 * tksuoran/SDL) so scripts/update_sdl.py can sync them mechanically. erhe's
 * only customization to the shim lives here, in a subclass upstream
 * explicitly supports.
 */
public class ErheActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // erhe links SDL3 statically into libmain.so. Loading "SDL3" would
        // fail with "library libSDL3.so not found"; load "main" directly.
        // getMainSharedObject() picks the last entry as the SDL_main host,
        // which is what we want.
        return new String[] {
            "main"
        };
    }
}
