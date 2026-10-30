#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx/text.h"

typedef struct LabelFit {
    size_t byte_count;
    bool ellipsis;
} LabelFit;

typedef struct Label {
    Rect area;
    const char *text;
} Label;

typedef struct Marquee {
    int offset;
    int gap;
} Marquee;

LabelFit label_fit(const Font *font, const char *text, int max_width);
int label_baseline(const Font *font, Rect area);
Rect label_centered_area(const Font *font, Rect bounds, const char *text);
void label_draw(Canvas *canvas, TextStyle style, Label label);
void label_draw_scrolling(Canvas *canvas, TextStyle style, Label label, Marquee marquee);

int label_wrapped_height(const Font *font, const char *text, int width);
void label_draw_wrapped(Canvas *canvas, TextStyle style, Rect area, const char *text);
