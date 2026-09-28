// apps/android_boot: WP-18 bring-up program.
//
// Proves the Android toolchain end to end: SDL2 window + GLES 3.0 context,
// reads the original pak archives (through the TEMPORARY reader in
// pak_boot.h -- see that file's header comment), and draws a rotating
// triangle. Also builds and runs on desktop Linux as a quick check (reads
// the paks from $AS3D_DATA_ROOT/third_party_local/original/data/ instead of
// APK assets).
#include <SDL.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "as3d/core.h"
#include "pak_boot.h"

namespace {

#if !defined(__ANDROID__)
std::string dataRoot() {
    const char* env = std::getenv("AS3D_DATA_ROOT");
    if (env && *env) return env;
    return AS3D_REPO_ROOT;
}

std::string pakPath(int index) {
    return dataRoot() + "/third_party_local/original/data/pak" + std::to_string(index) + ".apk";
}
#else
const char* pakAssetName(int index) {
    static const char* names[3] = {"pak0.apk", "pak1.apk", "pak2.apk"};
    return names[index];
}
#endif

GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetShaderInfoLog(shader, sizeof(log), &n, log);
        AS3D_ERROR("shader compile failed: %.*s", (int)n, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aColor");
    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetProgramInfoLog(prog, sizeof(log), &n, log);
        AS3D_ERROR("program link failed: %.*s", (int)n, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

const char* kVertexShaderSrc =
    "#version 300 es\n"
    "in vec2 aPos;\n"
    "in vec3 aColor;\n"
    "uniform float uAngle;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    float c = cos(uAngle);\n"
    "    float s = sin(uAngle);\n"
    "    vec2 rotated = vec2(c * aPos.x - s * aPos.y, s * aPos.x + c * aPos.y);\n"
    "    gl_Position = vec4(rotated, 0.0, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

const char* kFragmentShaderSrc =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vec4(vColor, 1.0);\n"
    "}\n";

// Opens the three original pak archives, logs AS3D_BOOT_OK with the summed
// entry count, and returns the decrypted maps\levels.txt blob (from pak0)
// through `levelsTxt`, if found.
bool bootPaks(as3d::Blob& levelsTxt) {
    as3d_boot::PakArchive paks[3];
    std::size_t total = 0;
    bool allOpened = true;
    for (int i = 0; i < 3; ++i) {
#if defined(__ANDROID__)
        std::string path = pakAssetName(i);
#else
        std::string path = pakPath(i);
#endif
        if (!paks[i].open(path)) {
            AS3D_ERROR("bootPaks: failed to open pak%d ('%s')", i, path.c_str());
            allOpened = false;
            continue;
        }
        total += paks[i].fileCount();
    }
    if (!allOpened) return false;

    AS3D_INFO("AS3D_BOOT_OK files=%zu", total);

    if (paks[0].read(as3d::normalizePath("maps\\levels.txt"), levelsTxt)) {
        std::string mission = as3d_boot::firstMissionName(levelsTxt);
        AS3D_INFO("AS3D_FIRST_MISSION %s", mission.c_str());
    } else {
        AS3D_WARN("bootPaks: maps\\levels.txt not found in pak0");
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    long desktopFrameLimit = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            desktopFrameLimit = std::strtol(argv[i + 1], nullptr, 10);
            ++i;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        AS3D_ERROR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(
        "AS3D Boot", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        AS3D_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        AS3D_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    AS3D_INFO("GL_VENDOR: %s", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    AS3D_INFO("GL_RENDERER: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    AS3D_INFO("GL_VERSION: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    as3d::Blob levelsTxt;
    bootPaks(levelsTxt);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    GLuint program = (vs && fs) ? linkProgram(vs, fs) : 0;
    GLint uAngleLoc = program ? glGetUniformLocation(program, "uAngle") : -1;

    // Interleaved x,y, r,g,b for a single triangle.
    const float vertices[] = {
        0.0f, 0.6f, 1.0f, 0.2f, 0.2f,   // top, red
        -0.6f, -0.5f, 0.2f, 1.0f, 0.2f, // bottom-left, green
        0.6f, -0.5f, 0.2f, 0.2f, 1.0f,  // bottom-right, blue
    };

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                           (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    bool running = true;
    bool paused = false;
    Uint64 frame = 0;
    bool loggedFrameMilestone = false;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                case SDL_APP_TERMINATING:
                    running = false;
                    break;
                case SDL_APP_WILLENTERBACKGROUND:
                case SDL_APP_DIDENTERBACKGROUND:
                    if (!paused) AS3D_INFO("AS3D_PAUSE");
                    paused = true;
                    break;
                case SDL_APP_WILLENTERFOREGROUND:
                case SDL_APP_DIDENTERFOREGROUND:
                    if (paused) AS3D_INFO("AS3D_RESUME");
                    paused = false;
                    break;
                default:
                    break;
            }
        }

        if (paused) {
            SDL_Delay(16);
            continue;
        }

        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(window, &w, &h);
        if (w > 0 && h > 0) glViewport(0, 0, w, h);

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (program) {
            glUseProgram(program);
            float angle = static_cast<float>(frame) * 0.02f;
            if (uAngleLoc >= 0) glUniform1f(uAngleLoc, angle);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }

        SDL_GL_SwapWindow(window);
        ++frame;
        if (frame == 300 && !loggedFrameMilestone) {
            AS3D_INFO("AS3D_FRAME 300");
            loggedFrameMilestone = true;
        }

        if (desktopFrameLimit > 0 && static_cast<long>(frame) >= desktopFrameLimit) {
            running = false;
        }
    }

    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
