#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <time.h>

#include <EGL/egl.h>
#include <EGL/eglvivante.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "pngx.h"

typedef void (GL_APIENTRY *PFNGLTEXDIRECTVIV)(GLenum target, GLsizei width, GLsizei height, GLenum format, GLvoid **texels);
typedef void (GL_APIENTRY *PFNGLTEXDIRECTINVALIDATEVIV)(GLenum target);

struct egl_device {
    EGLNativeDisplayType display_type;
    EGLDisplay display;
    const EGLint *config_attributes;
    EGLConfig config;
    EGLNativeWindowType window;
    EGLSurface surface;
    const EGLint *context_attributes;
    EGLContext context;
    int width;
    int height;
};

volatile sig_atomic_t done = 0;

void signal_handler(int signal)
{
    done = 1;
}

int clock_gettime_diff(const struct timespec *beg, const struct timespec *end, struct timespec *diff)
{
    if (!diff)
        return -1;

    diff->tv_sec = end->tv_sec - beg->tv_sec;
    diff->tv_nsec = end->tv_nsec - beg->tv_nsec;

    return 0;
}

int egl_initialize(struct egl_device *device)
{
    device->display_type = (EGLNativeDisplayType)fbGetDisplayByIndex(0);

    fbGetDisplayGeometry(device->display_type, &device->width, &device->height);

    device->display = eglGetDisplay(device->display_type);
    assert(eglGetError() == EGL_SUCCESS);

    eglInitialize(device->display, NULL, NULL);
    assert(eglGetError() == EGL_SUCCESS);

    eglBindAPI(EGL_OPENGL_ES_API);
    assert(eglGetError() == EGL_SUCCESS);

    static const EGLint config_attributes[] = {
        EGL_SAMPLES,        0,
        EGL_RED_SIZE,       8,
        EGL_GREEN_SIZE,     8,
        EGL_BLUE_SIZE,      8,
        EGL_ALPHA_SIZE,     EGL_DONT_CARE,
        EGL_DEPTH_SIZE,     0,
        EGL_SURFACE_TYPE,   EGL_WINDOW_BIT,
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

GLuint gl_compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader)
        return -1;

    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return -1;
    }

    return shader;
}

GLuint gl_link_program(GLuint vertex_shader, GLuint fragment_shader)
{
    GLuint program = glCreateProgram();
    if (!program)
        return -1;

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    // Schedule shaders for later deletion
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(program);
        return -1;
    }

    glUseProgram(program);

    // Schedule program for later deletion
    glDeleteProgram(program);

    return program;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, &signal_handler);
    signal(SIGTERM, &signal_handler);

    setenv("FB_MULTI_BUFFER", "2", 0);

    system("echo 0 > /sys/devices/virtual/graphics/fbcon/cursor_blink");
    system("setterm -cursor off");

    struct egl_device device = { 0 };
    egl_initialize(&device);

    static const GLchar *vertex_shader_source[] = {
        "attribute vec4 v_position;         \n"
        "attribute vec2 v_coordinates;      \n"
        "varying vec2 coordinates;          \n"
        "                                   \n"
        "void main() {                      \n"
        "   gl_Position = v_position;       \n"
        "   coordinates = v_coordinates;    \n"
        "}                                  \n"
    };

    static const GLchar *fragment_shader_source[] = {
        "precision highp float;                             \n"
        "                                                   \n"
        "uniform sampler2D texture;                         \n"
        "varying vec2 coordinates;                          \n"
        "                                                   \n"
        "void main() {                                      \n"
        "   gl_FragColor = texture2D(texture, coordinates); \n"
        "}                                                  \n"
    };

    GLuint vertex_shader = gl_compile_shader(GL_VERTEX_SHADER, vertex_shader_source[0]);
    GLuint fragment_shader = gl_compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source[0]);
    GLuint program = gl_link_program(vertex_shader, fragment_shader);

    // -1, 1 (2) | 1, 1 (3)
    // ----------|---------
    // -1,-1 (0) | 1,-1 (1)
    GLfloat vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    // 0, 1 (2) | 1, 1 (3)
    // ---------|---------
    // 0, 0 (0) | 1, 0 (1)
    GLfloat coordinates[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    };

    GLint v_position = glGetAttribLocation(program, "v_position");
    glEnableVertexAttribArray(v_position);
    glVertexAttribPointer(v_position, 2, GL_FLOAT, GL_FALSE, 0, vertices);

    GLint v_coordinates = glGetAttribLocation(program, "v_coordinates");
    glEnableVertexAttribArray(v_coordinates);
    glVertexAttribPointer(v_coordinates, 2, GL_FLOAT, GL_FALSE, 0, coordinates);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLuint texture;
    glGenTextures(1, &texture);

    glActiveTexture(texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    {
        PFNGLTEXDIRECTVIV glTexDirectVIV =
            (PFNGLTEXDIRECTVIV)eglGetProcAddress("glTexDirectVIV");
        if (!glTexDirectVIV)
            printf("glTexDirectVIV() missing\n");

        PFNGLTEXDIRECTINVALIDATEVIV glTexDirectInvalidateVIV =
            (PFNGLTEXDIRECTINVALIDATEVIV)eglGetProcAddress("glTexDirectInvalidateVIV");
        if (!glTexDirectInvalidateVIV)
            printf("glTexDirectInvalidateVIV() missing\n");

        int w = 1920, h = 1080;
        int y = w * h;
        int v = y / 4;
        int u = v;

        FILE *fp = fopen("frame.yuv", "rb");
        fseek(fp, 0, SEEK_END);

        int file_size = ftell(fp);
        int frame_size = y + u + v;
        int frame_count = file_size / frame_size;

        printf("file_size = %d, frame_size = %d, frame_count = %d\n", file_size, frame_size, frame_count);

        void *planes[3];

        glTexDirectVIV(
            GL_TEXTURE_2D,
            w,
            h,
            GL_VIV_YV12,
            (GLvoid **)&planes);

        fseek(fp, file_size % frame_size, SEEK_SET);
        fread(planes[0], y, 1, fp);
        fread(planes[1], v, 1, fp);
        fread(planes[2], u, 1, fp);

        fclose(fp);

        glTexDirectInvalidateVIV(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glViewport(0, 0, device.width, device.height);

    //    glClearColor(128 / 255.0f, 128 / 255.0f, 128 / 255.0f, 1.0f);
    //    glClear(GL_COLOR_BUFFER_BIT);

    //    glEnable(GL_BLEND);
    //    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    struct timespec time_beg, time_end, time_diff;

    while (!done) {
        clock_gettime(CLOCK_MONOTONIC, &time_beg);

        eglSwapBuffers(device.display, device.surface);

        clock_gettime(CLOCK_MONOTONIC, &time_end);
        clock_gettime_diff(&time_beg, &time_end, &time_diff);

        printf("Current FPS: %.3Lf%10s\r",
            1.0L / (time_diff.tv_sec + time_diff.tv_nsec / 1000000000.0),
            " ");
    }

    printf("\n");

    egl_deinitialize(&device);

    system("setterm -cursor on");
    system("echo 1 > /sys/devices/virtual/graphics/fbcon/cursor_blink");

    // Reset screen
    char cmd_line[32];
    sprintf(cmd_line, "fbset -xres %d -yres %d", device.width, device.height);
    system(cmd_line);

    return 0;
}
