package org.as3dport.game;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.KeyEvent;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;

import org.libsdl.app.SDLActivity;

/**
 * The AirStrike 3D game activity. Everything runs in native code (apps/game, entry point
 * apps/game/android_main.cpp); this class names the native libraries, turns the launching
 * intent's extras into program arguments, lays the surface out under display cutouts and
 * reports the cutout insets so the touch controls stay clear of them.
 *
 * Intent extras (all optional), e.g.
 *   adb shell am start -n org.as3dport.game/.GameActivity --ez bot true --ei level 2
 *   bot (boolean)      the scripted test pilot plays (as3d_game --bot)
 *   level (int)        first mission, 1..20 (default 1)
 *   frames (int)       quit after this many simulation frames
 *   difficulty (int)   0..4
 *   no_audio (boolean)
 *   rebuild_on_resume (boolean)  test hook: rebuild every GL resource after a resume
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "AS3D";

    private static native void nativeSetSafeInsets(int left, int top, int right, int bottom);

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        Intent intent = getIntent();
        if (intent != null) {
            if (intent.getBooleanExtra("bot", false)) args.add("--bot");
            if (intent.getBooleanExtra("no_audio", false)) args.add("--no-audio");
            if (intent.getBooleanExtra("rebuild_on_resume", false)) args.add("--rebuild-on-resume");
            addInt(intent, "level", "--level", args);
            addInt(intent, "frames", "--frames", args);
            addInt(intent, "difficulty", "--difficulty", args);
        }
        return args.toArray(new String[0]);
    }

    private static void addInt(Intent intent, String extra, String flag, List<String> args) {
        if (!intent.hasExtra(extra)) return;
        int v = intent.getIntExtra(extra, Integer.MIN_VALUE);
        if (v == Integer.MIN_VALUE) {
            // `am start --es level 2` passes a string.
            String s = intent.getStringExtra(extra);
            if (s == null) return;
            try {
                v = Integer.parseInt(s.trim());
            } catch (NumberFormatException e) {
                return;
            }
        }
        args.add(flag);
        args.add(Integer.toString(v));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Draw under a cutout on the short edges (landscape: left or right); the native
        // side keeps the controls out of the reported insets.
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) return;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // With targetSdk 36 the system handles Back through OnBackInvokedDispatcher
        // (predictive back): no KEYCODE_BACK or onBackPressed reaches the app. Forward it to
        // SDL as the Back key, which pauses the game.
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT, new OnBackInvokedCallback() {
                    @Override
                    public void onBackInvoked() {
                        Log.i(TAG, "AS3D_BACK");
                        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
                        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
                    }
                });
        }
        View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                reportInsets(insets);
                return v.onApplyWindowInsets(insets);
            }
        });
    }

    private static void reportInsets(WindowInsets insets) {
        int l = 0, t = 0, r = 0, b = 0;
        if (Build.VERSION.SDK_INT >= 28 && insets != null) {
            DisplayCutout c = insets.getDisplayCutout();
            if (c != null) {
                l = c.getSafeInsetLeft();
                t = c.getSafeInsetTop();
                r = c.getSafeInsetRight();
                b = c.getSafeInsetBottom();
            }
        }
        Log.i(TAG, "AS3D_INSETS left=" + l + " top=" + t + " right=" + r + " bottom=" + b);
        try {
            nativeSetSafeInsets(l, t, r, b);
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "nativeSetSafeInsets unavailable: " + e);
        }
    }
}
