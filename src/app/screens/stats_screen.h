#pragma once

#include "library/library.h"
#include "ui/list_view.h"
#include "ui/screen.h"

typedef struct StatsScreen {
    Screen screen;
    const Theme *theme;
    const Library *library;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    uint32_t next_track;
    uint32_t track_count;
    uint64_t duration_ms;
    bool duration_failed;
    char values[4][32];
} StatsScreen;

Screen *stats_screen_init(StatsScreen *stats, const Library *library, const Theme *theme);
