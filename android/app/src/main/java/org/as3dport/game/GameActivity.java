package org.as3dport.game;

import org.libsdl.app.SDLActivity;

/**
 * WP-18 Android bring-up activity. All engine/rendering/pak-reading logic
 * lives in native code (apps/android_boot); this class only needs to exist so
 * SDLActivity has a concrete launcher activity to bind the "main" native
 * library to (see SDLActivity.getLibraries()/getMainSharedObject()).
 */
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main"
        };
    }
}
