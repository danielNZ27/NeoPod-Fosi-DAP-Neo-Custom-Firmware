#include "gfx/image.h"

#include <stdbool.h>

typedef struct Column {
    int x;
    int offset;
    int top;
    int height;
} Column;

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int interpolate(int from, int to, int offset, int span)
{
    return from + (to - from) * offset / span;
}

static Column column_at(Quad quad, int x)
{
    int offset = x - quad.left;
    return (Column){
        .x = x,
        .offset = offset,
        .top = interpolate(quad.left_top, quad.right_top, offset, quad.width),
        .height = interpolate(quad.left_height, quad.right_height, offset, quad.width),
    };
}

static Color *pixel_address(Canvas *canvas, int x, int y)
{
    Rect bounds = canvas->bounds;
    return &canvas->pixels[(y - bounds.y) * bounds.width + (x - bounds.x)];
}

enum {
    STEP_SHIFT = 16
};

typedef struct Sampler {
    const Color *column;
    uint32_t position;
    uint32_t step;
} Sampler;

static Sampler sampler_at(const Column *column, const Image *image, int quad_width, int skip)
{
    uint32_t height = (uint32_t)column->height;
    uint32_t step = (((uint32_t)image->height << STEP_SHIFT) + height - 1) / height;
    return (Sampler){
        .column = &image->pixels[column->offset * image->width / quad_width],
        .position = step * (uint32_t)skip,
        .step = step,
    };
}

static inline Color color_lerp(Color c1, Color c2, uint32_t w2)
{
    uint32_t w1 = 256u - w2;
    uint32_t r = (((c1 >> 11) * w1 + (c2 >> 11) * w2) + 128u) >> 8;
    uint32_t g = ((((c1 >> 5) & 0x3Fu) * w1 + ((c2 >> 5) & 0x3Fu) * w2) + 128u) >> 8;
    uint32_t b = (((c1 & 0x1Fu) * w1 + (c2 & 0x1Fu) * w2) + 128u) >> 8;
    return (Color)((r << 11) | (g << 5) | b);
}

static inline Color color_bilinear(const Image *image, uint32_t x0, uint32_t x1, uint32_t x_frac,
                                   uint32_t y0, uint32_t y1, uint32_t y_frac)
{
    const Color *row0 = &image->pixels[y0 * (uint32_t)image->width];
    const Color *row1 = &image->pixels[y1 * (uint32_t)image->width];
    Color p00 = row0[x0];
    Color p10 = row0[x1];
    Color p01 = row1[x0];
    Color p11 = row1[x1];
    if (x_frac == 0 && y_frac == 0) {
        return p00;
    }
    if (x_frac == 0) {
        return color_lerp(p00, p01, y_frac);
    }
    Color top = color_lerp(p00, p10, x_frac);
    Color bot = color_lerp(p01, p11, x_frac);
    return color_lerp(top, bot, y_frac);
}

static void draw_column(Canvas *canvas, Column column, const Image *image, int quad_width)
{
    Rect clip = canvas->clip;
    int first = max_of(column.top, clip.y);
    int last = min_of(column.top + column.height, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int stride = canvas->bounds.width;
    int width = image->width;
    Sampler sampler = sampler_at(&column, image, quad_width, first - column.top);
    Color *target = pixel_address(canvas, column.x, first);
    if (width < 32 || (quad_width == width && column.height == image->height)) {
        for (int count = last - first; count > 0; count--) {
            *target = sampler.column[(int)(sampler.position >> STEP_SHIFT) * width];
            target += stride;
            sampler.position += sampler.step;
        }
        return;
    }
    uint32_t x_num = (uint32_t)column.offset * (uint32_t)width;
    uint32_t x0 = x_num / (uint32_t)quad_width;
    uint32_t x_frac = ((x_num % (uint32_t)quad_width) << 8) / (uint32_t)quad_width;
    if (x0 >= (uint32_t)width) x0 = (uint32_t)width - 1;
    uint32_t x1 = (x0 + 1 < (uint32_t)width) ? x0 + 1 : x0;
    for (int count = last - first; count > 0; count--) {
        uint32_t y0 = sampler.position >> STEP_SHIFT;
        if (y0 >= (uint32_t)image->height) y0 = (uint32_t)image->height - 1;
        uint32_t y1 = (y0 + 1 < (uint32_t)image->height) ? y0 + 1 : y0;
        uint32_t y_frac = (sampler.position >> (STEP_SHIFT - 8)) & 0xFF;
        *target = color_bilinear(image, x0, x1, x_frac, y0, y1, y_frac);
        target += stride;
        sampler.position += sampler.step;
    }
}

static unsigned light_at(QuadLight light, Column column, int quad_width)
{
    return (unsigned)interpolate(light.left, light.right, column.offset, quad_width);
}

static void draw_lit_column(Canvas *canvas, Column column, const Image *image, int quad_width,
                            unsigned light)
{
    Rect clip = canvas->clip;
    int first = max_of(column.top, clip.y);
    int last = min_of(column.top + column.height, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int stride = canvas->bounds.width;
    int width = image->width;
    Sampler sampler = sampler_at(&column, image, quad_width, first - column.top);
    Color *target = pixel_address(canvas, column.x, first);
    if (width < 32 || (quad_width == width && column.height == image->height)) {
        for (int count = last - first; count > 0; count--) {
            *target = color_scale(sampler.column[(int)(sampler.position >> STEP_SHIFT) * width], light);
            target += stride;
            sampler.position += sampler.step;
        }
        return;
    }
    uint32_t x_num = (uint32_t)column.offset * (uint32_t)width;
    uint32_t x0 = x_num / (uint32_t)quad_width;
    uint32_t x_frac = ((x_num % (uint32_t)quad_width) << 8) / (uint32_t)quad_width;
    if (x0 >= (uint32_t)width) x0 = (uint32_t)width - 1;
    uint32_t x1 = (x0 + 1 < (uint32_t)width) ? x0 + 1 : x0;
    for (int count = last - first; count > 0; count--) {
        uint32_t y0 = sampler.position >> STEP_SHIFT;
        if (y0 >= (uint32_t)image->height) y0 = (uint32_t)image->height - 1;
        uint32_t y1 = (y0 + 1 < (uint32_t)image->height) ? y0 + 1 : y0;
        uint32_t y_frac = (sampler.position >> (STEP_SHIFT - 8)) & 0xFF;
        Color c = color_bilinear(image, x0, x1, x_frac, y0, y1, y_frac);
        *target = color_scale(c, light);
        target += stride;
        sampler.position += sampler.step;
    }
}

static void draw_reflected_column(Canvas *canvas, Column column, const Image *image, int quad_width,
                                  unsigned light, int rows)
{
    Rect clip = canvas->clip;
    int top = column.top + column.height;
    int first = max_of(top, clip.y);
    int last = min_of(top + rows, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int skip = first - top;
    int stride = canvas->bounds.width;
    int width = image->width;
    int last_row = image->height - 1;
    Sampler sampler = sampler_at(&column, image, quad_width, skip);
    uint32_t fade_step = ((uint32_t)light << COLOR_LIGHT_SHIFT) / (uint32_t)rows;
    uint32_t faded = ((uint32_t)light << COLOR_LIGHT_SHIFT) - fade_step * (uint32_t)skip;
    Color *target = pixel_address(canvas, column.x, first);
    for (int count = last - first; count > 0; count--) {
        int row = max_of(last_row - (int)(sampler.position >> STEP_SHIFT), 0);
        *target = color_scale(sampler.column[row * width], faded >> COLOR_LIGHT_SHIFT);
        target += stride;
        sampler.position += sampler.step;
        faded -= fade_step;
    }
}

static void fill_column(Canvas *canvas, Column column, Color color)
{
    Rect clip = canvas->clip;
    int first = max_of(column.top, clip.y);
    int last = min_of(column.top + column.height, clip.y + clip.height);
    for (int y = first; y < last; y++) {
        *pixel_address(canvas, column.x, y) = color;
    }
}

static bool quad_visible(const Canvas *canvas, Quad quad, int *first, int *last)
{
    Rect clip = canvas->clip;
    *first = max_of(quad.left, clip.x);
    *last = min_of(quad.left + quad.width, clip.x + clip.width);
    return quad.width > 0 && *first < *last;
}

void image_draw_quad(Canvas *canvas, Image image, Quad quad)
{
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            draw_column(canvas, column, &image, quad.width);
        }
    }
}

void image_draw_quad_lit(Canvas *canvas, Image image, Quad quad, QuadLight light)
{
    if (light.left >= COLOR_LIGHT_FULL && light.right >= COLOR_LIGHT_FULL) {
        image_draw_quad(canvas, image, quad);
        return;
    }
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            draw_lit_column(canvas, column, &image, quad.width,
                            light_at(light, column, quad.width));
        }
    }
}

void image_draw_quad_lit_fast(Canvas *canvas, Image image, Quad quad, QuadLight light)
{
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        int top = max_of(column.top, canvas->clip.y);
        int bottom = min_of(column.top + column.height, canvas->clip.y + canvas->clip.height);
        if (column.height <= 0 || top >= bottom) {
            continue;
        }
        Sampler sampler = sampler_at(&column, &image, quad.width, top - column.top);
        unsigned level = light_at(light, column, quad.width);
        Color *target = pixel_address(canvas, x, top);
        for (int y = top; y < bottom; y++) {
            Color pixel = sampler.column[(int)(sampler.position >> STEP_SHIFT) * image.width];
            *target = level >= COLOR_LIGHT_FULL ? pixel : color_scale(pixel, level);
            target += canvas->bounds.width;
            sampler.position += sampler.step;
        }
    }
}

void image_draw_reflection(Canvas *canvas, Image image, Quad quad, QuadLight light, int rows)
{
    int first = 0;
    int last = 0;
    if (rows <= 0 || image.width <= 0 || image.height <= 0 ||
        !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            draw_reflected_column(canvas, column, &image, quad.width,
                                  light_at(light, column, quad.width), rows);
        }
    }
}

void image_fill_quad(Canvas *canvas, Quad quad, Color color)
{
    int first = 0;
    int last = 0;
    if (!quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            fill_column(canvas, column, color);
        }
    }
}
