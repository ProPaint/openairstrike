// The native half of ImportActivity (android/app/src/main/java/org/as3dport/game/ImportActivity.java):
// which games the app can play (bundled in the APK's assets, or imported into the app's
// files under games/<key>/), and the import of the files the player picked
// (as3d::importGameFiles, as3d/game_import.h). ImportActivity runs in its own process
// (":import") and loads this library without starting SDL: nothing here may use SDL.
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/game_import.h"
#include "as3d/game_profile.h"

namespace {

std::string str(JNIEnv* env, jstring s) {
    if (!s) return std::string();
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

// Java strings must be valid modified UTF-8: the notes are ASCII but file names may not be.
jstring jstr(JNIEnv* env, const std::string& s) {
    std::string clean;
    for (unsigned char c : s) clean += c < 0x80 ? static_cast<char>(c) : '?';
    return env->NewStringUTF(clean.c_str());
}

jobjectArray toArray(JNIEnv* env, const std::vector<std::string>& v) {
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray a = env->NewObjectArray(static_cast<jsize>(v.size()), cls, nullptr);
    for (size_t i = 0; i < v.size(); ++i) {
        jstring s = jstr(env, v[i]);
        env->SetObjectArrayElement(a, static_cast<jsize>(i), s);
        env->DeleteLocalRef(s);
    }
    return a;
}

bool inAssets(AAssetManager* am, const as3d::GameProfile& g) {
    if (!am) return false;
    const std::string path = std::string(g.key) + "/" + g.paks[0];
    AAsset* a = AAssetManager_open(am, path.c_str(), AASSET_MODE_UNKNOWN);
    if (!a) return false;
    AAsset_close(a);
    return true;
}

} // namespace

// "key:title" of every playable game the app holds, bundled or imported, in GameId order.
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_as3dport_game_ImportActivity_nativeListGames(JNIEnv* env, jclass,
                                                                                                 jstring gamesDir,
                                                                                                 jobject assets) {
    AAssetManager* am = assets ? AAssetManager_fromJava(env, assets) : nullptr;
    const std::string dir = str(env, gamesDir);
    std::vector<std::string> out;
    for (int i = 0; i < as3d::kGameCount; ++i) {
        const as3d::GameProfile& g = as3d::gameProfile(static_cast<as3d::GameId>(i));
        if (!as3d::gameIsPlayable(g)) continue;
        if (inAssets(am, g) || as3d::importedGameComplete(dir, g)) out.push_back(std::string(g.key) + ":" + g.title);
    }
    return toArray(env, out);
}

// {imported keys separated by spaces, note, note, ...}
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_as3dport_game_ImportActivity_nativeImport(JNIEnv* env, jclass,
                                                                                              jstring incomingDir,
                                                                                              jstring gamesDir) {
    const as3d::ImportResult r = as3d::importGameFiles(str(env, incomingDir), str(env, gamesDir));
    std::vector<std::string> out;
    std::string keys;
    for (const std::string& k : r.imported) keys += (keys.empty() ? "" : " ") + k;
    out.push_back(keys);
    out.insert(out.end(), r.notes.begin(), r.notes.end());
    return toArray(env, out);
}
