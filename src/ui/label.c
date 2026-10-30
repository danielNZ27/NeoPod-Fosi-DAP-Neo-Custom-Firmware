#include "ui/label.h"

#include "core/utf8.h"

enum {
    ELLIPSIS_CODEPOINT = 0x2026
};

static const char ELLIPSIS_TEXT[] = "\xE2\x80\xA6";

static int advance_of(const Font *font, uint32_t codepoint)
{
    const Glyph *glyph = font_find_glyph(font, codepoint);
    return glyph != NULL ? glyph->advance : 0;
}

LabelFit label_fit(const Font *font, const char *text, int max_width)
{
    int ellipsis_width = advance_of(font, ELLIPSIS_CODEPOINT);
    int used = 0;
    const char *cursor = text;
    const char *cut = text;
    for (uint32_t codepoint = utf8_next(&cursor); codepoint != 0; codepoint = utf8_next(&cursor)) {
        used += advance_of(font, codepoint);
        if (used + ellipsis_width <= max_width) {
            cut = cursor;
        }
        if (used > max_width) {
            return (LabelFit){.byte_count = (size_t)(cut - text), .ellipsis = true};
        }
    }
    return (LabelFit){.byte_count = (size_t)(cursor - text), .ellipsis = false};
}

int label_baseline(const Font *font, Rect area)
{
    int top = area.y + (area.height - font_line_height(font)) / 2;
    return top + font->ascent;
}

Rect label_centered_area(const Font *font, Rect bounds, const char *text)
{
    int width = text_width(font, text);
    if (width > bounds.width) {
        width = bounds.width;
    }
    return (Rect){bounds.x + (bounds.width - width) / 2, bounds.y, width, bounds.height};
}

void label_draw(Canvas *canvas, TextStyle style, Label label)
{
    Canvas clipped = canvas_clipped(canvas, label.area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    LabelFit fit = label_fit(style.font, label.text, label.area.width);
    Point pen = {label.area.x, label_baseline(style.font, label.area)};
    pen.x = text_draw_span(&clipped, style, pen, (TextSpan){label.text, fit.byte_count});
    if (fit.ellipsis) {
        text_draw(&clipped, style, pen, ELLIPSIS_TEXT);
    }
}

void label_draw_scrolling(Canvas *canvas, TextStyle style, Label label, Marquee marquee)
{
    Canvas clipped = canvas_clipped(canvas, label.area);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    int baseline = label_baseline(style.font, label.area);
    int start = label.area.x - marquee.offset;
    int repeat_start = start + text_width(style.font, label.text) + marquee.gap;
    text_draw(&clipped, style, (Point){start, baseline}, label.text);
    text_draw(&clipped, style, (Point){repeat_start, baseline}, label.text);
}

/* Break only at UTF-8 boundaries; prefer word boundaries when a line fills. */
static TextSpan wrapped_line(const Font *font, const char **text, int width)
{
    const char *start = *text;
    const char *cursor = start;
    const char *end = start;
    const char *word_end = NULL;
    const char *word_next = NULL;
    int used = 0;
    while (*cursor != '\0') {
        const char *before = cursor;
        uint32_t cp = utf8_next(&cursor);
        if (cp == '\n') {
            *text = cursor;
            return (TextSpan){start, (size_t)(before - start)};
        }
        if (cp == ' ') {
            word_end = before;
            word_next = cursor;
        }
        int advance = advance_of(font, cp);
        if (used + advance > width && before != start) {
            if (word_end != NULL && word_end != start) {
                *text = word_next;
                return (TextSpan){start, (size_t)(word_end - start)};
            }
            *text = before;
            return (TextSpan){start, (size_t)(before - start)};
        }
        used += advance;
        end = cursor;
    }
    *text = cursor;
    return (TextSpan){start, (size_t)(end - start)};
}

int label_wrapped_height(const Font *font, const char *text, int width)
{
    if (width <= 0) {
        return 0;
    }
    int lines = 0;
    while (*text != '\0') {
        (void)wrapped_line(font, &text, width);
        lines++;
    }
    return lines * font_line_height(font);
}

void label_draw_wrapped(Canvas *canvas, TextStyle style, Rect area, const char *text)
{
    if (area.width <= 0) {
        return;
    }
    Canvas clipped = canvas_clipped(canvas, area);
    int height = label_wrapped_height(style.font, text, area.width);
    int y = area.y + (area.height - height) / 2 + style.font->ascent;
    while (*text != '\0') {
        TextSpan line = wrapped_line(style.font, &text, area.width);
        text_draw_span(&clipped, style, (Point){area.x, y}, line);
        y += font_line_height(style.font);
    }
}
