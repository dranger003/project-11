#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <time.h>

#include <EGL/egl.h>
#include <EGL/eglvivante.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

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

void draw1()
{
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
        "   vec3 rgb = texture2D(texture, coordinates).rgb; \n"
        "   gl_FragColor = vec4(rgb, 0.5);                  \n"
        "}                                                  \n"
    };

    GLuint vertex_shader = gl_compile_shader(GL_VERTEX_SHADER, vertex_shader_source[0]);
    GLuint fragment_shader = gl_compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source[0]);

    GLuint program = glCreateProgram();

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked);

    glUseProgram(program);

    GLfloat vertices[] = {
        -1,  1,  1,  1,
        -1, -1,  1, -1,
    };

    GLfloat coordinates[] = {
        0, 0, 1, 0,
        0, 1, 1, 1,
    };

    // 2x2 RGB texture
    static const GLubyte texels[] = {
        255,   0,   0,   0, 255,   0,
          0,   0, 255, 255, 255,   0,
    };

    GLint v_position = glGetAttribLocation(program, "v_position");
    glEnableVertexAttribArray(v_position);
    glVertexAttribPointer(v_position, 2, GL_FLOAT, GL_FALSE, 0, vertices);

    GLint v_coordinates = glGetAttribLocation(program, "v_coordinates");
    glEnableVertexAttribArray(v_coordinates);
    glVertexAttribPointer(v_coordinates, 2, GL_FLOAT, GL_FALSE, 0, coordinates);

    GLint texture_uniform = glGetUniformLocation(program, "texture");
    glUniform1i(texture_uniform, 0);

    GLuint texture;
    glGenTextures(1, &texture);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, texels);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void draw2()
{
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
        "   vec3 rgb = texture2D(texture, coordinates).rgb; \n"
        "   gl_FragColor = vec4(rgb, 0.5);                  \n"
        "}                                                  \n"
    };

    GLuint vertex_shader = gl_compile_shader(GL_VERTEX_SHADER, vertex_shader_source[0]);
    GLuint fragment_shader = gl_compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source[0]);

    GLuint program = glCreateProgram();

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked);

    glUseProgram(program);

    GLfloat x = 640 / 960.0;
    GLfloat y = 360 / 540.0;
    GLfloat vertices[] = {
        -x,  y,  x,  y,
        -x, -y,  x, -y,
    };

    GLfloat coordinates[] = {
        0, 0, 1, 0,
        0, 1, 1, 1,
    };

    // 2x2 RGB texture
    static const GLubyte texels[] = {
          0,   0, 255,   0,   0, 255,
          0,   0, 255,   0,   0, 255,
    };

    GLint v_position = glGetAttribLocation(program, "v_position");
    glEnableVertexAttribArray(v_position);
    glVertexAttribPointer(v_position, 2, GL_FLOAT, GL_FALSE, 0, vertices);

    GLint v_coordinates = glGetAttribLocation(program, "v_coordinates");
    glEnableVertexAttribArray(v_coordinates);
    glVertexAttribPointer(v_coordinates, 2, GL_FLOAT, GL_FALSE, 0, coordinates);

    GLint texture_uniform = glGetUniformLocation(program, "texture");
    glUniform1i(texture_uniform, 0);

    GLuint texture;
    glGenTextures(1, &texture);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, texels);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, &signal_handler);
    signal(SIGTERM, &signal_handler);

    setenv("FB_MULTI_BUFFER", "2", 0);
    system("echo 0 > /sys/devices/virtual/graphics/fbcon/cursor_blink");

    struct egl_device device = { 0 };
    egl_initialize(&device);

    int display_width, display_height;
    fbGetDisplayGeometry(device.display_type, &display_width, &display_height);

    system("setterm -cursor off");

    struct timespec time_beg, time_end;

    glViewport(0, 0, device.width, device.height);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    draw1();
    draw2();

    while (!done) {
        clock_gettime(CLOCK_MONOTONIC, &time_beg);

        eglSwapBuffers(device.display, device.surface);

        clock_gettime(CLOCK_MONOTONIC, &time_end);

        printf("Current FPS: %.3Lf%10s\r",
            1.0L / ((time_end.tv_sec - time_beg.tv_sec) + ((time_end.tv_nsec - time_beg.tv_nsec) / 1000000000.0)),
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
