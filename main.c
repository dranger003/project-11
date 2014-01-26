#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglvivante.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

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

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

//    glEnable(GL_BLEND);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    static const GLchar *vertex_shader_source[] = {
        "attribute vec4 v_position;         \n"
        "attribute vec2 v_coordinates;      \n"
        "varying vec2 coordinates;          \n"
        "                                   \n"
        "void main() {                      \n"
        "   coordinates = v_coordinates;    \n"
        "   gl_Position = v_position;       \n"
        "}                                  \n"
    };

    //
    // Y'UV420sp (NV21) to ARGB8888 conversion
    // Source: en.wikipedia.org/wiki/Yuv
    //
    // private static int convertYUVtoARGB(int y, int u, int v) {
    //     int r,g,b;

    //     r = y + (int)(1.402f*u);
    //     g = y - (int)(0.344f*v + 0.714f*u);
    //     b = y + (int)(1.772f*v);
    //     r = r>255? 255 : r<0 ? 0 : r;
    //     g = g>255? 255 : g<0 ? 0 : g;
    //     b = b>255? 255 : b<0 ? 0 : b;
    //     return 0xff000000 | (r<<16) | (g<<8) | b;
    // }

    // YUV to BGR conversion matrix (column major)
    static const GLfloat matrix[] = {
        //   B        G        R
        1.000f,  1.000f,  1.000f, // Y
        1.772f, -0.344f,  0.000f, // V
        0.000f, -0.714f,  1.402f, // U
    };

    static const GLchar *fragment_shader_source[] = {
        "uniform sampler2D texture_y;                               \n"
        "uniform sampler2D texture_u;                               \n"
        "uniform sampler2D texture_v;                               \n"
        "varying vec2 coordinates;                                  \n"
        "uniform mat3 yuv_to_rgb;                                   \n"
        "                                                           \n"
        "void main() {                                              \n"
        "   float y = texture2D(texture_y, coordinates).r;          \n"
        "   float u = texture2D(texture_u, coordinates).r - 0.5;    \n"
        "   float v = texture2D(texture_v, coordinates).r - 0.5;    \n"
        "   vec3 rgb = yuv_to_rgb * vec3(y, u, v);                  \n"
        "   gl_FragColor = vec4(clamp(rgb, 0.0, 255.0), 1.0);       \n"
        "}                                                          \n"
    };

    GLuint vertex_shader = gl_compile_shader(GL_VERTEX_SHADER, vertex_shader_source[0]);
    assert(vertex_shader != -1);
    GLuint fragment_shader = gl_compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source[0]);
    assert(fragment_shader != -1);
    GLuint program = gl_link_program(vertex_shader, fragment_shader);
    assert(program != -1);

    // -1, 1 (0) | 1, 1 (2)
    // ----------|---------
    // -1,-1 (1) | 1,-1 (3)
    GLfloat vertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
         1.0f, -1.0f,
    };

    // 0, 1 (1) | 1, 1 (3)
    // ---------|---------
    // 0, 0 (0) | 1, 0 (2)
    GLfloat coordinates[] = {
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
    };

    GLint v_position = glGetAttribLocation(program, "v_position");
    glEnableVertexAttribArray(v_position);
    glVertexAttribPointer(v_position, 2, GL_FLOAT, GL_FALSE, 0, vertices);

    GLint v_coordinates = glGetAttribLocation(program, "v_coordinates");
    glEnableVertexAttribArray(v_coordinates);
    glVertexAttribPointer(v_coordinates, 2, GL_FLOAT, GL_FALSE, 0, coordinates);

    {
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

        GLvoid *planes[] = {
            (GLvoid *)malloc(y),
            (GLvoid *)malloc(u),
            (GLvoid *)malloc(v)
        };

        fseek(fp, file_size % frame_size, SEEK_SET);
        fread(planes[0], y, 1, fp);
        fread(planes[2], v, 1, fp);
        fread(planes[1], u, 1, fp);

        fclose(fp);

        glUniformMatrix3fv(
            glGetUniformLocation(program, "yuv_to_rgb"),
            1,
            GL_FALSE,
            matrix);

        GLuint textures[3];
        glGenTextures(3, textures);

        // Y
        GLint texture_y = glGetUniformLocation(program, "texture_y");
        glUniform1i(texture_y, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_y);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_LUMINANCE,
            w,
            h,
            0,
            GL_LUMINANCE,
            GL_UNSIGNED_BYTE,
            (GLvoid *)planes[0]);

        // U
        GLint texture_u = glGetUniformLocation(program, "texture_u");
        glUniform1i(texture_u, 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture_u);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_LUMINANCE,
            w / 2,
            h / 2,
            0,
            GL_LUMINANCE,
            GL_UNSIGNED_BYTE,
            (GLvoid *)planes[1]);

        // V
        GLint texture_v = glGetUniformLocation(program, "texture_v");
        glUniform1i(texture_v, 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texture_v);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_LUMINANCE,
            w / 2,
            h / 2,
            0,
            GL_LUMINANCE,
            GL_UNSIGNED_BYTE,
            (GLvoid *)planes[2]);

        free(planes[2]);
        free(planes[1]);
        free(planes[0]);
    }

    glViewport(0, 0, device.width, device.height);

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
