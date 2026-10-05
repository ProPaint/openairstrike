package org.as3dport.game;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.provider.OpenableColumns;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The launcher entry of the app (docs/android.md, "Your own game files"). It starts the game
 * (GameActivity) at once when the app holds a game, bundled in the APK or imported earlier.
 * Otherwise, and when asked with the extra {@code import}, it shows the import screen: the
 * player picks the files of their own copy of a game (the paks, the executable for the menu
 * texts, Settings.xml and the logo; or a zip of the game folder), which are copied into
 * {@code <files>/games/incoming/} and handed to the native import
 * (apps/game/android_import.cpp, as3d::importGameFiles), which installs each game under
 * {@code <files>/games/<key>/}.
 *
 * It runs in its own process (":import") so that loading the native library here never
 * meets SDL, which only the game's process starts; after an import it ends a running game
 * process so that the game starts again and sees the new files.
 *
 * Intent extras (all optional):
 *   import (boolean)          show the import screen even when a game is there (to add a game)
 *   import_pending (boolean)  test hook: import the files already in files/games/incoming/
 *                             (put there with run-as), as if they had just been picked
 *   any other extra           passed on to GameActivity (game, bot, level, ...)
 */
public class ImportActivity extends Activity {
    private static final String TAG = "AS3D";
    private static final int PICK = 1;

    private static native String[] nativeListGames(String gamesDir, android.content.res.AssetManager assets);
    private static native String[] nativeImport(String incomingDir, String gamesDir);

    private static boolean sLoaded;
    private static boolean sLoadTried;

    private File gamesDir;
    private File incomingDir;
    private TextView gamesText;
    private TextView statusText;
    private Button chooseButton;
    private Button playButton;
    private boolean busy;
    private volatile boolean importedSomething;

    private static synchronized boolean loadNative() {
        if (!sLoadTried) {
            sLoadTried = true;
            try {
                System.loadLibrary("SDL2");
                System.loadLibrary("main");
                sLoaded = true;
            } catch (UnsatisfiedLinkError e) {
                Log.e(TAG, "ImportActivity: cannot load the native library: " + e);
            }
        }
        return sLoaded;
    }

    private String[] listGames() {
        if (!loadNative()) return new String[0];
        try {
            return nativeListGames(gamesDir.getAbsolutePath() + "/", getAssets());
        } catch (Throwable t) {
            Log.e(TAG, "nativeListGames: " + t);
            return new String[0];
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        gamesDir = new File(getFilesDir(), "games");
        incomingDir = new File(gamesDir, "incoming");
        Intent intent = getIntent();
        boolean ask = intent != null && intent.getBooleanExtra("import", false);
        boolean pending = intent != null && intent.getBooleanExtra("import_pending", false);
        String[] games = listGames();
        Log.i(TAG, "AS3D_IMPORT_GAMES available=" + keys(games) + " ask=" + (ask ? 1 : 0) + " pending=" + (pending ? 1 : 0));
        if (games.length > 0 && !ask && !pending && savedInstanceState == null) {
            startGame();
            return;
        }
        buildScreen();
        showGames(games);
        Log.i(TAG, "AS3D_IMPORT_SCREEN");
        if (pending && savedInstanceState == null) runImport(null);
    }

    private static String keys(String[] games) {
        StringBuilder b = new StringBuilder();
        for (String g : games) {
            if (b.length() > 0) b.append(',');
            int c = g.indexOf(':');
            b.append(c < 0 ? g : g.substring(0, c));
        }
        return b.length() == 0 ? "none" : b.toString();
    }

    // ---- the screen (no layout files, no support library)

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(LinearLayout parent, String s, float sp) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setPadding(0, dp(6), 0, dp(6));
        parent.addView(t);
        return t;
    }

    private void buildScreen() {
        ScrollView scroll = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(24), dp(24), dp(24), dp(24));
        scroll.addView(col);
        // targetSdk 35+ draws edge to edge: keep the text clear of the system bars and cutouts.
        scroll.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                int l, t, r, b;
                if (Build.VERSION.SDK_INT >= 30) {
                    android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                    l = i.left;
                    t = i.top;
                    r = i.right;
                    b = i.bottom;
                } else {
                    l = insets.getSystemWindowInsetLeft();
                    t = insets.getSystemWindowInsetTop();
                    r = insets.getSystemWindowInsetRight();
                    b = insets.getSystemWindowInsetBottom();
                }
                v.setPadding(l, t, r, b);
                return insets;
            }
        });

        TextView title = text(col, "AirStrike: your game files", 24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        text(col,
            "This app holds no game data. It plays AirStrike 3D, AirStrike 2 and AirStrike II: Gulf Thunder "
            + "(DivoGames, the Windows versions) from the files of your own copy. Copy the game folder to this "
            + "device, then choose its files:", 16);
        text(col,
            "• Required: the pak files from the game's data folder: pak0.apk, pak1.apk, pak2.apk "
            + "(Gulf Thunder also pak4.apk).\n"
            + "• Recommended: the game's executable (AirStrike3D.exe, AirStrike3D II.exe or "
            + "AirStrike3D II - Gulf.exe), for the menu texts: Information pages, ranks, dialogues. "
            + "Only the texts are kept.\n"
            + "• Optional: data/Settings.xml (intro pages, version line) and data/gfx/logo2s.tga "
            + "(the main menu's logo).\n"
            + "• Or a single zip of the game folder, or of the original download: every game in it "
            + "is imported.\n\n"
            + "Loose files: choose the files of one game at a time (the games' files have the same names). "
            + "You can come back here later to add another game.", 15);

        chooseButton = new Button(this);
        chooseButton.setText("Choose files");
        chooseButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                pickFiles();
            }
        });
        col.addView(chooseButton);

        statusText = text(col, "", 15);
        statusText.setTextIsSelectable(true);
        gamesText = text(col, "", 16);

        playButton = new Button(this);
        playButton.setText("Play");
        playButton.setVisibility(View.GONE);
        playButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startGame();
            }
        });
        col.addView(playButton);
        setContentView(scroll);
    }

    private void showGames(String[] games) {
        if (games.length == 0) {
            gamesText.setText("No game imported yet.");
            playButton.setVisibility(View.GONE);
            return;
        }
        StringBuilder b = new StringBuilder("Ready to play:");
        for (String g : games) {
            int c = g.indexOf(':');
            b.append("\n• ").append(c < 0 ? g : g.substring(c + 1));
        }
        gamesText.setText(b.toString());
        playButton.setVisibility(View.VISIBLE);
    }

    private void setBusy(boolean b) {
        busy = b;
        chooseButton.setEnabled(!b);
        playButton.setEnabled(!b);
    }

    // ---- picking and importing

    private void pickFiles() {
        if (busy) return;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            startActivityForResult(i, PICK);
        } catch (Exception e) {
            statusText.setText("No file picker on this device: " + e.getMessage());
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK || resultCode != RESULT_OK || data == null) return;
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); ++i) uris.add(clip.getItemAt(i).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (!uris.isEmpty()) runImport(uris);
    }

    private void status(final String s) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                statusText.setText(s);
            }
        });
    }

    // Copies the picked documents into incoming/ (null: use what is there), then imports.
    private void runImport(final List<Uri> uris) {
        setBusy(true);
        statusText.setText("Copying the files…");
        new Thread(new Runnable() {
            @Override
            public void run() {
                final StringBuilder report = new StringBuilder();
                String importedKeys = "";
                try {
                    if (uris != null) {
                        deleteTree(incomingDir);
                        if (!incomingDir.mkdirs() && !incomingDir.isDirectory()) throw new Exception("cannot create " + incomingDir);
                        copyAll(uris, report);
                    }
                    status("Recognising the files…");
                    if (!loadNative()) throw new Exception("the app's native library is missing");
                    String[] r = nativeImport(incomingDir.getAbsolutePath(), gamesDir.getAbsolutePath());
                    importedKeys = r.length > 0 ? r[0] : "";
                    for (int i = 1; i < r.length; ++i) report.append(r[i]).append('\n');
                    if (!importedKeys.isEmpty()) importedSomething = true;
                } catch (Throwable t) {
                    report.append("Import failed: ").append(t.getMessage()).append('\n');
                    Log.e(TAG, "import: " + t);
                } finally {
                    deleteTree(incomingDir);
                }
                Log.i(TAG, "AS3D_IMPORT_DONE imported=" + (importedKeys.isEmpty() ? "none" : importedKeys.replace(' ', ','))
                    + " notes=" + report.toString().trim().replace('\n', '|'));
                final String shown = report.toString().trim();
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        statusText.setText(shown);
                        showGames(listGames());
                        setBusy(false);
                    }
                });
            }
        }, "as3d-import").start();
    }

    private void copyAll(List<Uri> uris, StringBuilder report) throws Exception {
        Set<String> names = new HashSet<>();
        byte[] buf = new byte[1 << 16];
        int n = 0;
        for (Uri uri : uris) {
            ++n;
            String name = displayName(uri);
            if (names.contains(name.toLowerCase())) {
                report.append(name).append(": chosen twice (two games' files?), the second one is ignored. "
                    + "Choose one game's files at a time.\n");
                continue;
            }
            names.add(name.toLowerCase());
            status("Copying " + name + " (" + n + " of " + uris.size() + ")…");
            File out = new File(incomingDir, name);
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream os = new FileOutputStream(out)) {
                if (in == null) throw new Exception("cannot open " + name);
                int k;
                while ((k = in.read(buf)) > 0) os.write(buf, 0, k);
            }
        }
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception e) {
            Log.w(TAG, "display name of " + uri + ": " + e);
        }
        if (name == null) name = uri.getLastPathSegment();
        if (name == null) name = "file";
        name = name.replace('/', '_').replace('\\', '_');
        if (name.startsWith(".")) name = "_" + name;
        return name;
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        if (!f.delete()) Log.w(TAG, "cannot delete " + f);
    }

    // ---- the game

    private void startGame() {
        boolean restarted = false;
        if (importedSomething) {
            // A game that is running read its list of games when it started: end it, so the
            // new files are seen. Its progress was saved when it went to the background.
            ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            List<ActivityManager.RunningAppProcessInfo> procs = am != null ? am.getRunningAppProcesses() : null;
            if (procs != null) {
                for (ActivityManager.RunningAppProcessInfo p : procs) {
                    if (p.processName.equals(getPackageName()) && p.pid != Process.myPid()) {
                        Log.i(TAG, "AS3D_IMPORT_RESTART pid=" + p.pid);
                        Process.killProcess(p.pid);
                        restarted = true;
                    }
                }
            }
        }
        if (restarted) {
            // Give the system a moment to notice that the old process is gone.
            setBusy(true);
            new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
                @Override
                public void run() {
                    launchGame();
                }
            }, 500);
        } else {
            launchGame();
        }
    }

    private void launchGame() {
        Intent game = new Intent(this, GameActivity.class);
        Intent from = getIntent();
        if (from != null && from.getExtras() != null) {
            game.putExtras(from.getExtras());
            game.removeExtra("import");
            game.removeExtra("import_pending");
        }
        Log.i(TAG, "AS3D_IMPORT_START_GAME");
        startActivity(game);
        finish();
    }
}
