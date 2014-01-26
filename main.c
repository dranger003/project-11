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

// t = time
// b = beginning
// c = change
// d = duration
// Example:
// From 50 to 200 in 1 second
// t=0s, b=50, c=150, d=1s
double quadratic_ease_in_out(double t, double b, double c, double d)
{
    static double ov = 0;
    t /= d / 2;
    if (t < 1) {
        double nv = c / 2 * t * t + b;
        if (nv < ov) return ov;
        return ov = nv, nv;
    }
    --t;
    double nv = -c / 2 * (t * (t - 2) - 1) + b;
    if (nv < ov) return ov;
    return ov = nv, nv;
}

struct timespec clock_gettime_diff(struct timespec *time_beg, struct timespec *time_end)
{
    struct timespec time_diff;
    time_diff.tv_sec = time_end->tv_sec - time_beg->tv_sec;
    time_diff.tv_nsec = time_end->tv_nsec - time_beg->tv_nsec;
    return time_diff;
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

struct draw_context {
    GLfloat vertices[8];
    const GLfloat *coordinates;
    const GLubyte *texels;
    GLuint program;
    GLint v_position;
    GLint v_coordinates;
    GLint texture_uniform;
};

struct draw_context draw1_context;
struct draw_context draw2_context;

void draw1_init()
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
    draw1_context.program = program;

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked);

    glUseProgram(program);
}

void draw1()
{
    glUseProgram(draw1_context.program);

    GLfloat vertices[] = {
        -1,  1,  1,  1,
        -1, -1,  1, -1,
    };
//    draw1_context.vertices = vertices;

    static const GLfloat coordinates[] = {
        0, 0, 1, 0,
        0, 1, 1, 1,
    };
    draw1_context.coordinates = coordinates;

    // 2x2 RGB texture
    static const GLubyte texels[] = {
        255,   0,   0,   0, 255,   0,
          0,   0, 255, 255, 255,   0,
    };
    draw1_context.texels = texels;

    GLint v_position = glGetAttribLocation(draw1_context.program, "v_position");
    glEnableVertexAttribArray(v_position);
    GLint v_coordinates = glGetAttribLocation(draw1_context.program, "v_coordinates");
    glVertexAttribPointer(v_position, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    draw1_context.v_position = v_position;

    glEnableVertexAttribArray(v_coordinates);
    glVertexAttribPointer(v_coordinates, 2, GL_FLOAT, GL_FALSE, 0, coordinates);
    draw1_context.v_coordinates = v_coordinates;

    GLint texture_uniform = glGetUniformLocation(draw1_context.program, "texture");
    glUniform1i(texture_uniform, 0);
    draw1_context.texture_uniform = texture_uniform;

    GLuint texture;
    glGenTextures(1, &texture);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, draw1_context.texels);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void draw2_init()
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
    draw2_context.program = program;

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glLinkProgram(program);

    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(linked);

    glUseProgram(program);

    GLfloat x = 640 / 960.0;
    GLfloat y = 360 / 540.0;
    draw2_context.vertices[0] = -x;
    draw2_context.vertices[1] = y;
    draw2_context.vertices[2] = x;
    draw2_context.vertices[3] = y;
    draw2_context.vertices[4] = -x;
    draw2_context.vertices[5] = -y;
    draw2_context.vertices[6] = x;
    draw2_context.vertices[7] = -y;
}

void draw2()
{
    glUseProgram(draw2_context.program);

//    GLfloat x = 640 / 960.0;
//    GLfloat y = 360 / 540.0;
//    GLfloat vertices[] = {
//        -x,  y,  x,  y,
//        -x, -y,  x, -y,
//    };

    static const GLfloat coordinates[] = {
        0, 0, 1, 0,
        0, 1, 1, 1,
    };
    draw2_context.coordinates = coordinates;

    // 2x2 RGB texture
    static const GLubyte texels[] = {
          0,   0, 255,   0,   0, 255,
          0,   0, 255,   0,   0, 255,
    };
    draw2_context.texels = texels;

    GLint v_position = glGetAttribLocation(draw2_context.program, "v_position");
    glEnableVertexAttribArray(v_position);
    glVertexAttribPointer(v_position, 2, GL_FLOAT, GL_FALSE, 0, draw2_context.vertices);
    draw2_context.v_position = v_position;

    GLint v_coordinates = glGetAttribLocation(draw2_context.program, "v_coordinates");
    glEnableVertexAttribArray(v_coordinates);
    glVertexAttribPointer(v_coordinates, 2, GL_FLOAT, GL_FALSE, 0, coordinates);
    draw2_context.v_coordinates = v_coordinates;

    GLint texture_uniform = glGetUniformLocation(draw2_context.program, "texture");
    glUniform1i(texture_uniform, 0);
    draw2_context.texture_uniform = texture_uniform;

    GLuint texture;
    glGenTextures(1, &texture);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, draw2_context.texels);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main(int argc, char *argv[])
{
//    {
//        system("echo 0 > /sys/devices/virtual/graphics/fbcon/cursor_blink");
//        system("setterm -cursor off");

//        struct timespec time_beg, time_end, time_diff;
//        double elapsed = 0, v;
//        int done = 0;

//        clock_gettime(CLOCK_MONOTONIC, &time_beg);
//        do {
//            v = quadratic_ease_in_out(elapsed, 50, 200, 2.5);
//            printf("%.3fs, %.3f                \r", elapsed, v);

//            clock_gettime(CLOCK_MONOTONIC, &time_end);
//            time_diff = clock_gettime_diff(&time_beg, &time_end);
//            elapsed = time_diff.tv_sec + time_diff.tv_nsec / 1000000000.0;
//            if (elapsed > 3)
//                done = 1;
//        } while (!done);

//        printf("\n");

//        system("setterm -cursor on");
//        system("echo 1 > /sys/devices/virtual/graphics/fbcon/cursor_blink");
//    }

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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        draw1_init();
        draw2_init();
    }

    struct timespec t1, t2, d;
    double e, v;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    while (!done) {
        clock_gettime(CLOCK_MONOTONIC, &time_beg);

        {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            clock_gettime(CLOCK_MONOTONIC, &t2);
            d = clock_gettime_diff(&t1, &t2);
            e = d.tv_sec + d.tv_nsec / 1000000000.0;

            v = quadratic_ease_in_out(e, 0, 640, 0.25);

            GLfloat x = v / 960.0;
            GLfloat y = 360 / 540.0;
            draw2_context.vertices[0] = -x;
            draw2_context.vertices[1] = y;
            draw2_context.vertices[2] = x;
            draw2_context.vertices[3] = y;
            draw2_context.vertices[4] = -x;
            draw2_context.vertices[5] = -y;
            draw2_context.vertices[6] = x;
            draw2_context.vertices[7] = -y;

            draw1();
            draw2();
        }

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
