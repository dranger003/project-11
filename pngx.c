#include <stdlib.h>
#include <GLES2/gl2.h>

#include "pngx.h"

int png_texture_info(int color_type, struct gl_texture_t *texture)
{
    switch (color_type) {
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            texture->pixel_format = GL_LUMINANCE_ALPHA;
            texture->bytes_per_pixel = 2;
            break;
        case PNG_COLOR_TYPE_GRAY:
        case PNG_COLOR_TYPE_RGB:
            texture->pixel_format = GL_RGB;
            texture->bytes_per_pixel = 3;
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            texture->pixel_format = GL_RGBA;
            texture->bytes_per_pixel = 4;
            break;
        default:
            break;
    }

    return 0;
}

int png_texture_load(const char *file_name, struct gl_texture_t *texture)
{
    FILE *fp = fopen(file_name, "rb");
    if (!fp)
        return -1;

    png_byte magic[8];
    fread(magic, 1, sizeof(magic), fp);

    if (!png_check_sig(magic, sizeof(magic))) {
        fclose(fp);
        return -1;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return -1;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fclose(fp);
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return -1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fclose(fp);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return -1;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(magic));
    png_read_info(png_ptr, info_ptr);

    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY) {
        texture->bytes_per_pixel = 3;
        png_set_gray_to_rgb(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    else if (bit_depth < 8)
        png_set_packing(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    png_uint_32 width, height;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);
    texture->width = width;
    texture->height = height;

    png_texture_info(color_type, texture);

    texture->texels =
        (GLubyte *)malloc(sizeof(GLubyte) * texture->width * texture->height * texture->bytes_per_pixel);

    png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * texture->height);

    int i;
    for (i = 0; i < texture->height; i++)
        row_pointers[i] =
            (png_bytep)(texture->texels + ((texture->height - (i + 1)) * texture->width * texture->bytes_per_pixel));

    png_read_image(png_ptr, row_pointers);

    png_read_end(png_ptr, NULL);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    free(row_pointers);

    fclose(fp);

    return 0;
}
