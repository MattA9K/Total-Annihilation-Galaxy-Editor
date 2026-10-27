#include "ui/Terrain3D.hpp"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

#if !defined(__APPLE__)

// Windows and Linux headers only declare OpenGL 1.1, so the newer calls the 3D
// view needs are looked up from the driver once.
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_R8 0x8229
#define GL_RED 0x1903
#define GL_RGBA8 0x8058
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_SRC_ALPHA 0x80CB
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
typedef void (APIENTRY* DeleteObjectFn)(unsigned int);
typedef void (APIENTRY* UseProgramFn)(unsigned int);
typedef int (APIENTRY* GetUniformLocationFn)(unsigned int, const char*);
typedef void (APIENTRY* Uniform1iFn)(int, int);
typedef void (APIENTRY* Uniform1fFn)(int, float);
typedef void (APIENTRY* Uniform2iFn)(int, int, int);
typedef void (APIENTRY* Uniform2fFn)(int, float, float);
typedef void (APIENTRY* Uniform3fFn)(int, float, float, float);
typedef void (APIENTRY* Uniform4fFn)(int, float, float, float, float);
typedef void (APIENTRY* UniformMatrix4fvFn)(int, int, unsigned char, const float*);
typedef void (APIENTRY* GenObjectsFn)(int, unsigned int*);
typedef void (APIENTRY* DeleteObjectsFn)(int, const unsigned int*);
typedef void (APIENTRY* BindVertexArrayFn)(unsigned int);
typedef void (APIENTRY* BindTargetFn)(unsigned int, unsigned int);
typedef void (APIENTRY* FramebufferTexture2DFn)(unsigned int, unsigned int, unsigned int, unsigned int, int);
typedef void (APIENTRY* FramebufferRenderbufferFn)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef unsigned int (APIENTRY* CheckFramebufferStatusFn)(unsigned int);
typedef void (APIENTRY* RenderbufferStorageFn)(unsigned int, unsigned int, int, int);
typedef void (APIENTRY* ActiveTextureFn)(unsigned int);
typedef void (APIENTRY* GenerateMipmapFn)(unsigned int);
typedef void (APIENTRY* BlendFuncSeparateFn)(unsigned int, unsigned int, unsigned int, unsigned int);

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
DeleteObjectFn pDeleteShader;
DeleteObjectFn pDeleteProgram;
UseProgramFn pUseProgram;
GetUniformLocationFn pGetUniformLocation;
Uniform1iFn pUniform1i;
Uniform1fFn pUniform1f;
Uniform2iFn pUniform2i;
Uniform2fFn pUniform2f;
Uniform3fFn pUniform3f;
Uniform4fFn pUniform4f;
UniformMatrix4fvFn pUniformMatrix4fv;
GenObjectsFn pGenVertexArrays;
DeleteObjectsFn pDeleteVertexArrays;
BindVertexArrayFn pBindVertexArray;
GenObjectsFn pGenFramebuffers;
DeleteObjectsFn pDeleteFramebuffers;
BindTargetFn pBindFramebuffer;
FramebufferTexture2DFn pFramebufferTexture2D;
FramebufferRenderbufferFn pFramebufferRenderbuffer;
CheckFramebufferStatusFn pCheckFramebufferStatus;
GenObjectsFn pGenRenderbuffers;
DeleteObjectsFn pDeleteRenderbuffers;
BindTargetFn pBindRenderbuffer;
RenderbufferStorageFn pRenderbufferStorage;
ActiveTextureFn pActiveTexture;
GenerateMipmapFn pGenerateMipmap;
BlendFuncSeparateFn pBlendFuncSeparate;

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
            && load(pDeleteShader, "glDeleteShader") && load(pDeleteProgram, "glDeleteProgram")
            && load(pUseProgram, "glUseProgram") && load(pGetUniformLocation, "glGetUniformLocation")
            && load(pUniform1i, "glUniform1i") && load(pUniform1f, "glUniform1f") && load(pUniform2i, "glUniform2i")
            && load(pUniform2f, "glUniform2f") && load(pUniform3f, "glUniform3f") && load(pUniform4f, "glUniform4f")
            && load(pUniformMatrix4fv, "glUniformMatrix4fv") && load(pGenVertexArrays, "glGenVertexArrays")
            && load(pDeleteVertexArrays, "glDeleteVertexArrays") && load(pBindVertexArray, "glBindVertexArray")
            && load(pGenFramebuffers, "glGenFramebuffers") && load(pDeleteFramebuffers, "glDeleteFramebuffers")
            && load(pBindFramebuffer, "glBindFramebuffer") && load(pFramebufferTexture2D, "glFramebufferTexture2D")
            && load(pFramebufferRenderbuffer, "glFramebufferRenderbuffer")
            && load(pCheckFramebufferStatus, "glCheckFramebufferStatus") && load(pGenRenderbuffers, "glGenRenderbuffers")
            && load(pDeleteRenderbuffers, "glDeleteRenderbuffers") && load(pBindRenderbuffer, "glBindRenderbuffer")
            && load(pRenderbufferStorage, "glRenderbufferStorage") && load(pActiveTexture, "glActiveTexture")
            && load(pGenerateMipmap, "glGenerateMipmap") && load(pBlendFuncSeparate, "glBlendFuncSeparate");
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
#define glDeleteProgram pDeleteProgram
#define glUseProgram pUseProgram
#define glGetUniformLocation pGetUniformLocation
#define glUniform1i pUniform1i
#define glUniform1f pUniform1f
#define glUniform2i pUniform2i
#define glUniform2f pUniform2f
#define glUniform3f pUniform3f
#define glUniform4f pUniform4f
#define glUniformMatrix4fv pUniformMatrix4fv
#define glGenVertexArrays pGenVertexArrays
#define glDeleteVertexArrays pDeleteVertexArrays
#define glBindVertexArray pBindVertexArray
#define glGenFramebuffers pGenFramebuffers
#define glDeleteFramebuffers pDeleteFramebuffers
#define glBindFramebuffer pBindFramebuffer
#define glFramebufferTexture2D pFramebufferTexture2D
#define glFramebufferRenderbuffer pFramebufferRenderbuffer
#define glCheckFramebufferStatus pCheckFramebufferStatus
#define glGenRenderbuffers pGenRenderbuffers
#define glDeleteRenderbuffers pDeleteRenderbuffers
#define glBindRenderbuffer pBindRenderbuffer
#define glRenderbufferStorage pRenderbufferStorage
#define glActiveTexture pActiveTexture
#define glGenerateMipmap pGenerateMipmap
#define glBlendFuncSeparate pBlendFuncSeparate

#else
namespace {
bool loadOpenGl() {
    return true;
}
}  // namespace
#endif

namespace galaxy {
namespace {

// The plane is a grid of quads. Each vertex finds its place from its number, so
// there is no vertex data: the shader reads the corner's height from the height
// texture (the average of the four cells that meet there) and leans the light on
// the slope between neighboring corners.
constexpr const char* kTerrainVertexShader = R"(#version 150
uniform mat4 uViewProjection;
uniform sampler2D uHeights;
uniform ivec2 uCells;
uniform ivec2 uQuads;
uniform int uStride;
uniform float uCellSize;
uniform float uHeightScale;
out vec2 vUv;
out vec3 vNormal;
out float vHeight;

float cornerHeight(ivec2 corner) {
    ivec2 low = clamp(corner - ivec2(1), ivec2(0), uCells - 1);
    ivec2 high = clamp(corner, ivec2(0), uCells - 1);
    float sum = texelFetch(uHeights, ivec2(low.x, low.y), 0).r
        + texelFetch(uHeights, ivec2(high.x, low.y), 0).r
        + texelFetch(uHeights, ivec2(low.x, high.y), 0).r
        + texelFetch(uHeights, ivec2(high.x, high.y), 0).r;
    return sum * 0.25 * 255.0 * uHeightScale;
}

void main() {
    int quad = gl_VertexID / 6;
    int slot = gl_VertexID - quad * 6;
    ivec2 offsets[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 0), ivec2(1, 1), ivec2(0, 1));
    ivec2 cell = ivec2(quad % uQuads.x, quad / uQuads.x);
    ivec2 corner = min((cell + offsets[slot]) * uStride, uCells);

    float height = cornerHeight(corner);
    float step = float(uStride);
    float west = cornerHeight(max(corner - ivec2(uStride, 0), ivec2(0)));
    float east = cornerHeight(min(corner + ivec2(uStride, 0), uCells));
    float north = cornerHeight(max(corner - ivec2(0, uStride), ivec2(0)));
    float south = cornerHeight(min(corner + ivec2(0, uStride), uCells));
    vNormal = normalize(vec3(west - east, 2.0 * step * uCellSize, north - south));

    vec3 position = vec3(float(corner.x) * uCellSize, height, float(corner.y) * uCellSize);
    vUv = vec2(corner) / vec2(uCells);
    vHeight = height;
    gl_Position = uViewProjection * vec4(position, 1.0);
}
)";

constexpr const char* kTerrainFragmentShader = R"(#version 150
uniform sampler2D uColors;
in vec2 vUv;
in vec3 vNormal;
in float vHeight;
out vec4 fragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 sun = normalize(vec3(-0.45, 0.80, -0.40));
    float diffuse = max(dot(normal, sun), 0.0);
    // Flat ground keeps the colors of the 2D view, and slopes darken or brighten around that.
    float flatLight = max(sun.y, 0.0);
    float light = 0.50 + 0.50 * diffuse / flatLight;
    vec3 color = texture(uColors, vUv).rgb * clamp(light, 0.35, 1.25);
    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

constexpr const char* kSeaVertexShader = R"(#version 150
uniform mat4 uViewProjection;
uniform vec2 uExtent;
uniform float uLevel;

void main() {
    vec2 corners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 at = corners[gl_VertexID] * uExtent;
    gl_Position = uViewProjection * vec4(at.x, uLevel, at.y, 1.0);
}
)";

constexpr const char* kSeaFragmentShader = R"(#version 150
uniform vec4 uColor;
out vec4 fragColor;

void main() {
    fragColor = uColor;
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
        std::fprintf(stderr, "3D view shader did not compile: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) {
            glDeleteShader(vertex);
        }
        if (fragment != 0) {
            glDeleteShader(fragment);
        }
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        std::fprintf(stderr, "3D view program did not link: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// Puts back whatever the rest of the program had selected.
struct SavedState {
    GLint viewport[4] = {};
    GLint framebuffer = 0;
    GLint program = 0;
    GLint vertexArray = 0;
    GLint activeTexture = 0;
    GLint texture0 = 0;
    GLint texture1 = 0;
    GLboolean blend = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean depthMask = GL_TRUE;
    GLint blendSourceRgb = GL_ONE;
    GLint blendDestinationRgb = GL_ZERO;
    GLint blendSourceAlpha = GL_ONE;
    GLint blendDestinationAlpha = GL_ZERO;

    SavedState() {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1);
        glActiveTexture(static_cast<GLenum>(activeTexture));
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        depthTest = glIsEnabled(GL_DEPTH_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSourceRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDestinationRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSourceAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDestinationAlpha);
    }

    ~SavedState() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vertexArray));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture1));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        const auto set = [](GLenum capability, GLboolean on) {
            if (on) {
                glEnable(capability);
            } else {
                glDisable(capability);
            }
        };
        set(GL_BLEND, blend);
        set(GL_SCISSOR_TEST, scissor);
        set(GL_DEPTH_TEST, depthTest);
        set(GL_CULL_FACE, cull);
        glDepthMask(depthMask);
        glBlendFuncSeparate(
            static_cast<GLenum>(blendSourceRgb),
            static_cast<GLenum>(blendDestinationRgb),
            static_cast<GLenum>(blendSourceAlpha),
            static_cast<GLenum>(blendDestinationAlpha));
    }
};

}  // namespace

Terrain3D::~Terrain3D() {
    // The window and its context may already be gone at exit, and the operating
    // system frees the objects with it, so nothing is deleted here.
}

bool Terrain3D::init() {
    if (ready_) {
        return true;
    }
    if (failed_) {
        return false;
    }
    if (!loadOpenGl()) {
        failed_ = true;
        return false;
    }
    terrainProgram_ = link(kTerrainVertexShader, kTerrainFragmentShader);
    seaProgram_ = link(kSeaVertexShader, kSeaFragmentShader);
    if (terrainProgram_ == 0 || seaProgram_ == 0) {
        failed_ = true;
        return false;
    }
    static const char* const kTerrainNames[] = {
        "uViewProjection", "uHeights", "uCells", "uQuads", "uStride", "uCellSize", "uHeightScale", "uColors"};
    for (int i = 0; i < 8; ++i) {
        terrainUniforms_[i] = glGetUniformLocation(terrainProgram_, kTerrainNames[i]);
    }
    static const char* const kSeaNames[] = {"uViewProjection", "uExtent", "uLevel", "uColor"};
    for (int i = 0; i < 4; ++i) {
        seaUniforms_[i] = glGetUniformLocation(seaProgram_, kSeaNames[i]);
    }

    glGenVertexArrays(1, &vertexArray_);
    glGenTextures(1, &heightTexture_);
    glGenTextures(1, &colorTexture_);
    glGenTextures(1, &targetTexture_);
    glGenRenderbuffers(1, &depthBuffer_);
    glGenFramebuffers(1, &framebuffer_);
    ready_ = true;
    return true;
}

void Terrain3D::setHeights(const std::uint8_t* heights, int cellsWide, int cellsHigh, float cellSize) {
    if (!heights || cellsWide < 1 || cellsHigh < 1 || !init()) {
        return;
    }
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, heightTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cellsWide, cellsHigh, 0, GL_RED, GL_UNSIGNED_BYTE, heights);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    cellsWide_ = cellsWide;
    cellsHigh_ = cellsHigh;
    cellSize_ = cellSize;
}

void Terrain3D::setColors(const std::uint8_t* rgba, int width, int height) {
    if (!rgba || width < 1 || height < 1 || !init()) {
        return;
    }
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Sharper ground at a low angle, where the drivers allow it. Drivers without it ignore this.
    glTexParameterf(GL_TEXTURE_2D, 0x84FE, 8.0f);
    glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    haveColors_ = true;
}

void Terrain3D::resizeTarget(int width, int height) {
    if (width == targetWidth_ && height == targetHeight_) {
        return;
    }
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, targetTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
    targetWidth_ = width;
    targetHeight_ = height;
}

unsigned int Terrain3D::render(
    int width, int height, const float viewProjection[16], const Sea& sea, const float background[3]) {
    if (width < 1 || height < 1 || cellsWide_ < 1 || cellsHigh_ < 1 || !haveColors_ || !init()) {
        return 0;
    }

    SavedState saved;
    resizeTarget(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        failed_ = true;
        return 0;
    }

    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearColor(background[0], background[1], background[2], 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindVertexArray(vertexArray_);

    // Keep the mesh to about a million quads on very large maps by skipping cells.
    const double cells = static_cast<double>(cellsWide_) * static_cast<double>(cellsHigh_);
    const int stride = std::max(1, static_cast<int>(std::ceil(std::sqrt(cells / 1.0e6))));
    const int quadsWide = (cellsWide_ + stride - 1) / stride;
    const int quadsHigh = (cellsHigh_ + stride - 1) / stride;

    glUseProgram(terrainProgram_);
    glUniformMatrix4fv(terrainUniforms_[0], 1, GL_FALSE, viewProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, heightTexture_);
    glUniform1i(terrainUniforms_[1], 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glUniform1i(terrainUniforms_[7], 1);
    glUniform2i(terrainUniforms_[2], cellsWide_, cellsHigh_);
    glUniform2i(terrainUniforms_[3], quadsWide, quadsHigh);
    glUniform1i(terrainUniforms_[4], stride);
    glUniform1f(terrainUniforms_[5], cellSize_);
    glUniform1f(terrainUniforms_[6], kHeightScale);
    glDrawArrays(GL_TRIANGLES, 0, quadsWide * quadsHigh * 6);

    // The sea is drawn last and does not write depth, so the ground below the
    // surface shows through it and the ground above it hides it.
    if (sea.opacity > 0.0f) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(seaProgram_);
        glUniformMatrix4fv(seaUniforms_[0], 1, GL_FALSE, viewProjection);
        glUniform2f(seaUniforms_[1], static_cast<float>(cellsWide_) * cellSize_, static_cast<float>(cellsHigh_) * cellSize_);
        glUniform1f(seaUniforms_[2], sea.level * kHeightScale);
        glUniform4f(seaUniforms_[3], sea.red, sea.green, sea.blue, sea.opacity);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    return targetTexture_;
}

}  // namespace galaxy
