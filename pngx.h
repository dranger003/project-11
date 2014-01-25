#include <png.h>

struct gl_texture_t
{
    GLuint id;

    GLsizei width;
    GLsizei height;

    GLenum pixel_format;
    GLint bytes_per_pixel;

    GLubyte *texels;
};

int png_texture_info(int color_type, struct gl_texture_t *texture);
int png_texture_load(const char *file_name, struct gl_texture_t *texture);
