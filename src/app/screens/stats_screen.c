#include "stats_screen.h"

#include "core/string_builder.h"
#include "ui/header.h"
#include "ui/list_navigation.h"


static const char *const LABELS[] = {
    "Total songs", "Artists", "Albums", "Music hours"
};
static int count(const void *context) { (void)context; return 4; }
static const char *label(const void *context, int index) { (void)context; return LABELS[index]; }
static const char *detail(const void *context, int index)
{
    const StatsScreen *s = context;
    return s->values[index];
}
static void copy_text(char *buffer, const char *text)
{
    StringBuilder builder = string_builder_make(buffer, 32);
    string_builder_append(&builder, text);
}
static void number(char *buffer, uint32_t value)
{
    StringBuilder text = string_builder_make(buffer, 32);
    string_builder_append_unsigned(&text, value);
}
static void format_duration(StatsScreen *s)
{
    if (s->duration_failed || s->next_track < s->track_count) {
        copy_text(s->values[3], s->duration_failed ? "Unavailable" : "Calculating...");
        return;
    }
    uint64_t minutes = s->duration_ms / 60000;
    StringBuilder text = string_builder_make(s->values[3], sizeof s->values[3]);
    string_builder_append_unsigned(&text, (uint32_t)(minutes / 60));
    string_builder_append(&text, "h ");
    string_builder_append_unsigned(&text, (uint32_t)(minutes % 60));
    string_builder_append_char(&text, 'm');
}
static void enter(void *context, uint32_t now_ms)
{
    StatsScreen *s = context;
    s->now_ms = now_ms;
    s->next_track = 0;
    s->duration_ms = 0;
    s->duration_failed = false;
    s->track_count = s->library ? library_count(s->library, LIBRARY_TRACKS) : 0;
    if (s->library) {
        number(s->values[0], s->track_count);
        number(s->values[1], library_count(s->library, LIBRARY_ARTISTS));
        number(s->values[2], library_count(s->library, LIBRARY_ALBUMS));
    } else {
        for (int i = 0; i < 3; i++) copy_text(s->values[i], "Unavailable");
        s->duration_failed = true;
    }
    format_duration(s);
    list_view_init(&s->list, &s->model, s->theme, now_ms);
}
static void update(void *context, uint32_t now_ms)
{
    StatsScreen *s = context;
    s->now_ms = now_ms;
    s->list.visible_rows = theme_content_area(s->theme).height / s->theme->metrics.row_height;
    /* Bound library reads so playback and navigation keep getting serviced. */
    for (int i = 0; i < 16 && s->next_track < s->track_count && !s->duration_failed; i++) {
        LibraryTrack track;
        if (!library_read_track(s->library, s->next_track++, &track)) {
            s->duration_failed = true;
            break;
        }
        s->duration_ms += track.duration_ms;
    }
    format_duration(s);
}
static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    StatsScreen *s = context;
    return list_navigation_handle(&s->list, event, now_ms, (ListSelection){0});
}
static void draw(void *context, Canvas *canvas)
{
    StatsScreen *s = context;
    Rect content = theme_content_area(s->theme);
    canvas_fill_rect(canvas, content, s->theme->colors.background);
    header_draw(canvas, s->theme, "Stats");
    list_view_draw(&s->list, canvas, content, s->now_ms);
}
Screen *stats_screen_init(StatsScreen *s, const Library *library, const Theme *theme)
{
    *s = (StatsScreen){.theme = theme, .library = library};
    s->model = (ListModel){.context = s, .count = count, .label = label, .detail = detail,
                          .detail_max_width = 112};
    s->screen = (Screen){.title = "Stats", .context = s, .enter = enter,
                         .update = update, .handle_key = handle_key, .draw = draw};
    return &s->screen;
}
