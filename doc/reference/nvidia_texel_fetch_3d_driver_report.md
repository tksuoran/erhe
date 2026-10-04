# NVIDIA 595.91.07 OpenGL: texelFetch on a sampler3D returns zeros when the z coordinate derives from an integer division of the pixel coordinate

Standalone reproduction (raw GLX + GL 4.6 core, no erhe code) of the
`Texel_fetch_test.texture_3d` failure of `erhe_graphics_gpu_tests` on the
OpenGL backend (`doc/plans/opengl_test_failures.md` item 1). Measured
2026-10-04 on Linux, `GL_RENDERER` NVIDIA (RTX 5070 laptop), `GL_VERSION`
`4.6.0 NVIDIA 595.91.07`.

## Summary

An immutable 16x16x4 `GL_RGBA8` `GL_TEXTURE_3D` (one level, uploaded with
`glTextureSubImage3D`) is read in a fragment shader with
`texelFetch(s_texture, ivec3(texel, layer), 0)` into a 16x16 color
attachment. When `layer` is computed from the pixel coordinate through an
integer division (`tile = pixel / 16; layer = 3 - (tile.x + 2 * tile.y)`),
every fetch returns `(0, 0, 0, 0)` although every coordinate is in range
(`tile` is `(0, 0)` for every pixel of a 16x16 target, so `layer` is 3).
The same shader reads the correct texels when `layer` is a constant, when
the expression's result is consumed by a comparison, or when the sampler is
a `sampler2DArray` over an identical `GL_TEXTURE_2D_ARRAY`.

Variants, same texture, same draw, 256 texels compared against the CPU model
(`fail` = all 256 texels read `(0, 0, 0, 0)`):

| `layer` expression | sampler | result |
|---|---|---|
| `3 - (tile.x + 2 * tile.y)` (`tile = pixel / 16`) | sampler3D | fail |
| `3 - tile.x` | sampler3D | fail |
| `3 - (tile.x + tile.y)` | sampler3D | fail |
| `3 - ((pixel.x >> 4) + 2 * (pixel.y >> 4))` | sampler3D | fail |
| `3 - int(uint(pixel.x) / 16u + 2u * (uint(pixel.y) / 16u))` | sampler3D | fail |
| `3 - 2 * tile.y` | sampler3D | pass |
| `3` | sampler3D | pass |
| `clamp(3 - (tile.x + 2 * tile.y), 0, 3)` | sampler3D | pass |
| `3 - (tile.x + 2 * tile.y); if (layer < 0) layer = 0;` | sampler3D | pass |
| `3 - (tile.x + 2 * tile.y)` followed by `if (layer != 3 \|\| texel out of range) fetched = vec4(1.0)` | sampler3D | pass |
| `3 - (tile.x + 2 * tile.y)` | sampler2DArray | pass |
| `int(gl_FragCoord.x) & 3` (z varies per pixel) | sampler3D | pass |

No effect: binding a sampler object, `GL_TEXTURE_MIN_FILTER` on the texture
object, the texture unit, uploading through a pixel unpack buffer, a bindless
texture handle made resident, `#extension GL_ARB_bindless_texture : enable`,
the `.gbra` swizzle of the result, mirroring x or flipping y of the fetch.
Sampling the same texture with `texture()` returns the correct texels in
every variant. Under Mesa zink on the same GPU the erhe test fails too (not
re-measured standalone).

The pass/fail pattern follows the shape of the integer expression feeding the
z coordinate, not the values it produces, which points at the GLSL compiler's
handling of `texelFetch` on a 3D image.

## Reproduction program

Build and run on Linux with an X display:

```
c++ -std=c++20 -O1 repro.cpp -o repro -lGL -lX11
./repro            # layer from pixel / 16: prints 256 mismatches, texel (0,0) {0, 0, 0, 0}
./repro --control  # layer = 3: prints 0 mismatches
```

```cpp
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define LOAD(type, name) static type name = reinterpret_cast<type>(glXGetProcAddress(reinterpret_cast<const GLubyte*>(#name)))

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

static const int W = 16, H = 16, D = 4;

static GLuint compile(GLenum type, const std::string& src)
{
    LOAD(PFNGLCREATESHADERPROC, glCreateShader);
    LOAD(PFNGLSHADERSOURCEPROC, glShaderSource);
    LOAD(PFNGLCOMPILESHADERPROC, glCompileShader);
    LOAD(PFNGLGETSHADERIVPROC, glGetShaderiv);
    LOAD(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile failed:\n%s\n%s\n", log, src.c_str());
        std::exit(2);
    }
    return s;
}

int main(int argc, char** argv)
{
    const bool control = (argc > 1) && (std::strcmp(argv[1], "--control") == 0);

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { std::fprintf(stderr, "no display\n"); return 2; }
    static int visual_attribs[] = { GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DOUBLEBUFFER, True, None };
    int fbcount = 0;
    GLXFBConfig* fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), visual_attribs, &fbcount);
    if (!fbc || fbcount == 0) { std::fprintf(stderr, "no fbconfig\n"); return 2; }
    XVisualInfo* vi = glXGetVisualFromFBConfig(dpy, fbc[0]);
    XSetWindowAttributes swa{};
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
    Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 64, 64, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    int ctx_attribs[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 6, GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None };
    GLXContext ctx = glXCreateContextAttribsARB(dpy, fbc[0], nullptr, True, ctx_attribs);
    if (!ctx) { std::fprintf(stderr, "no context\n"); return 2; }
    glXMakeCurrent(dpy, win, ctx);
    std::printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    LOAD(PFNGLCREATETEXTURESPROC, glCreateTextures);
    LOAD(PFNGLTEXTURESTORAGE2DPROC, glTextureStorage2D);
    LOAD(PFNGLTEXTURESTORAGE3DPROC, glTextureStorage3D);
    LOAD(PFNGLTEXTURESUBIMAGE3DPROC, glTextureSubImage3D);
    LOAD(PFNGLBINDTEXTUREUNITPROC, glBindTextureUnit);
    LOAD(PFNGLCREATEPROGRAMPROC, glCreateProgram);
    LOAD(PFNGLATTACHSHADERPROC, glAttachShader);
    LOAD(PFNGLLINKPROGRAMPROC, glLinkProgram);
    LOAD(PFNGLGETPROGRAMIVPROC, glGetProgramiv);
    LOAD(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
    LOAD(PFNGLUSEPROGRAMPROC, glUseProgram);
    LOAD(PFNGLCREATEFRAMEBUFFERSPROC, glCreateFramebuffers);
    LOAD(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC, glNamedFramebufferTexture);
    LOAD(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC, glCheckNamedFramebufferStatus);
    LOAD(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
    LOAD(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays);
    LOAD(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);
    LOAD(PFNGLGETTEXTUREIMAGEPROC, glGetTextureImage);

    // Volume: R = x * 15 + 10, G = y * 15 + 10, B = per slice, A = 255.
    std::vector<unsigned char> volume(W * H * D * 4);
    const int blue[D] = { 40, 110, 180, 250 };
    for (int z = 0; z < D; ++z) for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        unsigned char* t = &volume[((z * H + y) * W + x) * 4];
        t[0] = (unsigned char)(x * 15 + 10); t[1] = (unsigned char)(y * 15 + 10); t[2] = (unsigned char)blue[z]; t[3] = 255;
    }
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_3D, 1, &tex);
    glTextureStorage3D(tex, 1, GL_RGBA8, W, H, D);
    glTextureSubImage3D(tex, 0, 0, 0, 0, W, H, D, GL_RGBA, GL_UNSIGNED_BYTE, volume.data());

    GLuint color = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &color);
    glTextureStorage2D(color, 1, GL_RGBA8, W, H);
    GLuint fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);
    if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { std::fprintf(stderr, "fbo incomplete\n"); return 2; }

    const std::string vs = R"(#version 460 core
void main()
{
    vec2 p[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
}
)";
    // Every pixel of the 16x16 target is in tile (0, 0), so layer is 3 for
    // every pixel; the texel coordinate is in range. Only the way layer is
    // written differs between the failing and the control shader.
    std::string fs = R"(#version 460 core
layout(binding = 0) uniform sampler3D s_texture;
layout(location = 0) out vec4 out_color;
void main()
{
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy));
    ivec2 tile  = pixel / 16;
    ivec2 texel = pixel % 16;
)";
    fs += control ? "    int layer = 3;\n" : "    int layer = 3 - (tile.x + 2 * tile.y);\n";
    fs += R"(    out_color = texelFetch(s_texture, ivec3(texel, layer), 0);
}
)";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) { char log[4096]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log); std::fprintf(stderr, "link failed: %s\n", log); return 2; }

    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, W, H);
    glUseProgram(prog);
    glBindTextureUnit(0, tex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    std::vector<unsigned char> out(W * H * 4);
    glGetTextureImage(color, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)out.size(), out.data());
    int mismatches = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        if (std::memcmp(&out[(y * W + x) * 4], &volume[((3 * H + y) * W + x) * 4], 4) != 0) ++mismatches;
    }
    std::printf("texel (0,0): got {%d, %d, %d, %d} expected {10, 10, 250, 255}; mismatches %d / %d\n", out[0], out[1], out[2], out[3], mismatches, W * H);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) std::printf("GL error 0x%x\n", err);
    return mismatches == 0 ? 0 : 1;
}
```
