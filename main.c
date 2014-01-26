#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <EGL/egl.h>
#include <EGL/eglvivante.h>
#include <GLES2/gl2.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

struct egl_device {
    EGLNativeDisplayType display_type;
    EGLDisplay display;
    const EGLint *config_attributes;
    EGLConfig config;
    EGLNativeWindowType window;
    EGLSurface surface;
    const EGLint *context_attributes;
    EGLContext context;
};

struct freetype_library {
    FT_Library handle;
    FT_Face face;
};

volatile sig_atomic_t done = 0;

void signal_handler(int signal)
{
    done = 1;
}

int egl_initialize(struct egl_device *device)
{
    device->display_type = (EGLNativeDisplayType)fbGetDisplayByIndex(0);

    device->display = eglGetDisplay(device->display_type);
    assert(eglGetError() == EGL_SUCCESS);

    eglInitialize(device->display, NULL, NULL);
    assert(eglGetError() == EGL_SUCCESS);

    eglBindAPI(EGL_OPENGL_ES_API);
    assert(eglGetError() == EGL_SUCCESS);

    static const EGLint config_attributes[] = {
        EGL_SAMPLES,			0,
        EGL_RED_SIZE,			8,
        EGL_GREEN_SIZE,			8,
        EGL_BLUE_SIZE,			8,
        EGL_ALPHA_SIZE,			EGL_DONT_CARE,
        EGL_DEPTH_SIZE,			0,
        EGL_SURFACE_TYPE,		EGL_WINDOW_BIT,
        EGL_NONE
    };

    EGLint config_count = 0;
    eglChooseConfig(device->display, config_attributes, &device->config, 1, &config_count);
    assert(eglGetError() == EGL_SUCCESS);
    assert(config_count == 1);

    device->config_attributes = config_attributes;

    device->window = fbCreateWindow(device->display_type, 0, 0, 0, 0);

    device->surface = eglCreateWindowSurface(device->display, device->config, device->window, NULL);
    assert(eglGetError() == EGL_SUCCESS);

    static const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION,		2,
        EGL_NONE
    };

    device->context = eglCreateContext(device->display, device->config, EGL_NO_CONTEXT, context_attributes);
    assert(eglGetError() == EGL_SUCCESS);

    device->context_attributes = context_attributes;

    eglMakeCurrent(device->display, device->surface, device->surface, device->context);
    assert(eglGetError() == EGL_SUCCESS);

    return 0;
}

int egl_deinitialize(struct egl_device *device)
{
    eglMakeCurrent(device->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    assert(eglGetError() == EGL_SUCCESS);

    eglDestroyContext(device->display, device->context);
    assert(eglGetError() == EGL_SUCCESS);
    device->context = (EGLContext)0;

    eglDestroySurface(device->display, device->surface);
    assert(eglGetError() == EGL_SUCCESS);
    device->surface = (EGLSurface)0;

    fbDestroyWindow(device->window);
    device->window = (EGLNativeWindowType)0;

    eglTerminate(device->display);
    assert(eglGetError() == EGL_SUCCESS);
    device->display = (EGLDisplay)0;

    eglReleaseThread();
    assert(eglGetError() == EGL_SUCCESS);

    return 0;
}

char *font_name;
char *font_path;
int font_size;

int freetype_initialize(struct freetype_library *library)
{
/*
    char *file_name;

    { // TODO: Cleanup this mess! //////////////////////////////////
        FcConfig *config = FcInitLoadConfigAndFonts();
        FcPattern *pattern = FcNameParse((const FcChar8 *)font_name);
        FcConfigSubstitute(config, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        FcPattern *font = FcFontMatch(config, pattern, &result);
        FcChar8 *file;
        FcPatternGetString(font, FC_FILE, 0, &file);
        file_name = (char *)file;
    } //////////////////////////////////////////////////////////////
*/
    if (FT_Init_FreeType(&library->handle))
        return 1;

    if (FT_New_Face(library->handle, font_path, 0, &library->face))
        return 1;

    FT_Set_Pixel_Sizes(library->face, 0, font_size);

    return 0;
}

int freetype_deinitialize(struct freetype_library *library)
{
    return 0;
}

void render_text(struct freetype_library *l, const char *text, float x, float y, float sx, float sy)
{
    const char *p;
    FT_GlyphSlot g = l->face->glyph;

    for (p = text; *p; p++) {
        if (FT_Load_Char(l->face, *p, FT_LOAD_RENDER))
            continue;

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_ALPHA,
            g->bitmap.width,
            g->bitmap.rows,
            0,
            GL_ALPHA,
            GL_UNSIGNED_BYTE,
            g->bitmap.buffer);

        float x2 = x + g->bitmap_left * sx;
        float y2 = -y - g->bitmap_top * sy;
        float w = g->bitmap.width * sx;
        float h = g->bitmap.rows * sy;

        GLfloat box[][4] = {
            { x2,     -y2    , 0, 0 },
            { x2 + w, -y2    , 1, 0 },
            { x2,     -y2 - h, 0, 1 },
            { x2 + w, -y2 - h, 1, 1 },
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof(box), box, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        x += (g->advance.x >> 6) * sx;
        y += (g->advance.y >> 6) * sy;
    }
}

char *font_text;

int run()
{
    setenv("FB_MULTI_BUFFER", "2", 0);
    system("echo 0 > /sys/devices/virtual/graphics/fbcon/cursor_blink");

    struct egl_device device = { 0 };
    egl_initialize(&device);

    int display_width, display_height;
    fbGetDisplayGeometry(device.display_type, &display_width, &display_height);

    system("setterm -cursor off");

    struct timespec run_beg, fps_beg, time_end;
    clock_gettime(CLOCK_MONOTONIC, &run_beg);

    static const GLchar *vertex_shader_source[] = {
        "attribute vec4 coord;						\n"
        "varying vec2 texcoord;						\n"
        "											\n"
        "void main(void) {							\n"
        "  gl_Position = vec4(coord.xy, 0, 1);		\n"
        "  texcoord = coord.zw;						\n"
        "}											\n"
    };

    static const GLchar *fragment_shader_source[] = {
        "varying vec2 texcoord;													\n"
        "uniform sampler2D tex;													\n"
        "uniform vec4 color;													\n"
        "																		\n"
        "void main(void) {														\n"
        "  gl_FragColor = vec4(1, 1, 1, texture2D(tex, texcoord).a) * color;	\n"
        "}																		\n"
    };

    GLint compiled, linked;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &compiled);
    assert(compiled != 0);

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &compiled);
    assert(compiled != 0);

    GLuint program = glCreateProgram();

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked != 0);

    glUseProgram(program);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint tex;
    glGenTextures(1, &tex);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLint uniform_tex = glGetUniformLocation(program, "tex");
    glUniform1i(uniform_tex, 0);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    GLint attribute_coord = glGetAttribLocation(program, "coord");
    glEnableVertexAttribArray(attribute_coord);
    glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

    GLfloat white[4] = { 1, 1, 1, 1 };
    GLint uniform_color = glGetUniformLocation(program, "color");
    glUniform4fv(uniform_color, 1, white);

    // Scaling factors
    // One glyph pixel should correspond to one screen pixel
    float sx = 2.0 / 1920;
    float sy = 2.0 / 1080;

    struct freetype_library library;
    assert(freetype_initialize(&library) == 0);

    char text1[256] = "", text2[256] = "";
    while (!done) {
        clock_gettime(CLOCK_MONOTONIC, &fps_beg);

        {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);

            render_text(
                &library,
                text1,
                -1 + 20 * sx,
                1 - 60 * sy,
                sx,
                sy);

            render_text(
                &library,
                text2,
                -1 + 20 * sx,
                1 - (1080 - 20) * sy,
                sx,
                sy);

            eglSwapBuffers(device.display, device.surface);
        }

        clock_gettime(CLOCK_MONOTONIC, &time_end);

        sprintf(text1, "RT: %.3fs",
            time_end.tv_sec - run_beg.tv_sec + ((time_end.tv_nsec - run_beg.tv_nsec) / 1000000000.0));

        sprintf(text2, "FPS: %.3Lf",
            1.0L / ((time_end.tv_sec - fps_beg.tv_sec) + ((time_end.tv_nsec - fps_beg.tv_nsec) / 1000000000.0)));

        printf("RT: %.3fs, FPS: %.3Lf%10s\r",
            time_end.tv_sec - run_beg.tv_sec + ((time_end.tv_nsec - run_beg.tv_nsec) / 1000000000.0),
            1.0L / ((time_end.tv_sec - fps_beg.tv_sec) + ((time_end.tv_nsec - fps_beg.tv_nsec) / 1000000000.0)),
            " ");
    }

    printf("\n");

    system("setterm -cursor on");

    egl_deinitialize(&device);

    char cmd_line[32];
    sprintf(cmd_line, "fbset -xres %d -yres %d", display_width, display_height);
    system(cmd_line);

    system("echo 1 > /sys/devices/virtual/graphics/fbcon/cursor_blink");

    return 0;
}

int main(int argc, char *argv[])
{
    font_name = argv[1];
    font_path = argv[1];
    font_size = atoi(argv[2]);
    font_text = argv[3];

    signal(SIGINT, &signal_handler);
    signal(SIGTERM, &signal_handler);

    int result = run();

    return result;
}
