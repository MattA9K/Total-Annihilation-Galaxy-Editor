#include "ui/GlassBlur.hpp"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GLFW/glfw3.h>
#endif

#include <cstdio>
#include <vector>

#if !defined(__APPLE__)

// Windows and Linux headers only declare OpenGL 1.1, so the few newer calls
// this effect needs are looked up from the driver once.
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {

typedef unsigned int (APIENTRY* CreateShaderFn)(unsigned int);
typedef void (APIENTRY* ShaderSourceFn)(unsigned int, int, const char* const*, const int*);
typedef void (APIENTRY* CompileShaderFn)(unsigned int);
typedef void (APIENTRY* GetShaderivFn)(unsigned int, unsigned int, int*);
typedef void (APIENTRY* GetInfoLogFn)(unsigned int, int, int*, char*);
typedef unsigned int (APIENTRY* CreateProgramFn)();
typedef void (APIENTRY* AttachShaderFn)(unsigned int, unsigned int);
typedef void (APIENTRY* LinkProgramFn)(unsigned int);
typedef void (APIENTRY* GetProgramivFn)(unsigned int, unsigned int, int*);
typedef void (APIENTRY* DeleteShaderFn)(unsigned int);
typedef void (APIENTRY* UseProgramFn)(unsigned int);
typedef int (APIENTRY* GetUniformLocationFn)(unsigned int, const char*);
typedef void (APIENTRY* Uniform1iFn)(int, int);
typedef void (APIENTRY* Uniform1fFn)(int, float);
typedef void (APIENTRY* Uniform2fFn)(int, float, float);
typedef void (APIENTRY* Uniform3fFn)(int, float, float, float);
typedef void (APIENTRY* GenArraysFn)(int, unsigned int*);
typedef void (APIENTRY* BindVertexArrayFn)(unsigned int);
typedef void (APIENTRY* BindFramebufferFn)(unsigned int, unsigned int);
typedef void (APIENTRY* FramebufferTexture2DFn)(unsigned int, unsigned int, unsigned int, unsigned int, int);
typedef unsigned int (APIENTRY* CheckFramebufferStatusFn)(unsigned int);
typedef void (APIENTRY* ActiveTextureFn)(unsigned int);

CreateShaderFn pCreateShader;
ShaderSourceFn pShaderSource;
CompileShaderFn pCompileShader;
GetShaderivFn pGetShaderiv;
GetInfoLogFn pGetShaderInfoLog;
CreateProgramFn pCreateProgram;
AttachShaderFn pAttachShader;
LinkProgramFn pLinkProgram;
GetProgramivFn pGetProgramiv;
GetInfoLogFn pGetProgramInfoLog;
DeleteShaderFn pDeleteShader;
UseProgramFn pUseProgram;
GetUniformLocationFn pGetUniformLocation;
Uniform1iFn pUniform1i;
Uniform1fFn pUniform1f;
Uniform2fFn pUniform2f;
Uniform3fFn pUniform3f;
GenArraysFn pGenVertexArrays;
BindVertexArrayFn pBindVertexArray;
GenArraysFn pGenFramebuffers;
BindFramebufferFn pBindFramebuffer;
FramebufferTexture2DFn pFramebufferTexture2D;
CheckFramebufferStatusFn pCheckFramebufferStatus;
ActiveTextureFn pActiveTexture;

template <typename Fn>
bool load(Fn& target, const char* name) {
    target = reinterpret_cast<Fn>(glfwGetProcAddress(name));
    return target != nullptr;
}

bool loadOpenGl() {
    static int state = 0;  // 0 untried, 1 loaded, -1 unavailable
    if (state == 0) {
        const bool ok = load(pCreateShader, "glCreateShader") && load(pShaderSource, "glShaderSource")
            && load(pCompileShader, "glCompileShader") && load(pGetShaderiv, "glGetShaderiv")
            && load(pGetShaderInfoLog, "glGetShaderInfoLog") && load(pCreateProgram, "glCreateProgram")
            && load(pAttachShader, "glAttachShader") && load(pLinkProgram, "glLinkProgram")
            && load(pGetProgramiv, "glGetProgramiv") && load(pGetProgramInfoLog, "glGetProgramInfoLog")
            && load(pDeleteShader, "glDeleteShader") && load(pUseProgram, "glUseProgram")
            && load(pGetUniformLocation, "glGetUniformLocation") && load(pUniform1i, "glUniform1i")
            && load(pUniform1f, "glUniform1f") && load(pUniform2f, "glUniform2f") && load(pUniform3f, "glUniform3f")
            && load(pGenVertexArrays, "glGenVertexArrays") && load(pBindVertexArray, "glBindVertexArray")
            && load(pGenFramebuffers, "glGenFramebuffers") && load(pBindFramebuffer, "glBindFramebuffer")
            && load(pFramebufferTexture2D, "glFramebufferTexture2D")
            && load(pCheckFramebufferStatus, "glCheckFramebufferStatus") && load(pActiveTexture, "glActiveTexture");
        state = ok ? 1 : -1;
    }
    return state == 1;
}

}  // namespace

#define glCreateShader pCreateShader
#define glShaderSource pShaderSource
#define glCompileShader pCompileShader
#define glGetShaderiv pGetShaderiv
#define glGetShaderInfoLog pGetShaderInfoLog
#define glCreateProgram pCreateProgram
#define glAttachShader pAttachShader
#define glLinkProgram pLinkProgram
#define glGetProgramiv pGetProgramiv
#define glGetProgramInfoLog pGetProgramInfoLog
#define glDeleteShader pDeleteShader
#define glUseProgram pUseProgram
#define glGetUniformLocation pGetUniformLocation
#define glUniform1i pUniform1i
#define glUniform1f pUniform1f
#define glUniform2f pUniform2f
#define glUniform3f pUniform3f
#define glGenVertexArrays pGenVertexArrays
#define glBindVertexArray pBindVertexArray
#define glGenFramebuffers pGenFramebuffers
#define glBindFramebuffer pBindFramebuffer
#define glFramebufferTexture2D pFramebufferTexture2D
#define glCheckFramebufferStatus pCheckFramebufferStatus
#define glActiveTexture pActiveTexture

#else
namespace {
bool loadOpenGl() {
    return true;
}
}  // namespace
#endif

namespace galaxy {
namespace {

constexpr const char* kVertexShader = R"(#version 150
out vec2 uv;
void main() {
    // One triangle that covers the whole target, with no vertex data.
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 150
uniform sampler2D uImage;
uniform vec2 uStep;
uniform float uSigma;
uniform float uComposite;
uniform vec3 uBackground;
uniform float uFinish;
uniform float uWhiteness;
uniform vec3 uTint;
uniform float uSaturation;
in vec2 uv;
out vec4 fragColor;

vec3 fetch(vec2 at) {
    vec4 c = texture(uImage, at);
    // Where the map is missing the picture is transparent: show the canvas color.
    return mix(c.rgb, mix(uBackground, c.rgb, c.a), uComposite);
}

void main() {
    vec3 sum = vec3(0.0);
    float total = 0.0;
    float twoSigmaSquared = 2.0 * uSigma * uSigma;
    for (int i = -10; i <= 10; ++i) {
        float weight = exp(-float(i * i) / twoSigmaSquared);
        sum += fetch(uv + uStep * float(i)) * weight;
        total += weight;
    }
    vec3 rgb = sum / total;
    if (uFinish > 0.5) {
        float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb = mix(vec3(luma), rgb, uSaturation);
        rgb = mix(rgb, uTint, uWhiteness);
    }
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "Glass shader did not compile: %s\n", log);
        return 0;
    }
    return shader;
}

void makeTexture(GLuint& texture) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Puts back whatever the rest of the program had selected.
struct SavedState {
    GLint viewport[4] = {};
    GLint framebuffer = 0;
    GLint program = 0;
    GLint vertexArray = 0;
    GLint activeTexture = 0;
    GLint texture = 0;
    GLboolean blend = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean depth = GL_FALSE;
    GLboolean cull = GL_FALSE;

    SavedState() {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        depth = glIsEnabled(GL_DEPTH_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
    }

    ~SavedState() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vertexArray));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        auto restore = [](GLenum capability, GLboolean was) {
            if (was != GL_FALSE) {
                glEnable(capability);
            } else {
                glDisable(capability);
            }
        };
        restore(GL_BLEND, blend);
        restore(GL_SCISSOR_TEST, scissor);
        restore(GL_DEPTH_TEST, depth);
        restore(GL_CULL_FACE, cull);
    }
};

}  // namespace

bool GlassBlur::initialize() {
    if (!loadOpenGl()) {
        return false;
    }
    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) {
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        char log[1024] = {};
        glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "Glass shader did not link: %s\n", log);
        return false;
    }
    imageUniform_ = glGetUniformLocation(program_, "uImage");
    directionUniform_ = glGetUniformLocation(program_, "uStep");
    sigmaUniform_ = glGetUniformLocation(program_, "uSigma");
    compositeUniform_ = glGetUniformLocation(program_, "uComposite");
    backgroundUniform_ = glGetUniformLocation(program_, "uBackground");
    finishUniform_ = glGetUniformLocation(program_, "uFinish");
    whitenessUniform_ = glGetUniformLocation(program_, "uWhiteness");
    tintUniform_ = glGetUniformLocation(program_, "uTint");
    saturationUniform_ = glGetUniformLocation(program_, "uSaturation");

    glGenVertexArrays(1, &vertexArray_);
    glGenFramebuffers(1, &framebuffer_);
    makeTexture(sourceTexture_);
    makeTexture(pingTexture_);
    makeTexture(finalTexture_);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool GlassBlur::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return true;
    }
    for (const GLuint texture : {pingTexture_, finalTexture_}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, sourceTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    width_ = width;
    height_ = height;
    return true;
}

bool GlassBlur::update(const std::uint8_t* rgba, int width, int height, const Settings& settings) {
    if (failed_ || width < 2 || height < 2) {
        return false;
    }
    // SavedState's constructor/destructor call glActiveTexture, glBindFramebuffer,
    // glUseProgram and glBindVertexArray. Outside macOS those are function pointers
    // filled in by loadOpenGl(), so they must be loaded before SavedState exists.
    if (!loadOpenGl()) {
        failed_ = true;
        return false;
    }
    SavedState saved;
    if (!ready_) {
        if (!initialize()) {
            failed_ = true;
            return false;
        }
        ready_ = true;
    }
    resize(width, height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, width, height);
    glUseProgram(program_);
    glBindVertexArray(vertexArray_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glUniform1i(imageUniform_, 0);
    glUniform1f(sigmaUniform_, settings.sigma);
    glUniform3f(backgroundUniform_, settings.backgroundRed, settings.backgroundGreen, settings.backgroundBlue);
    glUniform1f(whitenessUniform_, settings.whiteness);
    glUniform3f(tintUniform_, settings.tintRed, settings.tintGreen, settings.tintBlue);
    glUniform1f(saturationUniform_, settings.saturation);

    // Blur sideways, then up and down, and do it twice for a smooth, even frost.
    // The last pass also adds the white.
    struct Pass {
        GLuint from;
        GLuint to;
        bool horizontal;
        bool first;
        bool last;
    };
    const Pass passes[] = {
        {sourceTexture_, pingTexture_, true, true, false},
        {pingTexture_, finalTexture_, false, false, false},
        {finalTexture_, pingTexture_, true, false, false},
        {pingTexture_, finalTexture_, false, false, true},
    };
    bool complete = true;
    for (const Pass& pass : passes) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pass.to, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            complete = false;
            break;
        }
        glBindTexture(GL_TEXTURE_2D, pass.from);
        glUniform2f(directionUniform_, pass.horizontal ? 1.0f / static_cast<float>(width) : 0.0f, pass.horizontal ? 0.0f : 1.0f / static_cast<float>(height));
        glUniform1f(compositeUniform_, pass.first ? 1.0f : 0.0f);
        glUniform1f(finishUniform_, pass.last ? 1.0f : 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    if (!complete) {
        failed_ = true;
    }
    return complete;
}

}  // namespace galaxy
